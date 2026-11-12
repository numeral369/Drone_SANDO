# Mission 2: dynamic easy, seed 1

**Result: SUCCESS**

- Session: 2026-11-11 23:40 UTC (session 1)
- Scenario: forest 100 m x 40 m, 50 obstacles (65% moving), start (0.0, 0.0, 2.0) -> goal (105.0, 0.0, 2.0)
- Planner: SANDO (MIT ACL) with open-source grb_compat solver (no Gurobi)

| Metric | This mission | Paper (Table VII, SANDO) |
|---|---|---|
| Goal reached | yes | 100% success |
| Collisions (checked from recording) | 0 | 0 |
| Min. distance to an obstacle surface | 0.41 m | n/a |
| Travel time | 24.4 s | 21.8-26.2 s |
| Path length | 106.0 m | 104.8-106.3 m |
| Avg / max speed | 4.35 / 4.84 m/s | per-axis limit 5 m/s (3D speed may exceed 5) |
| Max acceleration | 9.32 m/s^2 | per-axis limit 20 m/s^2 |
| Replans succeeded / attempted | 1110 / 1616 | - |
| Replanning time mean / p95 / max | 20.4 / 29.5 / 114.9 ms | ~20-35 ms (hardware) |
| Local trajectory optimisation mean / p95 | 17.2 / 25.9 ms | 0.6-30 ms (Table VII, Gurobi) |
| Corridor (STSFC) generation mean | 4.9 ms | 6-15 ms (Table IX) |
| Vel / acc / jerk violations | 0 / 0 / 0 | 0 / 0 / 0 |
| Corridor (SFC) violations | 0 | 0 |

## Notes

Collision check from the recording was run in session 2 with the authors analysis code.

