# Mission 7: dynamic hard, seed 1

**Result: SUCCESS**

- Session: 2026-11-12 00:06 UTC (session 2)
- Scenario: forest 100 m x 40 m, 200 obstacles (65% moving), start (0.0, 0.0, 2.0) -> goal (105.0, 0.0, 2.0)
- Planner: SANDO (MIT ACL) with open-source grb_compat solver (no Gurobi)

| Metric | This mission | Paper (Table VII, SANDO) |
|---|---|---|
| Goal reached | yes | 100% success |
| Collisions (checked from recording) | 0 | 0 |
| Min. distance to an obstacle surface | 0.40 m | n/a |
| Travel time | 26.7 s | 22.7-26.8 s |
| Path length | 108.0 m | 108.5-111.3 m |
| Avg / max speed | 4.05 / 5.47 m/s | per-axis limit 5 m/s (3D speed may exceed 5) |
| Max acceleration | 12.94 m/s^2 | per-axis limit 20 m/s^2 |
| Replans succeeded / attempted | 400 / 900 | - |
| Replanning time mean / p95 / max | 61.2 / 96.3 / 292.2 ms | ~20-35 ms (hardware) |
| Local trajectory optimisation mean / p95 | 48.4 / 76.6 ms | 0.6-30 ms (Table VII, Gurobi) |
| Corridor (STSFC) generation mean | 16.4 ms | 6-15 ms (Table IX) |
| Vel / acc / jerk violations | 0 / 0 / 0 | 0 / 0 / 0 |
| Corridor (SFC) violations | 0 | 0 |

## Notes

Collision-checked from the recording with the authors analysis code (drone 0.2 m box, 0.8 m obstacle cubes).

