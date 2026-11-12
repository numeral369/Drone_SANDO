# Destroy all sando droplets and run the teardown check (T-TEARDOWN).
param([string]$Reason = 'manual')
. (Join-Path $PSScriptRoot 'lib.ps1')
Initialize-DoToken

foreach ($d in Get-LiveDroplets) {
    Invoke-Doctl compute droplet delete ([string]$d.id) --force | Out-Null
    Close-LedgerRow ([string]$d.id) $Reason
    Write-GuardLog "DELETED $($d.name) id=$($d.id) reason=$Reason"
}

# Reconcile ledger rows for droplets that vanished without us (e.g. deleted by the cloud guard):
# charge them their projected (worst-case) cost.
$liveIds = @(Get-LiveDroplets | ForEach-Object { [string]$_.id })
$rows = @(Read-Ledger)
foreach ($r in $rows) {
    if (-not $r.destroyed_utc -and ($liveIds -notcontains $r.droplet_id)) {
        $r.destroyed_utc = (Get-Date).ToUniversalTime().ToString('o'); $r.cost_usd = $r.projected_usd
        $r.reason = 'deleted-outside-scripts (cloud guard?)'
        Write-GuardLog "LEDGER reconciled $($r.name) id=$($r.droplet_id) cost=$($r.projected_usd)"
    }
}
if ($rows.Count) { Save-Ledger $rows }

# --- teardown check ---
$ok = $true
$left = Get-LiveDroplets
if ($left.Count) { $ok = $false; Write-Host "FAIL: droplets still present: $($left.name -join ',')" }
$vols = Get-DoJson @('compute','volume','list') | Where-Object { $_.name -like 'sando*' }
if ($vols) { $ok = $false; Write-Host "FAIL: volumes present: $($vols.name -join ',')" }
$ips = Get-DoJson @('compute','reserved-ip','list') | Where-Object { -not $_.droplet }
if ($ips) { $ok = $false; Write-Host "FAIL: unassigned reserved IPs: $($ips.ip -join ',')" }
$snaps = Get-DoJson @('compute','snapshot','list','--resource','droplet') | Where-Object { $_.name -like 'sando-*' }
$spent = Get-TodaySpendUsd
Write-Host ("Snapshots kept: {0}" -f ((@($snaps | ForEach-Object { "$($_.name) ($($_.size_gigabytes) GB)" }) -join ', ')))
Write-Host ("Today's project spend (conservative): {0:N3} USD / cap {1:N2} USD" -f $spent, $Cfg.DailyCapUsd)
if ($ok) { Write-Host 'TEARDOWN OK: no billable sando droplets/volumes/IPs left.' } else { throw 'TEARDOWN FAILED' }
Remove-Item (Join-Path $PSScriptRoot '.current_ip') -ErrorAction SilentlyContinue
