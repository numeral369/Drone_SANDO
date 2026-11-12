# SANDO demo - DigitalOcean configuration (dot-sourced by the other scripts)
$Script:Cfg = [ordered]@{
    Region          = 'lon1'                 # only nearby region offering c-8 (checked 2026-11-11)
    Image           = 'ubuntu-22-04-x64'
    Tag             = 'sando-demo'
    SshKeyName      = 'sando-demo-key'
    SshKeyPath      = (Join-Path $env:USERPROFILE '.ssh\sando_do')
    # Hard daily cap in USD for THIS project. 0.90 USD < 1 EUR for any EUR/USD rate above 0.90
    # (EUR/USD has been above 0.95 for decades). The user's limit is 1 EUR/day.
    DailyCapUsd     = 0.90
    # Conservative billing assumption: every droplet is billed for whole started hours, minimum 1 h.
    MinBilledHours  = 1
    MaxSessionMin   = 180                    # absolute upper bound for any single session
    SnapshotUsdPerGbMonth = 0.06
    InfraDir        = $PSScriptRoot
    LedgerPath      = (Join-Path $PSScriptRoot 'ledger.csv')
    TokenFile       = (Join-Path $PSScriptRoot '.do_token.dpapi')   # DPAPI-encrypted, only this Windows user can decrypt
}
