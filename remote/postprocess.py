#!/usr/bin/env python3
"""Post-process one mission run directory on the droplet (needs ROS 2 for rosbag2_py).

For every trial in the latest benchmark_*.json (the files are cumulative):
  * collision check from the bag with SANDO's own analyze_collision_from_bag()
    (drone half-extents 0.1 m, obstacle cubes 0.8 m - same as the authors' analysis)
  * vel/acc/jerk violations recomputed from the bag (authors' recompute_violations_from_bag, L-inf)
  * computation-time statistics from csv/num_<trial>.csv (successful replans only)
Writes <case_dir>/trials_augmented.json.
Usage: python3 postprocess.py <case_dir>      e.g. ~/missions/20261111_234037_easy/easy
"""
import csv
import glob
import json
import math
import os
import pathlib
import statistics
import sys

sys.path.insert(0, "/root/sando_ws/src/sando/scripts")
import analyze_dynamic_benchmark as adb  # noqa: E402  (authors' analysis module)


def comp_stats(csv_path):
    if not os.path.exists(csv_path):
        return {}
    tot, loc, cvx, glob_t = [], [], [], []
    n_all = 0
    with open(csv_path) as fh:
        for row in csv.DictReader(fh):
            n_all += 1
            if row.get("Result") != "1":
                continue
            tot.append(float(row["Total replanning time [ms]"]))
            loc.append(float(row["Local Traj Time [ms]"]))
            cvx.append(float(row["CVX Decomposition Time [ms]"]))
            glob_t.append(float(row["Global Planning Time [ms]"]))
    if not tot:
        return {"replans_attempted": n_all, "replans_succeeded": 0}
    p95 = lambda v: sorted(v)[max(0, int(math.ceil(0.95 * len(v))) - 1)]
    return {
        "replans_attempted": n_all,
        "replans_succeeded": len(tot),
        "replan_ms_mean": statistics.mean(tot), "replan_ms_p95": p95(tot), "replan_ms_max": max(tot),
        "local_opt_ms_mean": statistics.mean(loc), "local_opt_ms_p95": p95(loc),
        "stsfc_ms_mean": statistics.mean(cvx), "global_ms_mean": statistics.mean(glob_t),
    }


def main():
    case_dir = pathlib.Path(sys.argv[1])
    jsons = sorted(case_dir.glob("benchmark_*.json"), key=lambda p: (len(json.loads(p.read_text())), p.name))
    if not jsons:
        sys.exit(f"no benchmark json in {case_dir}")
    trials = json.loads(jsons[-1].read_text())
    for t in trials:
        tid = t["trial_id"]
        bag = case_dir / "bags" / f"trial_{tid}"
        t["collision_checked"] = False
        if bag.exists() and adb.HAS_ROSBAG:
            col = adb.analyze_collision_from_bag(bag, (0.1, 0.1, 0.1))
            t["collision_checked"] = True
            t["collision_count"] = int(col.get("collision_count", 0))
            t["collision"] = t["collision_count"] > 0
            t["min_distance_to_obstacles"] = float(col.get("min_distance", float("nan")))
            t["collision_free_ratio"] = float(col.get("collision_free_ratio", 1.0))
            viol = adb.recompute_violations_from_bag(bag)
            for k in ("vel_violation_count", "acc_violation_count", "jerk_violation_count"):
                if k in viol:
                    t[k] = int(viol[k])
            t["violation_samples"] = int(viol.get("vel_total", 0))
        t.update(comp_stats(str(case_dir / "csv" / f"num_{tid}.csv")))
    out = case_dir / "trials_augmented.json"
    out.write_text(json.dumps(trials, indent=2))
    print(f"wrote {out} ({len(trials)} trials)")
    for t in trials:
        print(f"  trial {t['trial_id']}: goal={t['goal_reached']} collisions={t.get('collision_count')} "
              f"min_dist={t.get('min_distance_to_obstacles')} replan_mean={t.get('replan_ms_mean')}")


if __name__ == "__main__":
    main()
