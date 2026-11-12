#!/usr/bin/env python3
"""Metrics of the recorded video mission, computed from its own bag with the authors' analysis code.
Usage: python3 video_metrics.py <bag_dir> <out.json>"""
import json
import pathlib
import sys

sys.path.insert(0, "/root/sando_ws/src/sando/scripts")
import analyze_dynamic_benchmark as adb  # noqa: E402

bag = pathlib.Path(sys.argv[1])
m = adb.recompute_metrics_from_bag(bag, (105.0, 0.0, 2.0))
col = adb.analyze_collision_from_trajs_bag(bag, (0.1, 0.1, 0.1), trajs_topic="/trajs")
viol = adb.recompute_violations_from_bag(bag)
out = {
    "travel_time": m.get("travel_time"), "path_length": m.get("path_length"),
    "goal_reached": m.get("travel_time") is not None,
    "collision_count": int(col.get("collision_count", 0)),
    "min_distance": float(col.get("min_distance", float("nan"))),
    "obstacles_checked": int(col.get("unique_obstacles", 0)),
    "vel_violations": int(viol.get("vel_violation_count", 0)),
    "acc_violations": int(viol.get("acc_violation_count", 0)),
    "jerk_violations": int(viol.get("jerk_violation_count", 0)),
}
pathlib.Path(sys.argv[2]).write_text(json.dumps(out, indent=2))
print(json.dumps(out))
