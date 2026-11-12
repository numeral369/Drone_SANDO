# Create ONE droplet for a test session, only if it fits today's budget.
# Usage: .\up.ps1 -Size c-8 -MaxMinutes 60 [-Image <snapshot-name|image-slug>] [-UserData path]
param(
    [Parameter(Mandatory)][string]$Size,
    [Parameter(Mandatory)][int]$MaxMinutes,
    [string]$Image,
    [string]$Name = ('sando-' + (Get-Date -Format 'MMdd-HHmm')),
    [string]$UserData = (Join-Path $PSScriptRoot 'cloud-init.yaml')
)
. (Join-Path $PSScriptRoot 'lib.ps1')
Initialize-DoToken
if (-not (Test-CloudGuard)) { throw 'REFUSED: cloud guard (DO Function trigger sando-guard-5min) is not active.' }
Write-Host 'Cloud guard active (deletes sando droplets past their deadline every 5 min).'

if ($MaxMinutes -lt 5 -or $MaxMinutes -gt $Cfg.MaxSessionMin) { throw "MaxMinutes must be 5..$($Cfg.MaxSessionMin)" }
if (-not $Image) { $Image = $Cfg.Image }

# 1) one droplet at a time
$live = Get-LiveDroplets
if ($live.Count -gt 0) { throw "A sando droplet already exists ($($live[0].name)). Run down.ps1 first." }

# 2) budget check (conservative: whole hours, deadline + 10 min guard slack, snapshot storage)
$hourly   = Get-SizeHourlyUsd $Size
$nowUtc   = (Get-Date).ToUniversalTime()
$deadline = $nowUtc.AddMinutes($MaxMinutes)
$projected = Get-BilledUsd $nowUtc $deadline.AddMinutes(10) $hourly
$spent    = Get-TodaySpendUsd
$after    = [math]::Round($spent + $projected, 4)
Write-Host ("Budget: spent today {0:N3} USD + this session (worst case) {1:N3} USD = {2:N3} / cap {3:N2} USD" -f $spent, $projected, $after, $Cfg.DailyCapUsd)
if ($after -gt $Cfg.DailyCapUsd) { throw "REFUSED: session would exceed today's cap." }

# 3) SSH key
if (-not (Test-Path $Cfg.SshKeyPath)) { throw "SSH key $($Cfg.SshKeyPath) missing. Run setup-ssh.ps1." }
$key = Get-DoJson @('compute','ssh-key','list') | Where-Object { $_.name -eq $Cfg.SshKeyName }
if (-not $key) { throw "SSH key not registered on DO. Run setup-ssh.ps1." }

# 4) resolve image (snapshot names -> id)
$imageArg = $Image
$snap = Get-DoJson @('compute','snapshot','list','--resource','droplet') | Where-Object { $_.name -eq $Image }
if ($snap) { $imageArg = [string]$snap.id }

# 5) create (tags carry the deadline so any guard can enforce it)
$deadlineTag = 'sando-deadline-' + ([DateTimeOffset]$deadline).ToUnixTimeSeconds()
$createArgs = @('compute','droplet','create',$Name,'--size',$Size,'--image',$imageArg,'--region',$Cfg.Region,
    '--ssh-keys',[string]$key.id,'--tag-names',"$($Cfg.Tag),$deadlineTag",'--wait')
if ($UserData -and (Test-Path $UserData) -and -not $snap) { $createArgs += @('--user-data-file',$UserData) }
$d = $null
try {
    $d = (Get-DoJson $createArgs)[0]
    $row = [pscustomobject]@{
        droplet_id = [string]$d.id; name = $Name; size = $Size; image = $Image; hourly_usd = $hourly
        created_utc = (ConvertTo-Utc $d.created_at).ToString('o'); deadline_utc = $deadline.ToString('o')
        projected_usd = $projected; destroyed_utc = ''; cost_usd = ''; reason = ''
    }
    $rows = @(Read-Ledger) + $row
    Save-Ledger $rows
} catch {
    Write-GuardLog "up.ps1 failed after/while creating: $_ ; deleting any sando droplet"
    & (Join-Path $PSScriptRoot 'down.ps1') -Reason 'up-failed'
    throw
}
$ip = ($d.networks.v4 | Where-Object { $_.type -eq 'public' } | Select-Object -First 1).ip_address
Write-GuardLog "CREATED $Name id=$($d.id) size=$Size ip=$ip deadline=$($deadline.ToString('u'))"
Set-Content -Path (Join-Path $PSScriptRoot '.current_ip') -Value $ip -Encoding ascii
[pscustomobject]@{ Name = $Name; Id = $d.id; Ip = $ip; DeadlineUtc = $deadline }
