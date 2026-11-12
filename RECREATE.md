# Recreate the SANDO demo video

Everything on DigitalOcean was deleted after the demo. This recreates it from scratch.
Typical cost is **≈ $0.25–0.50** in one session (worst case $0.50); nothing is left running afterwards.

## Requirements
- Windows with `doctl` and OpenSSH, `DIGITALOCEAN_TOKEN` set in the terminal.
- WSL Ubuntu is only needed for local solver tests (`solver/grb_compat/tests`) and report generation.
- No Gurobi licence: the build uses the open-source solver in `solver/grb_compat`.

## Steps (PowerShell, from the project folder)
```powershell
# 1. one-time setup (free): SSH key on DO + cloud guard (DO Function that deletes any
#    'sando-demo' droplet past its deadline, every 5 min, even if this PC is off)
powershell -ExecutionPolicy Bypass -File infra/setup-ssh.ps1
powershell -ExecutionPolicy Bypass -File infra/deploy-cloud-guard.ps1

# 2. one session: fresh c-8 droplet (lon1) -> build SANDO (~35 min) -> record, verify, render the video
#    -> download to demo/output/rebuild_<timestamp>/result.tgz -> droplet destroyed
powershell -ExecutionPolicy Bypass -File infra/session-full.ps1 -Case medium

# 3. delete everything again (droplets, snapshots, DO ssh key, cloud guard, local credentials)
powershell -ExecutionPolicy Bypass -File infra/cleanup-all.ps1
```

## Optional
- More missions with per-mission reports: run `infra/session-build.ps1` (build + snapshot), then
  `infra/session-missions.ps1 -Cases easy,medium,hard -Trials 3`. Reports go to `reports/missions/`.
  The snapshot costs ~$0.035/day until `cleanup-all.ps1` is run.
- `infra/status.ps1` shows running droplets, today's spend and whether the cloud guard is active.

## Budget safety (built in)
- `up.ps1` refuses any session whose worst case would push the day's project spend over **$0.90** (< 1 €).
  The worst case counts whole billed hours plus a 10-minute guard slack.
- One droplet at a time. Each droplet carries a deadline tag.
- Every session ends in `down.ps1`, which runs a teardown check.

## What the pieces are
| Path | Purpose |
|---|---|
| `solver/` | Open-source drop-in for the Gurobi API used by SANDO, plus the build patch (`apply_open_solver.py`) |
| `remote/build.sh` | Native build on Ubuntu 22.04, mirroring the upstream Dockerfile, without Gurobi |
| `remote/make_video.sh` | Records a mission, verifies it with the authors' analysis code, renders it with matplotlib |
| `remote/run_missions.sh`, `postprocess.py` | Paper benchmark missions + collision and violation checks |
| `reports/make_mission_report.py` | One Markdown report per mission |
| `infra/` | Budget-guarded DigitalOcean session scripts |
