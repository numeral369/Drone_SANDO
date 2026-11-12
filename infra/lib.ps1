# SANDO demo - shared helpers. Dot-source after config.ps1.
# Windows PowerShell 5.1 compatible.
. (Join-Path $PSScriptRoot 'config.ps1')
$ErrorActionPreference = 'Stop'
# Numbers in ledger.csv must use '.' regardless of the Windows locale (pt-PT uses ',').
[Threading.Thread]::CurrentThread.CurrentCulture = [Globalization.CultureInfo]::InvariantCulture
[Threading.Thread]::CurrentThread.CurrentUICulture = [Globalization.CultureInfo]::InvariantCulture

function ConvertTo-Utc { param($Value)
    # Parses ISO-8601 strings (with or without 'Z') or DateTime values into a UTC-kind DateTime.
    if ($Value -is [datetime]) { return $Value.ToUniversalTime() }
    $styles = [Globalization.DateTimeStyles]::AdjustToUniversal -bor [Globalization.DateTimeStyles]::AssumeUniversal
    return [datetime]::Parse([string]$Value, [Globalization.CultureInfo]::InvariantCulture, $styles)
}

function Initialize-DoToken {
    # Uses $env:DIGITALOCEAN_TOKEN when set (and caches it DPAPI-encrypted for the guard task);
    # otherwise loads the cached copy. The cache can only be decrypted by this Windows user.
    if ($env:DIGITALOCEAN_TOKEN) {
        if (-not (Test-Path $Cfg.TokenFile)) {
            $sec = ConvertTo-SecureString $env:DIGITALOCEAN_TOKEN -AsPlainText -Force
            ConvertFrom-SecureString $sec | Set-Content -Path $Cfg.TokenFile -Encoding ascii
        }
        return
    }
    if (-not (Test-Path $Cfg.TokenFile)) { throw 'No DIGITALOCEAN_TOKEN and no cached token.' }
    $sec = Get-Content $Cfg.TokenFile | ConvertTo-SecureString
    $b = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($sec)
    try { $env:DIGITALOCEAN_TOKEN = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($b) }
    finally { [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($b) }
}

function Invoke-Doctl {
    # Runs doctl, returns stdout as one string, throws on non-zero exit.
    $out = & doctl @args 2>&1
    if ($LASTEXITCODE -ne 0) { throw "doctl $($args -join ' ') failed: $out" }
    return ($out | Out-String)
}

function Get-DoJson { param([string[]]$DoArgs)
    $txt = Invoke-Doctl @DoArgs '-o' 'json'
    if (-not $txt.Trim()) { return @() }
    # PS 5.1: ConvertFrom-Json emits a JSON array as ONE object; re-pipe to enumerate its elements.
    $parsed = $txt | ConvertFrom-Json
    return @($parsed | ForEach-Object { $_ })
}

function Get-SizeHourlyUsd { param([string]$Slug)
    $s = Get-DoJson @('compute','size','list') | Where-Object { $_.slug -eq $Slug }
    if (-not $s) { throw "Unknown size $Slug" }
    return [double]$s.price_hourly
}

function Get-BilledUsd { param([datetime]$StartUtc, [datetime]$EndUtc, [double]$Hourly)
    $h = [math]::Ceiling(($EndUtc - $StartUtc).TotalHours)
    if ($h -lt $Cfg.MinBilledHours) { $h = $Cfg.MinBilledHours }
    return [math]::Round($h * $Hourly, 4)
}

function Get-DeadlineFromTags { param($Droplet)
    foreach ($t in @($Droplet.tags)) {
        if ($t -match '^sando-deadline-(\d+)$') {
            return [DateTimeOffset]::FromUnixTimeSeconds([int64]$Matches[1]).UtcDateTime
        }
    }
    return $null
}

function Get-LiveDroplets {
    return @(Get-DoJson @('compute','droplet','list','--tag-name',$Cfg.Tag))
}

function Read-Ledger {
    if (Test-Path $Cfg.LedgerPath) { return @(Import-Csv $Cfg.LedgerPath) }
    return @()
}

function Save-Ledger { param($Rows)
    $Rows | Export-Csv -Path $Cfg.LedgerPath -NoTypeInformation -Encoding UTF8
}

function Get-SnapshotDailyUsd {
    $snaps = Get-DoJson @('compute','snapshot','list','--resource','droplet') |
        Where-Object { $_.name -like 'sando-*' }
    $gb = 0.0; foreach ($s in $snaps) { $gb += [double]$s.size_gigabytes }
    return [math]::Round($gb * $Cfg.SnapshotUsdPerGbMonth / 30.0, 4)
}

function Get-TodaySpendUsd {
    # Conservative: running droplets are counted up to their deadline (not "now"),
    # whole hours rounded up, and today's snapshot storage is added.
    $today = (Get-Date).Date
    $total = 0.0
    $ledger = Read-Ledger
    $live = Get-LiveDroplets
    $liveIds = @($live | ForEach-Object { [string]$_.id })
    foreach ($r in $ledger) {
        if ((ConvertTo-Utc $r.created_utc).ToLocalTime().Date -ne $today) { continue }
        if ($r.destroyed_utc) { $total += [double]$r.cost_usd }
        elseif ($liveIds -contains $r.droplet_id) {
            $end = ConvertTo-Utc $r.deadline_utc
            if ((Get-Date).ToUniversalTime() -gt $end) { $end = (Get-Date).ToUniversalTime().AddMinutes(10) }
            $total += Get-BilledUsd (ConvertTo-Utc $r.created_utc) $end ([double]$r.hourly_usd)
        } else { $total += [double]$r.projected_usd }   # vanished without our record: assume projected
    }
    foreach ($d in $live) {   # droplets with our tag that are not in the ledger (should never happen)
        if (-not ($ledger | Where-Object { $_.droplet_id -eq [string]$d.id })) {
            $total += Get-BilledUsd (ConvertTo-Utc $d.created_at) (Get-Date).ToUniversalTime().AddHours(1) ([double]$d.size.price_hourly)
        }
    }
    $total += Get-SnapshotDailyUsd
    return [math]::Round($total, 4)
}

function Close-LedgerRow { param([string]$DropletId, [string]$Reason)
    $rows = Read-Ledger
    foreach ($r in $rows) {
        if ($r.droplet_id -eq $DropletId -and -not $r.destroyed_utc) {
            $now = (Get-Date).ToUniversalTime()
            $r.destroyed_utc = $now.ToString('o')
            $r.cost_usd = Get-BilledUsd (ConvertTo-Utc $r.created_utc) $now ([double]$r.hourly_usd)
            $r.reason = $Reason
        }
    }
    Save-Ledger $rows
}

function Test-CloudGuard {
    # True only if the DO Function trigger 'sando-guard-5min' exists and is enabled.
    $h = @{ Authorization = "Bearer $env:DIGITALOCEAN_TOKEN" }
    $api = 'https://api.digitalocean.com/v2/functions/namespaces'
    $ns = (Invoke-RestMethod -Uri $api -Headers $h).namespaces | Where-Object { $_.label -eq 'sando-guard' }
    if (-not $ns) { return $false }
    $tr = (Invoke-RestMethod -Uri "$api/$($ns.namespace)/triggers" -Headers $h).triggers |
        Where-Object { $_.name -eq 'sando-guard-5min' }
    return [bool]($tr -and $tr.is_enabled)
}

function Write-GuardLog { param([string]$Msg)
    $line = '{0:u} {1}' -f (Get-Date).ToUniversalTime(), $Msg
    Add-Content -Path (Join-Path $Cfg.InfraDir 'guard.log') -Value $line
    Write-Host $line
}
