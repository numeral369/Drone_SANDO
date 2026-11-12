# One-time: create a dedicated SSH key and register it on DigitalOcean (free).
. (Join-Path $PSScriptRoot 'lib.ps1')
Initialize-DoToken
if (-not (Test-Path $Cfg.SshKeyPath)) {
    & ssh-keygen -t ed25519 -N '""' -C 'sando-demo' -f $Cfg.SshKeyPath | Out-Null
}
$exists = Get-DoJson @('compute','ssh-key','list') | Where-Object { $_.name -eq $Cfg.SshKeyName }
if (-not $exists) {
    Invoke-Doctl compute ssh-key import $Cfg.SshKeyName --public-key-file "$($Cfg.SshKeyPath).pub" | Out-Null
    Write-Host 'SSH key registered on DO.'
} else { Write-Host 'SSH key already registered.' }
