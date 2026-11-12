# One session, no snapshot: fresh c-8 -> build SANDO with the open-source solver -> record + verify + render
# the investor video -> download to demo\output\rebuild_<timestamp>\ -> destroy the droplet.
# Worst case: 100 min + 10 min slack -> 2 billed hours x $0.25 = $0.50 (< $0.90/day cap).
param([int]$MaxMinutes = 100, [ValidateSet('easy','medium','hard')][string]$Case = 'medium')
. (Join-Path $PSScriptRoot 'lib.ps1')
. (Join-Path $PSScriptRoot 'remote.ps1')
$root = Split-Path $PSScriptRoot -Parent
$out = Join-Path $root ('demo\output\rebuild_' + (Get-Date).ToString('yyyyMMdd_HHmm'))
New-Item -ItemType Directory -Force -Path $out | Out-Null
$started = $false
function Wait-RemoteFlag([string]$flag, [string]$log, [datetime]$until) {
    while ((Get-Date) -lt $until) {
        Start-Sleep -Seconds 60
        $s = Invoke-Remote "test -f ~/$flag && echo FLAG; tail -n 1 ~/$log" -AllowFail
        Write-Host ("[{0:HH:mm}] {1}" -f (Get-Date), ($s -replace "`r?`n", ' | '))
        if ($s -match 'FLAG') { return }
    }
    throw "timed out waiting for $flag"
}
try {
    & (Join-Path $PSScriptRoot 'up.ps1') -Size c-8 -MaxMinutes $MaxMinutes -Name 'sando-full' | Out-Host
    $started = $true
    $until = (Get-Date).AddMinutes($MaxMinutes - 8)
    Wait-Ssh
    Invoke-Remote 'mkdir -p ~/sando_demo' | Out-Null
    Copy-ToRemote (Join-Path $root 'solver') '~/sando_demo/'
    Copy-ToRemote (Join-Path $root 'remote') '~/sando_demo/'
    Invoke-Remote "sed -i 's/\r$//' ~/sando_demo/remote/*.sh; nohup bash -c 'bash ~/sando_demo/remote/build.sh 6; touch ~/build.done' >/dev/null 2>&1 &" | Out-Null
    Wait-RemoteFlag 'build.done' 'build.log' $until
    if ((Invoke-Remote 'grep -c "BUILD DONE" ~/build.log' -AllowFail).Trim() -eq '0') { throw 'build failed (see build.log)' }
    Invoke-Remote "nohup bash -c 'bash ~/sando_demo/remote/make_video.sh $Case > ~/video.log 2>&1; touch ~/video.done' >/dev/null 2>&1 &" | Out-Null
    Wait-RemoteFlag 'video.done' 'video.log' $until
    Invoke-Remote 'cd ~ && tar czf result.tgz build.log video.log video' | Out-Null
    Copy-FromRemote '~/result.tgz' (Join-Path $out 'result.tgz')
    Write-GuardLog "full session done -> $out\result.tgz (video/sando_mission_$Case.mp4 inside)"
}
catch { Write-GuardLog "session-full ERROR: $_"; try { Copy-FromRemote '~/build.log' (Join-Path $out 'build.log') } catch {}; throw }
finally { if ($started) { & (Join-Path $PSScriptRoot 'down.ps1') -Reason 'session-full-end' } }
