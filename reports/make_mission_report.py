#!/usr/bin/env python3
"""Summarise SANDO benchmark trials into one Markdown report per mission + an index.

Usage: python make_mission_report.py <benchmark_json> <case: easy|medium|hard> <out_dir> [--session S] [--notes "..."]
Input is the JSON list written by scripts/run_benchmark.py (one BenchmarkMetrics dict per trial).
Standard library only.
"""
import argparse
import json
import math
import pathlib
from datetime import datetime, timezone

# Paper reference: arXiv:2604.07599, Table VII (dynamic, ground-truth obstacles), SANDO rows.
PAPER = {
    "easy":   {"succ": 100.0, "trav": (21.8, 26.2), "path": (104.8, 106.3)},
    "medium": {"succ": 100.0, "trav": (21.7, 24.6), "path": (105.5, 106.7)},
    "hard":   {"succ": 100.0, "trav": (22.7, 26.8), "path": (108.5, 111.3)},
}
OBSTACLES = {"easy": 50, "medium": 100, "hard": 200}


def f(x, nd=2, unit=""):
    if x is None or (isinstance(x, float) and (math.isnan(x) or math.isinf(x))):
        return "n/a"
    return f"{x:.{nd}f}{unit}"


def verdict(t):
    ok = t.get("goal_reached") and not t.get("collision") and int(t.get("collision_count", 0)) == 0
    viol = sum(int(t.get(k, 0)) for k in ("vel_violation_count", "acc_violation_count", "jerk_violation_count"))
    if t.get("goal_reached") and not t.get("collision_checked", False):
        return "GOAL REACHED - collision check pending", viol
    if ok and viol == 0:
        return "SUCCESS", viol
    if ok:
        return "SUCCESS (with constraint violations)", viol
    if t.get("collision") or int(t.get("collision_count", 0)) > 0:
        return "FAILED - collision", viol
    if t.get("timeout_reached"):
        return "FAILED - timeout (goal not reached)", viol
    return "FAILED", viol


def mission_md(t, case, session, notes, idx):
    v, viol = verdict(t)
    p = PAPER[case]
    lines = [
        f"# Mission {idx}: dynamic {case}, seed {t.get('seed')}",
        "",
        f"**Result: {v}**",
        "",
        f"- Session: {session}",
        f"- Scenario: forest 100 m x 40 m, {t.get('num_obstacles', OBSTACLES[case])} obstacles "
        f"({int(round(100 * float(t.get('dynamic_ratio', 0.65))))}% moving), start "
        f"({t.get('start_x')}, {t.get('start_y')}, {t.get('start_z')}) -> goal "
        f"({t.get('goal_x')}, {t.get('goal_y')}, {t.get('goal_z')})",
        "- Planner: SANDO (MIT ACL) with open-source grb_compat solver (no Gurobi)",
        "",
        "| Metric | This mission | Paper (Table VII, SANDO) |",
        "|---|---|---|",
        f"| Goal reached | {'yes' if t.get('goal_reached') else 'no'} | 100% success |",
        (f"| Collisions (checked from recording) | {t.get('collision_count', 0)} | 0 |" if t.get("collision_checked")
         else "| Collisions | NOT VERIFIED (bag analysis pending) | 0 |"),
        f"| Min. distance to an obstacle surface | {f(t.get('min_distance_to_obstacles'), 2, ' m')} | n/a |",
        f"| Travel time | {f(t.get('flight_travel_time'), 1, ' s')} | {p['trav'][0]}-{p['trav'][1]} s |",
        f"| Path length | {f(t.get('path_length'), 1, ' m')} | {p['path'][0]}-{p['path'][1]} m |",
        f"| Avg / max speed | {f(t.get('avg_velocity'))} / {f(t.get('max_velocity'))} m/s | per-axis limit 5 m/s (3D speed may exceed 5) |",
        f"| Max acceleration | {f(t.get('max_acceleration'))} m/s^2 | per-axis limit 20 m/s^2 |",
        f"| Replans succeeded / attempted | {t.get('replans_succeeded', 'n/a')} / {t.get('replans_attempted', 'n/a')} | - |",
        f"| Replanning time mean / p95 / max | {f(t.get('replan_ms_mean'), 1)} / {f(t.get('replan_ms_p95'), 1)} / "
        f"{f(t.get('replan_ms_max'), 1)} ms | ~20-35 ms (hardware) |",
        f"| Local trajectory optimisation mean / p95 | {f(t.get('local_opt_ms_mean'), 1)} / {f(t.get('local_opt_ms_p95'), 1)} ms | "
        f"0.6-30 ms (Table VII, Gurobi) |",
        f"| Corridor (STSFC) generation mean | {f(t.get('stsfc_ms_mean'), 1, ' ms')} | 6-15 ms (Table IX) |",
        f"| Vel / acc / jerk violations | {t.get('vel_violation_count', 0)} / {t.get('acc_violation_count', 0)} / "
        f"{t.get('jerk_violation_count', 0)} | 0 / 0 / 0 |",
        f"| Corridor (SFC) violations | {t.get('sfc_violation_count', 0)} | 0 |",
        "",
    ]
    if notes or t.get("notes"):
        lines += ["## Notes", "", notes or "", str(t.get("notes") or ""), ""]
    return "\n".join(lines), v


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("json_path")
    ap.add_argument("case", choices=list(PAPER))
    ap.add_argument("out_dir")
    ap.add_argument("--session", default=datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M UTC"))
    ap.add_argument("--notes", default="")
    a = ap.parse_args()
    trials = json.loads(pathlib.Path(a.json_path).read_text())
    if not (isinstance(trials, list) and trials and isinstance(trials[0], dict) and "goal_reached" in trials[0]):
        print(f"skip {a.json_path}: not a list of benchmark trials")
        return
    out = pathlib.Path(a.out_dir)
    out.mkdir(parents=True, exist_ok=True)
    index = out / "INDEX.md"
    existing = sorted(out.glob("mission_*.md"))
    n0 = len(existing)
    rows = []
    for i, t in enumerate(trials, start=1):
        idx = n0 + i
        md, v = mission_md(t, a.case, a.session, a.notes, idx)
        name = f"mission_{idx:03d}_{a.case}_seed{t.get('seed')}.md"
        (out / name).write_text(md, encoding="utf-8")
        rows.append(f"| [{idx}]({name}) | {a.session} | {a.case} | {t.get('seed')} | {v} | "
                    f"{f(t.get('flight_travel_time'), 1, ' s')} | {f(t.get('path_length'), 1, ' m')} |")
    if not index.exists():
        index.write_text("# SANDO mission log\n\n| # | Session | Case | Seed | Result | Travel time | Path |\n"
                         "|---|---|---|---|---|---|---|\n", encoding="utf-8")
    with index.open("a", encoding="utf-8") as fh:
        fh.write("\n".join(rows) + "\n")
    ok = sum(1 for t in trials if verdict(t)[0].startswith("SUCCESS"))
    print(f"{len(trials)} mission report(s) written to {out}; {ok}/{len(trials)} successful")


if __name__ == "__main__":
    main()
