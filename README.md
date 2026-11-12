# Autonomous UAV demo

This simulation shows how a UAV can autonomously travel in a dynamic-obstacle simulation environment using pure Mathematics principal, instead of Machine Learning. Based on *SANDO: Safe Autonomous Trajectory Planning for Dynamic Unknown Environments* (Kondo, Tordesillas & How, MIT ACL, [arXiv:2604.07599](https://arxiv.org/abs/2604.07599)). It runs on DigitalOcean and produces a demo video.

## Result
- **Video:** `demo/output/SANDO_demo_medium_mission.mp4` (1080p, 39 s). It shows a simulated flight through 100 obstacles, 65% of them moving.
- **Missions:** 9/9 succeeded on Easy, Medium and Hard. In every mission:
  - collisions: 0;
  - speed, acceleration and jerk limit violations: 0;
  - travel time: close to the paper's.

  There is one report per mission in `reports/missions/`.
- **Cost:** about $1.10 in total, under 1 €/day.

## How it works
- **Planner:** the authors' open-source code ([mit-acl/sando](https://github.com/mit-acl/sando)). It is built on a temporary Ubuntu 22.04 droplet.
- **No Gurobi:** `solver/grb_compat` is an open-source replacement for the Gurobi API that SANDO uses. It solves exactly, with a branch-and-bound search and an exact QP solver, and is unit-tested against brute-force results.
- **Video:** rendered with matplotlib from the recorded flight. Each video's metrics are checked with the authors' own analysis code.
- **Budget safety:**
  - a daily spend cap;
  - one droplet at a time, each with a deadline;
  - a cloud guard that deletes droplets past their deadline;
  - a teardown check after every session.

## Recreate
All cloud resources were deleted after the demo. See [`RECREATE.md`](RECREATE.md) for the three commands that rebuild the video (about $0.50).

## Layout
| Path | Contents |
|---|---|
| `solver/` | Open-source Gurobi replacement + build patch |
| `remote/` | Scripts run on the droplet: build, missions, video |
| `infra/` | DigitalOcean session scripts with budget guard |
| `reports/` | Mission report generator and reports |
| `DEMO_SANDO_DONE.md` | Development/test plan and progress log |

The planner is credit of MIT Aerospace Controls Lab. This is a simulation, not a hardware flight.
