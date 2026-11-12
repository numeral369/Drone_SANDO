# SANDO Simulation Replication on DigitalOcean: Development & Test Plan

**Source:** Kondo, Tordesillas, How, *SANDO: Safe Autonomous Trajectory Planning for Dynamic Unknown Environments*, arXiv:2604.07599v1 (local file `2604.07599v1_SANDO.pdf`)
**Code:** https://github.com/mit-acl/sando (official, public)
**Status:** Plan only. Nothing coded, nothing created on DigitalOcean yet.
**Date:** 2026-11-11 (rev. 2: open-source solver instead of Gurobi; technical RViz view only; MIT credited, no company logo)

---

## 1. Goal and definition of done

1. Run the SANDO simulation described in the paper on DigitalOcean (DO) infrastructure.
2. The drone completes the **mission**: flies from start `(0, 0, 2)` to goal `(105, 0, 2)` through the 100 m × 40 m forest with static pillars and moving trefoil-knot cubes (paper Sec. IX-D), with **no collision** and **no velocity, acceleration or jerk limit violations**.
3. Produce an **investor-ready video** (MP4, 1080p) of a successful mission.
4. Spend **≤ 1 € per day** on DO and leave **no billable resources running** after each test session.

The project is done when all of these are true:

| # | Criterion | How it is verified |
|---|-----------|--------------------|
| D1 | `dynamic` scenario (ground-truth obstacles), Medium difficulty: ≥ 3 consecutive runs reach the goal | Benchmark CSV/JSON in `benchmark_data/`: success = true, collisions = 0 |
| D2 | Constraint violations: vel/acc/jerk = 0 % | Same CSV/JSON (paper Table VII reports 0 % for SANDO) |
| D3 | Recorded MP4 of one successful run, with title and caption cards | File exists locally, plays from start to finish, shows drone reaching goal |
| D4 | No DO droplets/volumes left running, daily spend ≤ 1 € | `doctl` teardown check (T-TEARDOWN) + cost ledger (Section 6) |
| D5 *(stretch, not filmed)* | `unknown_dynamic` (perception only, Gazebo + depth camera) Easy: ≥ 1 successful run | Same as D1 |
| D6 | **No proprietary software.** Gurobi is replaced by an open-source solver that gives the same optimal trajectories | T3/T3b equivalence tests (Section 7) |

---

## 2. What the paper's simulation actually requires

| Item | Paper / repo | Implication for us |
|------|-------------|--------------------|
| OS / middleware | Ubuntu 22.04, ROS 2 Humble | DO image `ubuntu-22-04-x64` |
| Simulator | Gazebo (static forest, unknown_dynamic) or RViz-only (interactive, dynamic) | RViz-only `dynamic` mode avoids heavy Gazebo rendering, so it is the main target |
| Solver | **Gurobi** MIQP (C++ API), all in one class `SolverGurobi` (`src/sando/gurobi_solver.cpp`, ~5,500 lines; also referenced from `sando.cpp`, `sando.hpp`, `local_traj_benchmark.cpp`, `temporal_layered_corridor_test_node.cpp`) | **Replaced by an open-source backend** (Section 2b) |
| Sensors (sim) | Livox MID-360 (static), RealSense D435 (unknown_dynamic) | Built by `setup.sh` (Livox-SDK2, livox_ros_driver2) |
| Compute in paper | i9 ×16, 64 GB RAM | We use 8 vCPU. Runs will be slower but should still be real-time capable: the paper reports 3–30 ms per optimization |
| GPU | Optional, Gazebo rendering only | No GPU droplet (≥ $0.76/h would break the budget). Use software rendering (Mesa llvmpipe) |
| Build RAM | ~8 GB for `BUILD_JOBS=2`, 16 GB for `BUILD_JOBS=4` | Use a 16 GB droplet for the build |
| Launch | `python3 src/sando/scripts/run_sim.py -m <mode> -d <easy/medium/hard> -s install/setup.bash` | Scripted through tmuxp, so it can run over SSH |
| Outputs | `benchmark_data/`: per-trial CSV, JSON and **ROS bag** | The bag lets us re-render the video offline (Section 5) |

**Parameters to keep (paper Tables II/III, dynamic with ground truth):** N = 5 pieces, M = 2 or 3 polytopes per layer, r_drone = 0.1 m, v_obs_max = 0.5 m/s, w_heat = 5.0, γ = 0, margin = 0.1 m, max time factor = 2.5, v_max = 5 m/s, a_max = 20 m/s², j_max = 100 m/s³. We use the repo defaults and only diff them against these values. We do not tune.

---

## 2b. Open-source replacement for Gurobi

**What Gurobi does in SANDO** (from reading `gurobi_solver.cpp`):
- continuous variables: Bézier/cubic coefficients (only N − 3 free per axis after variable elimination);
- **binary** variables `s[i][j]`: piece *i* is assigned to polytope *j*, with Σⱼ s ≥ 1 per piece;
- **indicator constraints** (`addGenConstrIndicator`): if s = 1, the piece's 4 control points lie inside polytope *j*;
- linear per-axis (L∞) velocity, acceleration and jerk bounds on control points. This is what the paper uses. An optional L2 mode uses quadratic constraints (`addQConstr`) and is not needed;
- a quadratic objective (squared jerk).

**Key observation:** the problem is very small. With N = 5 pieces and M = 2 polytopes per layer there are only M^N = **32** possible piece-to-polytope assignments (243 for M = 3). Picking more than one polytope for a piece only adds constraints and can never improve the cost. So the MIQP optimum equals the best of the M^N convex QPs, one per assignment. **Enumerating them gives exactly the same optimal trajectory as Gurobi**, not an approximation.

**Candidates evaluated:**

| Option | License | Fit |
|--------|---------|-----|
| **A. Enumerate assignments + ProxQP** (part of `proxsuite`) | BSD-2 | **Recommended.** Exact same optimum. Each QP has only 6–30 variables, so expect sub-ms solves. The 32 QPs run in parallel threads, like SANDO's existing multi-thread design. Assignments infeasible by time layer are pruned early. ProxQP is a high-accuracy solver, so it keeps the hard constraints tight. |
| B. **SCIP** (MIQP with indicator constraints) | Apache 2.0 | Most literal 1:1 port of the Gurobi model (same concepts: binaries, indicators, quadratic objective). Slower than Gurobi, so higher replan times are possible. **Used as an independent cross-check** of option A in the tests. |
| C. HiGHS | MIT | Not suitable: it handles MILP and continuous QP, but not mixed-integer QP. |
| D. OSQP | Apache 2.0 | First-order solver with tolerance ~1e-3, so constraints can be slightly violated. Not ideal for a "hard constraint" planner. Kept as a fallback for option A. |
| E. CBC / GLPK | EPL / GPL | MILP only. Not suitable. |

**Plan:**
- Add `SolverOpen` with **the same public interface** as `SolverGurobi` (`setX0`, `setXf`, `setPolytopesTimeLayered`, `generateNewTrajectory`, ...).
- Reuse SANDO's own problem-building math (control-point expressions, variable elimination formulas for N = 4..7, time allocation factors) through a small expression layer instead of `GRBLinExpr`.
- Select the solver with a CMake option `SANDO_SOLVER=open|gurobi` (default `open`) and remove the hard Gurobi dependency from `CMakeLists.txt` and `setup.sh`.
- Keep the repo's existing post-checks `checkDynamicViolation` and `checkCollisionViolation` on every solution, as a safety net against numerical tolerance.
- All changes go in a fork/branch, credited to the original authors (repo license is respected).
- The video and report will say: "SANDO planner (MIT ACL) running with an open-source solver backend".

---

## 3. Risks and decisions

| ID | Risk | Mitigation / decision |
|----|------|------------------------|
| **R1** | **Gurobi license.** Commercial use would need a paid license or a time-limited evaluation license. | **Decided: replace Gurobi with open-source solvers** (Section 2b). New risks: R1a and R1b. |
| R1a | The port introduces a bug, so trajectories differ from the original planner | Offline equivalence tests: enumeration vs SCIP on hundreds of logged problems (T3b); the existing violation checkers run on every solution. |
| R1b | The open-source path is slower and replanning misses its time budget | Parallel enumeration, pruning, warm starts. Compare per-optimization time to the paper's 3–31 ms (T6). If needed, use M = 2 (32 QPs). |
| R2 | No GPU, so Gazebo is slow under software rendering | Main demo uses RViz-only `dynamic` mode. Gazebo (`unknown_dynamic`) is a stretch goal. If live capture is choppy, re-render from the ROS bag (Section 5). |
| R3 | 8 vCPU vs paper's 16-thread i9: slower replanning, possibly lower success | Use `c-8` (dedicated CPU) for runs. Fall back to Easy/Medium difficulty. Compare timing vs Table IX. |
| R4 | Build failure (`killed cc1plus`, OOM) | 16 GB droplet, `./setup.sh -j 4`, fall back to `-j 2`. |
| R5 | Cost overrun from a forgotten droplet | Every droplet tagged `sando-demo`, a local auto-delete timer, a teardown check after every session (Section 6). |
| R6 | A DO account charge exists that this project did not cause | `doctl balance get` showed $9.68 month-to-date usage before this project started. **You confirmed it is expected.** Our ledger only counts our own resources. |
| R7 | Docker Desktop (Windows) is not supported for display passthrough | Everything runs natively on the Linux droplet. Windows is only used as the `doctl`/SSH control machine. |

---

## 4. Development plan (phases)

All DO work is driven from this Windows machine with `doctl` and OpenSSH. All scripts live in this folder under `infra/` and `demo/` (to be written in later phases, not now).

### Phase 0: Local preparation (cost: 0 €)
- 0.1 Verify `doctl auth` works with `DIGITALOCEAN_TOKEN` (read-only calls: `account get`, `balance get`).
- 0.2 Create a dedicated SSH key pair (`~/.ssh/sando_do`) and register it with `doctl compute ssh-key import`. SSH keys cost nothing.
- 0.3 Region **`lon1` (London)**. Checked 2026-11-11: `c-8` is **not offered** in `fra1` or `ams3`, while `lon1` offers all planned sizes (`s-2vcpu-4gb`, `s-8vcpu-16gb`, `c-8`). Snapshots are regional, so all sessions use `lon1`.
- 0.4 Write `infra/up.ps1`, `infra/down.ps1` and `infra/status.ps1` wrappers:
  - `up.ps1 -Size <slug> -MaxMinutes <n>` creates the droplet (tag `sando-demo`, cloud-init user-data), records the start time in `infra/ledger.csv`, and starts a **local background timer that runs `down.ps1` after MaxMinutes**, no matter what.
  - `down.ps1` deletes all droplets with tag `sando-demo` and any volumes/reserved IPs created by them, records the stop time and cost in the ledger, and runs the teardown check.
  - `status.ps1` lists tagged resources, today's spend from the ledger, and the remaining daily budget.
- 0.5 Write `infra/cloud-init.yaml`: apt update, install tmux/xvfb/ffmpeg/mesa, create a swapfile. Keep it minimal.
- 0.6 Fork the SANDO repo (or keep a local patch set in `patches/`) for the solver work.

### Phase 0b: Open-source solver backend (mostly local work, cost ≈ 0 €)
- 0b.1 Add a ROS-free standalone C++ library `sando_open_solver`: assignment enumeration + ProxQP, SCIP model for cross-checks, and a problem logger/loader (JSON) so problems can be replayed offline.
- 0b.2 Port the problem construction from `gurobi_solver.cpp` into `SolverOpen` with the same public interface. Wire it in through `SANDO_SOLVER=open`.
- 0b.3 Unit tests (T3a) run on a cheap droplet (`s-2vcpu-4gb`, ~$0.036/h, ≤ 30 min) or locally if a Linux toolchain is available. The library has no ROS dependency.
- 0b.4 Patch `setup.sh`/`CMakeLists.txt`: drop the Gurobi install, add `proxsuite` (from source or conda/pip) and `scipopt` (apt or source).

### Phase 1: Build a reusable image (one session, ~1.5 h, ≈ $0.38)
- 1.1 `up.ps1 -Size c-8 -MaxMinutes 120` ($0.25/h). Same size as the run droplet, so the snapshot can be restored onto it.
- 1.2 Over SSH: clone our patched SANDO into `~/code/sando_ws/src`, then `./setup.sh -j 4` (installs ROS 2 Humble, Livox, ProxQP, SCIP, builds SANDO with `SANDO_SOLVER=open`). Run it inside `tmux` so a dropped SSH session does not kill the build.
- 1.3 No license file needed.
- 1.4 Run T2 and T3 (build and solver checks) **before** snapshotting.
- 1.5 Power off, then `doctl compute droplet-action snapshot` → snapshot `sando-built-v1`.
- 1.6 `down.ps1` (destroy the droplet).
- Snapshot storage: ~$0.06/GB-month × ~20 GB ≈ **$1.2/month ≈ 0.04 €/day**. That is far cheaper than rebuilding every session. The snapshot is deleted at project end.

### Phase 2: Bring-up & smoke tests (one session, ~45 min, ≈ $0.19)
- 2.1 `up.ps1 -Size c-8 -Image sando-built-v1 -MaxMinutes 60` ($0.25/h, dedicated CPUs for stable timing).
- 2.2 Start a virtual display: `Xvfb :99 -screen 0 1920x1080x24`, `DISPLAY=:99`, `LIBGL_ALWAYS_SOFTWARE=1`.
- 2.3 Run T4 (hover test) and T5 (`static` Easy). Pull `benchmark_data/` back with `scp`.
- 2.4 `down.ps1`.

### Phase 3: Mission runs, the core replication (1–2 sessions, ~1 h each, ≈ $0.25 each)
- 3.1 `dynamic` mode (ground-truth obstacles, RViz-only): Easy → Medium → Hard, 3 trials each, record ROS bags.
- 3.2 Compare success rate, travel time, path length and per-optimization time to paper Table VII (Section 7).
- 3.3 If success < 100 % on Medium, diagnose from logs/bag (compute time vs replan budget, solver failures, open-solver bugs). Fixes are limited to the solver port, compute size or build/runtime settings. Planner parameters are not changed away from the paper values.
- 3.4 *(Stretch)* `unknown_dynamic` Easy (Gazebo + depth camera, perception only). Expect slower runs under software rendering. Paper reference: 93–95 % success (Table IX).
- 3.5 `down.ps1` after every session.

### Phase 4: Investor video (1 session, ~1 h, ≈ $0.25)
See Section 5. Produce `demo/output/sando_mission_<date>.mp4`, then `down.ps1`.

### Phase 5: Cleanup
- Delete snapshot `sando-built-v1` (after you confirm the video is final), the SSH key on DO (optional), and any leftover tagged resources.
- Final teardown check plus a ledger summary.

---

## 5. Recording the mission for investors

**Approach: record the data during the run, render the video afterwards.**
Capturing the screen during the live run would compete with the planner for CPU on a GPU-less droplet. That could hurt planning and make the video choppy. Instead:

1. **During the mission run:** the repo already records a ROS bag per trial (`benchmark_data/`). Also make sure the visualization topics are included (trajectory, STSFC polytopes, obstacle boxes and predictions, drone pose).
2. **Render pass (same or later session, no real-time pressure):**
   - Replay the bag of the best successful run with `ros2 bag play --rate 0.5` (or slower if needed) into RViz on `Xvfb :99` at 1920×1080.
   - Capture with `ffmpeg -f x11grab -video_size 1920x1080 -framerate 30 -i :99 ...`, then speed the footage back up to real time (`setpts`) so the playback looks smooth and correct.
   - **Technical view only** (your choice): RViz with the drone as a marker/point, static pillars, moving obstacle boxes with their predicted paths, the planned trajectory colored by speed, and the safe corridors in translucent green (as in paper Figs. 8 and 10). No Gazebo, no photorealistic rendering.
   - Fixed, saved RViz config (`demo/sando_demo.rviz`): a camera following the drone from a 3/4 top view, dark background.
   - Optional second angle: top-down overview of the whole 100 m forest, combined picture-in-picture with ffmpeg `overlay`.
3. **Polish with ffmpeg (no paid tools):**
   - Opening title card: "SANDO: autonomous drone navigation among moving obstacles".
   - Lower-third captions at key moments: "Moving obstacle detected → replanned in X ms", "Safe corridor (green) guarantees no collision".
   - Ending card with the run's real metrics: travel time, path length, 0 collisions, 0 constraint violations, number of obstacles.
   - Output H.264 MP4, 1080p30, < 100 MB, so it can go in an email or deck.
4. **Download** the MP4 and raw bag to `demo/output/` with `scp`, then tear down.
5. **Credits and honesty:**
   - The title and end cards credit **"SANDO, Kondo, Tordesillas & How, MIT Aerospace Controls Lab (arXiv:2604.07599)"**.
   - **No company logo** (your choice).
   - The video is labelled as a **simulation** of the authors' planner, running with an open-source solver backend. It is not a hardware flight and not our own algorithm, and the captions must not suggest otherwise.

---

## 6. Cost control (hard rules)

All in region `lon1`, image `ubuntu-22-04-x64`, tag `sando-demo`.

| Resource | vCPU | RAM | Disk | Type | Hourly | Use |
|----------|------|-----|------|------|--------|-----|
| `s-2vcpu-4gb` | 2 shared | 4 GB | 80 GB SSD | Basic | $0.036 | Solver unit tests (Phase 0b), provisioning test T1 |
| `c-8` | 8 dedicated | 16 GB | 100 GB SSD | CPU-Optimized | $0.250 | One-time build, mission runs and video rendering |
| `s-4vcpu-8gb` | 4 shared | 8 GB | 160 GB SSD | Basic | $0.071 | Fallback for rendering only, if fast enough |
| Snapshot `sando-built-v1` (~20 GB est.) | | | | | ≈ $0.04/day ($0.06/GB-month) | Kept between sessions; deleted at project end |

Why the build runs on `c-8`: a snapshot can only be restored onto a droplet whose disk is ≥ the source droplet's disk. A build on `s-8vcpu-16gb` (320 GB) could not be restored onto `c-8` (100 GB). Building on `c-8` also gives 16 GB RAM for compiling.

Bandwidth: each droplet includes 4–6 TB of outbound transfer. We only download results and videos (< 1 GB), so there is no bandwidth cost.

**Daily budget:** ≤ 1 € ≈ **$1.05**. Planning cap is **$0.90/day**, which leaves a buffer for exchange rate and rounding. In practice that is **at most ~3.5 h of `c-8` per day**, and the plan needs ~1 h per session.

**Rules:**
1. A droplet exists only during an active test session. No idle droplets overnight. Note that a *powered-off* droplet is **still billed**, so droplets are always destroyed, never just shut down.
2. Every resource is tagged `sando-demo`. Teardown deletes by tag.
3. Every `up.ps1` starts a local auto-delete timer (`-MaxMinutes`, default 60, max 120).
4. **T-TEARDOWN runs after every session** (below). A session is not finished until it passes.
5. `infra/ledger.csv` logs each resource: created/destroyed timestamps, size and cost. Before any `up.ps1`, the script refuses to start if today's spend + planned session cost > $0.90.
6. No GPU droplets, no Kubernetes, no managed DBs, no load balancers, no paid Spaces/registry.
7. Nothing is sent to external services except DO, GitHub (clone) and Gurobi (license activation, if chosen).

**Estimated total project cost:** solver-port test sessions ≈ $0.15 + build ≈ $0.38 + 4–5 run/render sessions × ≈ $0.25 + snapshot ≈ $0.04/day ⇒ **≈ $1.5–2.5 total, spread over ~9 days, each day < 1 €.**

---

## 7. Test plan

Each test lists its purpose, steps, pass criteria and estimated DO cost. Tests run in order. A failure stops the chain until it is diagnosed.

### T0: Local preflight (0 €)
- **Steps:** `doctl account get`; `doctl balance get`; `doctl compute droplet list --tag-name sando-demo` (should be empty); `ssh -V`.
- **Pass:** auth OK, no tagged leftovers, OpenSSH present.

### T1: Provisioning smoke test (≈ $0.01)
- **Steps:** `up.ps1 -Size s-4vcpu-8gb -MaxMinutes 10`; SSH in; check cloud-init finished (`cloud-init status --wait`); `down.ps1`.
- **Pass:** droplet reachable over SSH in < 3 min; teardown leaves 0 tagged resources; the ledger entry is correct; the auto-delete timer fires if `down.ps1` is skipped (also test that path once).

### T2: Build verification (inside Phase 1)
- **Steps:** after `setup.sh`: `source install/setup.bash`; `ros2 pkg list | grep sando`; `run_sim.py --mode hover-test --dry-run`.
- **Pass:** build exits 0, SANDO packages listed, dry-run prints valid tmuxp YAML.

### T3a: Open-solver unit tests (Phase 0b, cheap droplet or local)
- **Steps:**
  - hand-made cases with known answers: one polytope (pure QP); two overlapping boxes where the optimum must switch polytope; an infeasible case that must report "infeasible";
  - every N in {4, 5, 6, 7} (all variable-elimination variants);
  - start/end states, continuity and per-axis vel/acc/jerk bounds checked on the returned trajectory.
- **Pass:** all cases correct; constraint violation < 1e-6; feasibility status correct.

### T3b: Equivalence test vs an independent MIQP solver
- **Steps:** run a 60 s `dynamic` Easy session with the problem logger on, which saves ≥ 300 real STSFC problems. Solve each offline with (a) enumeration + ProxQP and (b) SCIP MIQP.
- **Pass:** same feasibility verdict on 100 % of problems; optimal cost within 1e-4 relative; same polytope assignment (or equal cost on ties).
- **Report:** solve-time distribution for both. Target: enumeration median ≤ the paper's per-optimization times (3–9 ms for N=5, Table IX).

### T3: Solver smoke check in the built image (inside Phase 1)
- **Steps:** a short `interactive`/`dynamic` run for 30 s, then grep the logs for solver failures/exceptions.
- **Pass:** the planner publishes trajectories continuously; no solver crashes; the violation checkers report 0.

### T4: Hover / adversarial smoke test (≈ part of Phase 2 session)
- **Steps:** `run_sim.py --mode hover-test`, then `--mode adversarial-test`, headless on Xvfb.
- **Pass:** nodes start without crashing, the planner publishes trajectories, replan time is logged and is < 100 ms.

### T5: Static forest Easy (paper Sec. IX-C)
- **Steps:** `run_sim.py -m static -d easy`, 1 trial.
- **Pass:** goal reached, 0 collisions. Travel time is in the same order as the paper (~22–27 s, Table VI). Up to 2× is OK given weaker hardware and software rendering.

### T6: Dynamic with ground truth, the core mission (paper Sec. IX-D, Table VII)
- **Steps:** `run_sim.py -m dynamic -d {easy,medium,hard}`, 3 trials each, M = 2 and/or 3 (repo default first).
- **Pass (D1/D2):**
  - Easy and Medium: 3/3 successes; Hard: ≥ 2/3 (paper: 100 %).
  - vel/acc/jerk violations = 0 %.
  - Travel time within ±30 % of paper (≈ 21.7–22.8 s), path length ≈ 105–110 m.
  - Per-optimization time recorded (paper: 3–31 ms). Higher values are acceptable if success holds.
- **Outputs kept:** CSV/JSON summary + ROS bag of each successful run → `results/T6/`.

### T7 *(stretch)*: Unknown dynamic, perception only (paper Sec. IX-F, Table IX)
- **Steps:** `run_sim.py -m unknown_dynamic -d easy`, 2–3 trials.
- **Pass:** ≥ 1 success (paper: 93–95 %), 0 % constraint violations.

### T8: Recording pipeline (Phase 4)
- **Steps:** replay the best T6 bag into RViz on Xvfb, capture with ffmpeg, add cards/captions, download.
- **Pass (D3):** MP4 plays locally from start to finish; the drone is visibly avoiding moving obstacles and reaches the goal; on-screen metrics match the run's CSV; no rendering artefacts or frozen frames > 1 s; length 30–90 s.

### T9: Results comparison report
- **Steps:** table "ours vs paper" for T5, T6 and T7 (success, travel time, path length, opt time, violations) in `results/REPORT.md`.
- **Pass:** every deviation > 30 % has a stated likely cause (e.g., 8 vs 16 cores).

### T-TEARDOWN: after **every** session (mandatory)
- **Steps:** `down.ps1`, then:
  - `doctl compute droplet list --tag-name sando-demo` → empty
  - `doctl compute volume list` → no sando volumes
  - `doctl compute reserved-ip list` → none unassigned from us
  - `doctl compute snapshot list` → only `sando-built-v1` (intended)
  - ledger: today's total ≤ $0.90
- **Pass:** all of the above. If anything remains, delete it immediately and log the incident.

---

## 8. Session schedule (proposed)

| Day | Session | Tests | Est. DO cost |
|-----|---------|-------|--------------|
| 1 | Phase 0 + T0, T1 | T0, T1, T-TEARDOWN | ≈ $0.02 |
| 2–4 | Phase 0b: solver port and unit tests (a few short cheap-droplet sessions) | T3a, T-TEARDOWN | ≈ $0.05/day |
| 5 | Phase 1 build + T2, T3, snapshot | T2, T3, T-TEARDOWN | ≈ $0.38 |
| 6 | Phase 2 smoke + equivalence + start Phase 3 | T4, T3b, T5, T6-Easy, T-TEARDOWN | ≈ $0.30 |
| 7 | Phase 3 core mission | T6 Medium/Hard, (T7), T-TEARDOWN | ≈ $0.30 |
| 8 | Phase 4 video + report | T8, T9, T-TEARDOWN | ≈ $0.25 |
| 9 | Review video with you; cleanup | Phase 5, delete snapshot | ≈ $0.00 |

The solver port adds about 3 working days but only a few cents of cloud cost. The total stays at ≈ $1.5–2.5.

Each day stays well under 1 €. If something fails, the extra sessions move to the next day instead of increasing daily spend.

---

## 8b. Progress log

**2026-11-11**
- **Budget guard implemented** (`infra/`):
  - `up.ps1` refuses any session whose worst case would push today's project spend over **$0.90** (< 1 €). The worst case assumes whole billed hours, a 10-minute guard slack and snapshot storage.
  - Only one droplet may exist at a time. Each droplet carries a `sando-deadline-<unix>` tag.
  - `down.ps1` deletes the droplet and runs T-TEARDOWN.
  - Each droplet is also deleted at its deadline by a guard. Two variants are written but **not activated** because they need the user's approval:
    - `guard.ps1`: a local Windows scheduled task every 5 min (`register-guard.ps1`);
    - `cloud-guard/guard.py`: a DigitalOcean Function every 5 min that works even if the PC is off (`deploy-cloud-guard.ps1`).
- **T0 passed:** account active, 0 tagged droplets, OpenSSH present. SSH key `sando-demo-key` registered on DO (free). Spend today: $0.000.
- **Solver (Phase 0b) done and tested locally in WSL, cost 0 €.** Changed from the plan: third-party downloads were not permitted, so ProxQP/SCIP were replaced by our own dependency-free code:
  - `solver/grb_compat/include/gurobi_c++.h`: source-compatible subset of the Gurobi C++ API that SANDO uses, so `gurobi_solver.cpp` compiles **unchanged**;
  - `gi_qp.hpp`: exact Goldfarb–Idnani dual active-set QP solver (Eigen only);
  - branch-and-bound over piece→polytope choices. It is exact because indicator constraints only add restrictions;
  - exact elimination of equality constraints (null space) for the version without variable elimination.
- **T3a results (576/576 checks pass):**
  - QP vs active-set brute force: 400/400.
  - MIQP vs full enumeration: 150/150, using 1,677 vs 9,132 QPs.
  - Gurobi API semantics: lazy removal, callback abort, infeasible/ObjVal, quadratic constraints rejected.
  - SANDO-like 5-piece trajectory with 3 corridors per piece: equal to enumeration of all 243 assignments, 0.33 ms.
  - Also passes with SANDO's compile flags.
- **Build hazards found and fixed in `solver/apply_open_solver.py`:**
  - SANDO's `-ffast-math` + LTO broke infinity handling, so the solver library is compiled with `-fno-fast-math` and without LTO.
  - `-march=native` could crash a snapshot restored on another CPU, so it is replaced by `x86-64-v2`.
- **Ready to run:**
  - `remote/build.sh` (native build mirroring the upstream Dockerfile, no Gurobi);
  - `remote/run_missions.sh` (paper's dynamic benchmark harness);
  - `reports/make_mission_report.py` (one Markdown report per mission + `INDEX.md`, compared to paper Table VII);
  - `infra/session-build.ps1`.
- **Cloud guard deployed by the user.** T1 passed: a test droplet left running on purpose was deleted automatically 3.5 min after its deadline.
- **Bugs fixed during T1:** PS 5.1 JSON arrays; SSH stderr treated as fatal; pt-PT decimal comma in the ledger (read back as 357 USD).
- **Phase 1 build:** done in 35 min (T2 PASS: no Gurobi linked). Snapshot `sando-built-v1` is 17.7 GB.
- **Session 1:** missions 1–2 (Easy) reached the goal.

**2026-11-12**
- **Session 2:**
  - re-checked missions 1–2 from their recordings: 0 collisions;
  - missions 3–5 (Medium) and 6–8 (Hard): all succeeded.
  - **D1/D2 met: 8/8 missions succeeded**, each with goal reached, 0 collisions (from recordings, authors' analysis code) and 0 vel/acc/jerk violations.
  - Travel times 24.2–26.7 s and paths 105.6–108.0 m are close to Table VII.
  - The open-source solver is slower than Gurobi: replanning averages about 20 ms (Easy), 30 ms (Medium) and 60 ms (Hard).
- **Video, first attempt (RViz screen capture): rejected.** It included about 70 s of idle time after the goal (the recorder missed goal_reached), cluttered panels and a missing caption.
- **Video, final:** rendered with matplotlib from the recording (`remote/extract_bag.py` + `remote/render_mpl.py`). **D3 met:** `demo/output/SANDO_demo_medium_mission.mp4`, 1920x1080, 38.6 s.
- **Mission 9 (the filmed flight):** 106.6 m in 24.1 s, 0 collisions against all 100 obstacles, closest approach 0.57 m, 0 limit violations.
- **Cost:** day 1 = $0.571, day 2 = $0.535. Both are conservative whole-hour figures, under the $0.90 cap (< 1 €). D4 met.
- **Not done:** D5 (perception-only `unknown_dynamic`, Gazebo). It is a stretch goal and was not attempted.

## 9. Decisions log

| # | Question | Decision (2026-11-11) |
|---|----------|------------------------|
| 1 | Gurobi license | Replace with open-source: enumeration + ProxQP (main), SCIP (cross-check). See Section 2b. |
| 2 | Pre-existing $9.68 DO usage | Expected, from other projects. |
| 3 | Video style | Technical view only: drone as a point/marker, obstacles, trajectories, corridors (RViz). |
| 4 | Branding | Credit MIT ACL and the paper; **no company logo**. |
