# Deletes every DigitalOcean resource this project creates (droplets, snapshots, SSH key, cloud-guard
# Functions namespace) and the local credentials it stores. Keeps scripts, reports and videos.
. (Join-Path $PSScriptRoot 'lib.ps1')
Initialize-DoToken
$h = @{ Authorization = "Bearer $env:DIGITALOCEAN_TOKEN" }
foreach ($d in Get-LiveDroplets) { Invoke-Doctl compute droplet delete ([string]$d.id) --force | Out-Null; Write-Host "deleted droplet $($d.name)" }
foreach ($s in (Get-DoJson @('compute','snapshot','list','--resource','droplet') | Where-Object { $_.name -like 'sando-*' })) {
    Invoke-Doctl compute snapshot delete ([string]$s.id) --force | Out-Null; Write-Host "deleted snapshot $($s.name)" }
foreach ($k in (Get-DoJson @('compute','ssh-key','list') | Where-Object { $_.name -eq $Cfg.SshKeyName })) {
    Invoke-Doctl compute ssh-key delete ([string]$k.id) --force | Out-Null; Write-Host "deleted DO ssh key $($k.name)" }
$ns = (Invoke-RestMethod -Uri 'https://api.digitalocean.com/v2/functions/namespaces' -Headers $h).namespaces |
    Where-Object { $_.label -eq 'sando-guard' }
foreach ($n in @($ns)) {
    Invoke-RestMethod -Method Delete -Uri "https://api.digitalocean.com/v2/functions/namespaces/$($n.namespace)" -Headers $h | Out-Null
    Write-Host "deleted Functions namespace $($n.label) ($($n.namespace)) incl. function + trigger" }
foreach ($f in @($Cfg.TokenFile, $Cfg.SshKeyPath, "$($Cfg.SshKeyPath).pub", (Join-Path $PSScriptRoot '.known_hosts'), (Join-Path $PSScriptRoot '.current_ip'))) {
    if (Test-Path -LiteralPath $f) { Remove-Item -LiteralPath $f -Force; Write-Host "deleted local $f" } }
Write-Host 'cleanup done'
