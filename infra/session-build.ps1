# Phase 1 session: create c-8, build SANDO with the open-source solver (build.sh runs T2),
# snapshot the result, and destroy the droplet.
# Worst-case cost: 150 min + 10 min slack -> 3 billed hours x $0.25 = $0.75 (< $0.90 cap).
# Every exit path (success, error, Ctrl+C) ends in down.ps1.
param([int]$MaxMinutes = 150, [string]$SnapshotName = 'sando-built-v1')
. (Join-Path $PSScriptRoot 'lib.ps1')
. (Join-Path $PSScriptRoot 'remote.ps1')
$root = Split-Path $PSScriptRoot -Parent
$results = Join-Path $root 'results'
New-Item -ItemType Directory -Force -Path $results | Out-Null
$started = $false
try {
    & (Join-Path $PSScriptRoot 'up.ps1') -Size c-8 -MaxMinutes $MaxMinutes -Name 'sando-build' | Out-Host
    $started = $true
    Wait-Ssh
    Invoke-Remote 'mkdir -p ~/sando_demo' | Out-Null
    Copy-ToRemote (Join-Path $root 'solver') '~/sando_demo/'
    Copy-ToRemote (Join-Path $root 'remote') '~/sando_demo/'
    Invoke-Remote "sed -i 's/\r$//' ~/sando_demo/remote/*.sh; nohup bash ~/sando_demo/remote/build.sh 6 >/dev/null 2>&1 &" | Out-Null
    Write-GuardLog 'build started'
    $deadline = (Get-Date).AddMinutes($MaxMinutes - 25)   # leave time for snapshot + teardown
    while ($true) {
        Start-Sleep -Seconds 60
        $tail = Invoke-Remote 'tail -n 3 ~/build.log; pgrep -f remote/build.sh >/dev/null && echo RUNNING || echo STOPPED' -AllowFail
        Write-Host ("[{0:HH:mm}] {1}" -f (Get-Date), ($tail -replace "`r?`n", ' | '))
        if ($tail -match 'BUILD DONE') { break }
        if ($tail -match 'STOPPED') { throw 'build stopped without BUILD DONE (see results/build.log)' }
        if ((Get-Date) -gt $deadline) { throw 'build exceeded the time budget' }
    }
    Copy-FromRemote '~/build.log' (Join-Path $results 'build.log')
    Write-GuardLog 'build OK; snapshotting'
    Invoke-Remote 'sync; shutdown -h +0' -AllowFail | Out-Null
    $d = (Get-LiveDroplets)[0]
    Start-Sleep -Seconds 30
    Invoke-Doctl compute droplet-action snapshot ([string]$d.id) --snapshot-name $SnapshotName --wait | Out-Host
    Write-GuardLog "snapshot $SnapshotName created"
}
catch { Write-GuardLog "session-build ERROR: $_"; try { Copy-FromRemote '~/build.log' (Join-Path $results 'build.log') } catch {} ; throw }
finally { if ($started) { & (Join-Path $PSScriptRoot 'down.ps1') -Reason 'session-build-end' } }
