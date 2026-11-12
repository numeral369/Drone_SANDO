# Deploys the cloud guard as a DigitalOcean Function with a 5-minute scheduled trigger.
# Uses the DO REST API directly (doctl serverless cannot install on this Windows machine).
# The DO token is stored as a parameter of the function inside the user's own DO account.
. (Join-Path $PSScriptRoot 'lib.ps1')
Initialize-DoToken
$h = @{ Authorization = "Bearer $env:DIGITALOCEAN_TOKEN"; 'Content-Type' = 'application/json' }
$api = 'https://api.digitalocean.com/v2'
$label = 'sando-guard'

# 1) namespace (reuse if it exists)
$nsList = (Invoke-RestMethod -Uri "$api/functions/namespaces" -Headers $h).namespaces
$ns = $nsList | Where-Object { $_.label -eq $label }
if (-not $ns) {
    $body = @{ region = $Cfg.Region; label = $label } | ConvertTo-Json
    $ns = (Invoke-RestMethod -Method Post -Uri "$api/functions/namespaces" -Headers $h -Body $body).namespace
    Write-Host "Created namespace $($ns.namespace)"
}
$ns = (Invoke-RestMethod -Uri "$api/functions/namespaces/$($ns.namespace)" -Headers $h).namespace

# 2) action (OpenWhisk API with namespace credentials)
$basic = [Convert]::ToBase64String([Text.Encoding]::ASCII.GetBytes("$($ns.uuid):$($ns.key)"))
$wh = @{ Authorization = "Basic $basic"; 'Content-Type' = 'application/json' }
# [IO.File]::ReadAllText returns a plain string (Get-Content -Raw adds PS metadata that ConvertTo-Json serializes)
$code = [string][IO.File]::ReadAllText((Join-Path $PSScriptRoot 'cloud-guard\guard.py'))
$action = @{
    namespace = $ns.namespace; name = 'sando-guard'
    exec = @{ kind = 'python:default'; code = $code; main = 'main' }
    parameters = @(@{ key = 'token'; value = $env:DIGITALOCEAN_TOKEN })
    limits = @{ timeout = 60000; memory = 128 }
} | ConvertTo-Json -Depth 6
Invoke-RestMethod -Method Put -Uri "$($ns.api_host)/api/v1/namespaces/$($ns.namespace)/actions/sando-guard?overwrite=true" -Headers $wh -Body $action | Out-Null
Write-Host 'Deployed function sando-guard'

# 3) scheduled trigger every 5 minutes
$trs = (Invoke-RestMethod -Uri "$api/functions/namespaces/$($ns.namespace)/triggers" -Headers $h).triggers
if (-not ($trs | Where-Object { $_.name -eq 'sando-guard-5min' })) {
    $tb = @{ name = 'sando-guard-5min'; function = 'sando-guard'; type = 'SCHEDULED'; is_enabled = $true
             scheduled_details = @{ cron = '*/5 * * * *'; body = @{} } } | ConvertTo-Json -Depth 5
    Invoke-RestMethod -Method Post -Uri "$api/functions/namespaces/$($ns.namespace)/triggers" -Headers $h -Body $tb | Out-Null
    Write-Host 'Created trigger sando-guard-5min (*/5 * * * *)'
}

# 4) test invoke
$r = Invoke-RestMethod -Method Post -Uri "$($ns.api_host)/api/v1/namespaces/$($ns.namespace)/actions/sando-guard?blocking=true&result=true" -Headers $wh -Body '{}'
Write-Host ('Test invoke result: ' + ($r | ConvertTo-Json -Depth 6 -Compress))
