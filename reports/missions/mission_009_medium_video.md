# Mission 9: dynamic medium (filmed for the investor video)

**Result: SUCCESS**

- Session: 2026-11-12 00:36 UTC (video session)
- Scenario: forest 100 m x 40 m, 100 obstacles (65% moving), start (0, 0, 2) -> goal (105, 0, 2)
- Planner: SANDO (MIT ACL) with open-source grb_compat solver (no Gurobi)
- Recorded with SANDO's `benchmark-record` mode (all visualisation topics); verified with the authors' analysis code
  (`analyze_collision_from_trajs_bag`, `recompute_violations_from_bag`, `recompute_metrics_from_bag`).

| Metric | This mission | Paper (Table VII, SANDO) |
|---|---|---|
| Goal reached | yes | 100% success |
| Collisions (checked from recording, all 100 obstacles) | 0 | 0 |
| Min. distance to an obstacle surface | 0.57 m | n/a |
| Travel time | 24.1 s | 21.7-24.6 s |
| Path length | 106.6 m | 105.5-106.7 m |
| Vel / acc / jerk violations | 0 / 0 / 0 | 0 / 0 / 0 |

## Notes

Video: `demo/output/SANDO_demo_medium_mission.mp4`. The recorder missed the planner's goal_reached signal and kept
recording until its timeout; the video is trimmed to the flight (goal within 1 m + 3 s).
