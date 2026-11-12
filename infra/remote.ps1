# SSH/SCP helpers for the current sando droplet. Dot-source after lib.ps1.
$Script:KnownHosts = Join-Path $PSScriptRoot '.known_hosts'
function Get-RemoteIp {
    $f = Join-Path $PSScriptRoot '.current_ip'
    if (-not (Test-Path $f)) { throw 'No current droplet IP (run up.ps1).' }
    return (Get-Content $f -Raw).Trim()
}
function Get-SshArgs {
    return @('-i', $Cfg.SshKeyPath, '-o', "UserKnownHostsFile=$KnownHosts", '-o', 'StrictHostKeyChecking=accept-new',
             '-o', 'ConnectTimeout=15', '-o', 'ServerAliveInterval=30', '-o', 'BatchMode=yes')
}
function Invoke-Remote { param([string]$Command, [switch]$AllowFail)
    $ErrorActionPreference = "Continue"   # native stderr must not be fatal (PS 5.1)
    $ip = Get-RemoteIp
    $a = (Get-SshArgs) + @("root@$ip", $Command)
    $out = & ssh @a 2>&1
    if ($LASTEXITCODE -ne 0 -and -not $AllowFail) { throw "remote command failed ($LASTEXITCODE): $Command`n$out" }
    return ($out | Out-String)
}
function Copy-ToRemote { param([string]$Local, [string]$Remote)
    $ErrorActionPreference = "Continue"
    $ip = Get-RemoteIp
    $a = (Get-SshArgs) + @('-r', $Local, "root@${ip}:$Remote")
    & scp @a 2>&1 | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "scp to remote failed: $Local" }
}
function Copy-FromRemote { param([string]$Remote, [string]$Local)
    $ErrorActionPreference = "Continue"
    $ip = Get-RemoteIp
    $a = (Get-SshArgs) + @('-r', "root@${ip}:$Remote", $Local)
    & scp @a 2>&1 | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "scp from remote failed: $Remote" }
}
function Wait-Ssh { param([int]$TimeoutSec = 300)
    $end = (Get-Date).AddSeconds($TimeoutSec)
    while ((Get-Date) -lt $end) {
        $r = Invoke-Remote 'echo ok' -AllowFail
        if ($r -match 'ok') { return }
        Start-Sleep -Seconds 10
    }
    throw 'SSH did not become ready'
}
