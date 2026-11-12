. (Join-Path $PSScriptRoot 'lib.ps1')
Initialize-DoToken
$live = Get-LiveDroplets
foreach ($d in $live) {
    Write-Host ("LIVE {0} id={1} size={2} deadline={3:u}" -f $d.name, $d.id, $d.size_slug, (Get-DeadlineFromTags $d))
}
if (-not $live.Count) { Write-Host 'No sando droplets running.' }
$spent = Get-TodaySpendUsd
Write-Host ("Today's project spend (conservative): {0:N3} USD; remaining {1:N3} USD of {2:N2} USD cap" -f $spent, ($Cfg.DailyCapUsd - $spent), $Cfg.DailyCapUsd)
Write-Host ("Cloud guard (DO Function, every 5 min): {0}" -f $(if (Test-CloudGuard) { 'ACTIVE' } else { 'NOT ACTIVE' }))
