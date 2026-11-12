# Mission session: restore the built snapshot on c-8, run dynamic missions, download raw results,
# write one Markdown report per mission (reports/missions/), destroy the droplet.
# Usage: .\session-missions.ps1 -Cases easy,medium -Trials 3 [-StartSeed 0] [-MaxMinutes 60]
param(
    [string[]]$Cases = @('easy'),
    [int]$Trials = 1,
    [int]$StartSeed = 0,
    [int]$MaxMinutes = 60,
    [string]$Snapshot = 'sando-built-v1',
    [string]$Notes = '',
    [string]$Reanalyze = '',      # local case dir from an earlier session to collision-check on this droplet
    [string]$VideoCase = ''       # easy|medium|hard: also record + render the investor video
)
. (Join-Path $PSScriptRoot 'lib.ps1')
. (Join-Path $PSScriptRoot 'remote.ps1')
$root = Split-Path $PSScriptRoot -Parent
$raw = Join-Path $root 'results\missions_raw'
New-Item -ItemType Directory -Force -Path $raw | Out-Null
$session = (Get-Date).ToUniversalTime().ToString('yyyy-MM-dd HH:mm') + ' UTC'
$started = $false
try {
    & (Join-Path $PSScriptRoot 'up.ps1') -Size c-8 -MaxMinutes $MaxMinutes -Image $Snapshot -Name 'sando-missions' | Out-Host
    $started = $true
    Wait-Ssh
    Copy-ToRemote (Join-Path $root 'remote') '~/sando_demo/'
    Invoke-Remote "sed -i 's/\r$//' ~/sando_demo/remote/*.sh" | Out-Null
    $stopAt = (Get-Date).AddMinutes($MaxMinutes - 8)   # keep time to download + teardown
    if ($Reanalyze) {
        Write-GuardLog "re-analysing $Reanalyze on droplet"
        Invoke-Remote 'mkdir -p ~/reanalyze' | Out-Null
        Copy-ToRemote $Reanalyze '~/reanalyze/'
        $leaf = Split-Path $Reanalyze -Leaf
        Write-Host (Invoke-Remote "python3 -c 'import pandas' 2>/dev/null || apt-get install -y -q python3-pandas >/dev/null; source /opt/ros/humble/setup.bash; source /root/sando_ws/install/setup.bash; python3 ~/sando_demo/remote/postprocess.py ~/reanalyze/$leaf 2>&1 | tail -n 8" -AllowFail)
        Copy-FromRemote "~/reanalyze/$leaf/trials_augmented.json" (Join-Path $Reanalyze 'trials_augmented.json')
    }
    foreach ($case in $Cases) {
        Write-GuardLog "missions: $case x$Trials (seed $StartSeed)"
        Invoke-Remote "rm -f ~/mission.done; nohup bash -c 'bash ~/sando_demo/remote/run_missions.sh $case $Trials $StartSeed > ~/mission_$case.log 2>&1; touch ~/mission.done' >/dev/null 2>&1 &" | Out-Null
        while ($true) {
            Start-Sleep -Seconds 30
            $st = Invoke-Remote "test -f ~/mission.done && echo DONE; tail -n 2 ~/mission_$case.log" -AllowFail
            Write-Host ("[{0:HH:mm}] {1}" -f (Get-Date), (($st -split "`n" | Select-Object -Last 3) -join ' | '))
            if ($st -match 'DONE') { break }
            if ((Get-Date) -gt $stopAt) { Write-GuardLog "time budget reached during $case"; break }
        }
        Copy-FromRemote "~/mission_$case.log" (Join-Path $raw "mission_${case}_$((Get-Date).ToString('HHmmss')).log")
        if ((Get-Date) -gt $stopAt) { break }
    }
    if ($VideoCase -and (Get-Date) -lt $stopAt.AddMinutes(-12)) {
        Write-GuardLog "video: recording + rendering dynamic $VideoCase"
        Invoke-Remote "rm -f ~/video.done; nohup bash -c 'bash ~/sando_demo/remote/make_video.sh $VideoCase > ~/video.log 2>&1; touch ~/video.done' >/dev/null 2>&1 &" | Out-Null
        while ($true) {
            Start-Sleep -Seconds 30
            $st = Invoke-Remote 'test -f ~/video.done && echo DONE; grep "\[video\]" ~/video.log | tail -n 1' -AllowFail
            Write-Host ("[{0:HH:mm}] {1}" -f (Get-Date), ($st -replace "`r?`n", ' | '))
            if ($st -match 'DONE') { break }
            if ((Get-Date) -gt $stopAt) { Write-GuardLog 'time budget reached during video'; break }
        }
        $vdir = Join-Path $root 'demo\output'
        New-Item -ItemType Directory -Force -Path $vdir | Out-Null
        Copy-FromRemote '~/video.log' (Join-Path $vdir 'video.log')
        Invoke-Remote 'cd ~ && tar czf video.tgz video/*.mp4 video/*.json video/*.log' -AllowFail | Out-Null
        Copy-FromRemote '~/video.tgz' (Join-Path $vdir ("video_{0}.tgz" -f (Get-Date).ToString('yyyyMMdd_HHmmss')))
    }
    Invoke-Remote 'cd ~ && tar czf missions.tgz missions' | Out-Null
    $tgz = Join-Path $raw ("missions_{0}.tgz" -f (Get-Date).ToString('yyyyMMdd_HHmmss'))
    Copy-FromRemote '~/missions.tgz' $tgz
    Write-GuardLog "downloaded $tgz"
}
catch { Write-GuardLog "session-missions ERROR: $_"; throw }
finally { if ($started) { & (Join-Path $PSScriptRoot 'down.ps1') -Reason 'session-missions-end' } }

# ---- reports (local, via WSL python; no cloud cost) ----
$wslRoot = '/mnt/c' + ($root.Substring(2) -replace '\\', '/')
$wslTgz = '/mnt/c' + ($tgz.Substring(2) -replace '\\', '/')
$script = @"
set -e
X=`$(mktemp -d); tar xzf '$wslTgz' -C `$X
for d in `$X/missions/*/; do
  case_name=`$(basename `$d | sed -E 's/.*_(easy|medium|hard)`$/\1/')
  j="`$d`$case_name/trials_augmented.json"
  [ -f "`$j" ] || j=`$(ls -S `$d`$case_name/benchmark_*.json 2>/dev/null | head -1)
  [ -n "`$j" ] && python3 '$wslRoot/reports/make_mission_report.py' "`$j" "`$case_name" '$wslRoot/reports/missions' --session '$session' --notes '$Notes'
done
"@
# write without BOM and with LF line endings (a BOM breaks bash: "﻿set: command not found")
$tmp = Join-Path $env:TEMP 'sando_reports.sh'
[IO.File]::WriteAllText($tmp, ($script -replace "`r`n", "`n"), (New-Object Text.UTF8Encoding $false))
$wslTmp = '/mnt/c' + ($tmp.Substring(2) -replace '\\', '/')
wsl -d Ubuntu-24.04 -e bash $wslTmp
Write-Host "Reports: $(Join-Path $root 'reports\missions\INDEX.md')"
