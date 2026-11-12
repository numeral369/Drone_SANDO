# Mission 6: dynamic hard, seed 0

**Result: SUCCESS**

- Session: 2026-11-12 00:06 UTC (session 2)
- Scenario: forest 100 m x 40 m, 200 obstacles (65% moving), start (0.0, 0.0, 2.0) -> goal (105.0, 0.0, 2.0)
- Planner: SANDO (MIT ACL) with open-source grb_compat solver (no Gurobi)

| Metric | This mission | Paper (Table VII, SANDO) |
|---|---|---|
| Goal reached | yes | 100% success |
| Collisions (checked from recording) | 0 | 0 |
| Min. distance to an obstacle surface | 0.23 m | n/a |
| Travel time | 25.2 s | 22.7-26.8 s |
| Path length | 106.7 m | 108.5-111.3 m |
| Avg / max speed | 4.24 / 5.66 m/s | per-axis limit 5 m/s (3D speed may exceed 5) |
| Max acceleration | 13.40 m/s^2 | per-axis limit 20 m/s^2 |
| Replans succeeded / attempted | 364 / 870 | - |
| Replanning time mean / p95 / max | 63.4 / 99.3 / 291.5 ms | ~20-35 ms (hardware) |
| Local trajectory optimisation mean / p95 | 51.2 / 80.7 ms | 0.6-30 ms (Table VII, Gurobi) |
| Corridor (STSFC) generation mean | 17.7 ms | 6-15 ms (Table IX) |
| Vel / acc / jerk violations | 0 / 0 / 0 | 0 / 0 / 0 |
| Corridor (SFC) violations | 0 | 0 |

## Notes

Collision-checked from the recording with the authors analysis code (drone 0.2 m box, 0.8 m obstacle cubes).

