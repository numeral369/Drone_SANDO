#!/usr/bin/env python3
"""Render the investor video from extract_bag.py output with matplotlib (no GPU, no RViz).

Layout (1920x1080): banner, top-down follow view around the drone, full-course overview strip,
title card before and end card after (with metrics computed from this recording).
Usage: python3 render_mpl.py <timeline.npz> <metrics.json> <case> <out.mp4|png_dir> [--png N]
  --png N : write only N evenly spaced PNG frames into <png_dir> (for previews; no ffmpeg needed)
"""
import json
import os
import subprocess
import sys

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
import numpy as np  # noqa: E402
from matplotlib.collections import PatchCollection, PolyCollection  # noqa: E402
from matplotlib.patches import Rectangle  # noqa: E402

W, H, DPI, FPS = 1920, 1080, 100, 30
BG, FG, MUTED = "#191c24", "#e8edf5", "#8a94a8"
C_DRONE, C_PLAN, C_CORR, C_DYN, C_STAT, C_GOAL = "#00d1ff", "#ff5ec4", "#3ddc84", "#ff9f43", "#9aa4b8", "#ffd84d"
NOBS = {"easy": 50, "medium": 100, "hard": 200}


def card(fig, lines):
    fig.clf()
    fig.patch.set_facecolor(BG)
    for (y, txt, size, color, weight) in lines:
        fig.text(0.5, y, txt, ha="center", va="center", fontsize=size, color=color, weight=weight)


def frame_bytes(fig):
    fig.canvas.draw()
    return np.asarray(fig.canvas.buffer_rgba())[:, :, :3].tobytes()


def main():
    npz, metrics_path, case, out = sys.argv[1:5]
    png_n = int(sys.argv[sys.argv.index("--png") + 1]) if "--png" in sys.argv else 0
    d = np.load(npz)
    m = json.load(open(metrics_path))
    t, drone, ob, dyn = d["t"], d["drone"], d["obstacles"], d["dynamic"]
    corr_idx, corr_lens, corr_pts = d["corr_idx"], d["corr_lens"], d["corr_pts"]
    corr_off = np.concatenate([[0], np.cumsum(corr_lens)])
    plan_idx, plan_pts = d["plan_idx"], d["plan_pts"]
    goal = d["goal"]
    t_goal = float(d["t_goal"])
    nobs = NOBS.get(case, ob.shape[1])
    pct_dyn = int(round(100 * dyn.mean())) if len(dyn) else 0

    fig = plt.figure(figsize=(W / DPI, H / DPI), dpi=DPI)
    sink = None
    if not png_n:
        sink = subprocess.Popen(["ffmpeg", "-y", "-loglevel", "error", "-f", "rawvideo", "-pix_fmt", "rgb24",
                                 "-s", f"{W}x{H}", "-r", str(FPS), "-i", "-", "-c:v", "libx264", "-preset", "medium",
                                 "-crf", "20", "-pix_fmt", "yuv420p", "-movflags", "+faststart", out],
                                stdin=subprocess.PIPE)
    else:
        os.makedirs(out, exist_ok=True)

    def emit(n_frames=1, tag=None):
        if sink:
            b = frame_bytes(fig)
            for _ in range(n_frames):
                sink.stdin.write(b)
        elif tag is not None:
            fig.savefig(os.path.join(out, f"{tag}.png"), facecolor=fig.get_facecolor())

    # ---------------- title card (4 s) ----------------
    card(fig, [
        (0.60, "Autonomous drone flight among moving obstacles", 40, FG, "bold"),
        (0.50, f"Simulated mission: {nobs} obstacles in a 100 m forest, {pct_dyn}% of them moving", 24, "#b8c2d6", "normal"),
        (0.30, "SANDO planner by Kondo, Tordesillas & How - MIT Aerospace Controls Lab (arXiv:2604.07599)", 18, MUTED, "normal"),
        (0.25, "Simulation running the open-source planner with an open-source optimisation solver", 18, MUTED, "normal"),
    ])
    emit(4 * FPS, "00_title")

    # ---------------- mission frames ----------------
    fig.clf()
    fig.patch.set_facecolor(BG)
    banner = fig.text(0.02, 0.968, "Simulation - the drone re-plans its path about 30 times per second",
                      fontsize=22, color=FG, weight="bold", va="center")
    hud = fig.text(0.02, 0.928, "", fontsize=18, color=C_DRONE, ha="left", va="center", family="monospace")
    ax = fig.add_axes([0.02, 0.30, 0.96, 0.60])
    ov = fig.add_axes([0.02, 0.03, 0.96, 0.22])
    for a in (ax, ov):
        a.set_facecolor("#11141a")
        a.set_aspect("equal")
        a.tick_params(colors=MUTED, labelsize=10)
        for s in a.spines.values():
            s.set_color("#2c3240")
    ov.set_xlim(-3, 110); ov.set_ylim(-9, 9)
    ov.text(0.003, 0.93, "Whole course, top view (metres)", transform=ov.transAxes, color=FG, fontsize=13, va="top")
    ov.plot([goal[0]], [goal[1]], marker="*", ms=18, color=C_GOAL, zorder=6)
    ax_goal = ax.plot([goal[0]], [goal[1]], marker="*", ms=26, color=C_GOAL, zorder=6)[0]
    legend_items = [
        (C_DRONE, "drone (and flown path)"), (C_PLAN, "planned trajectory"), (C_CORR, "safe flight corridors"),
        (C_DYN, "moving obstacle"), (C_STAT, "static obstacle"), ("#4a5160", "faded = at a different height"),
    ]
    for k, (c, lab) in enumerate(legend_items):
        fig.text(0.03 + k * 0.158, 0.272, "■ " + lab, color=c if k != 5 else MUTED, fontsize=13, va="center")

    trail_drone, = ax.plot([], [], color=C_DRONE, lw=2.5, alpha=0.9, zorder=4)
    plan_line, = ax.plot([], [], color=C_PLAN, lw=2.5, alpha=0.95, zorder=5)
    drone_dot, = ax.plot([], [], "o", ms=14, color=C_DRONE, mec="white", mew=2, zorder=7)
    ov_trail, = ov.plot([], [], color=C_DRONE, lw=2, zorder=4)
    ov_dot, = ov.plot([], [], "o", ms=8, color=C_DRONE, mec="white", mew=1.5, zorder=7)
    dyn_coll = stat_coll = corr_coll = ov_obs = ov_win = None
    n = len(t)
    pick = set(np.linspace(0, n - 1, png_n).astype(int)) if png_n else None
    view_w, view_h = 46.0, 46.0 * 0.60 * H / (0.96 * W)
    for f in range(n):
        if pick is not None and f not in pick:
            continue
        x, y, z = drone[f, :3]
        spd = float(np.linalg.norm(drone[f, 3:6]))
        x0 = min(max(x - 14, -3), 112 - view_w)
        ax.set_xlim(x0, x0 + view_w); ax.set_ylim(-view_h / 2, view_h / 2)
        # obstacles: faded when their vertical extent does not include the drone's height (+-0.3 m)
        rects_d, rects_s, alph_d, alph_s, ov_rects = [], [], [], [], []
        for j in range(ob.shape[1]):
            ox, oy, oz, hx, hy, hz = ob[f, j]
            overlap = abs(z - oz) <= hz / 2 + 0.3
            r = Rectangle((ox - hx / 2, oy - hy / 2), hx, hy)
            (rects_d if dyn[j] else rects_s).append(r)
            (alph_d if dyn[j] else alph_s).append(1.0 if overlap else 0.22)
            ov_rects.append(Rectangle((ox - hx / 2, oy - hy / 2), hx, hy))
        for coll in (dyn_coll, stat_coll, corr_coll, ov_obs, ov_win):
            if coll is not None:
                coll.remove()
        dyn_coll = PatchCollection(rects_d, facecolors=[(1.0, 0.62, 0.26, a) for a in alph_d], edgecolors="none", zorder=3)
        stat_coll = PatchCollection(rects_s, facecolors=[(0.6, 0.64, 0.72, a) for a in alph_s], edgecolors="none", zorder=3)
        ax.add_collection(dyn_coll); ax.add_collection(stat_coll)
        s0, cnt = corr_idx[f]
        polys = [corr_pts[corr_off[k]:corr_off[k + 1]] for k in range(s0, s0 + cnt)]
        corr_coll = PolyCollection(polys, facecolors=(0.24, 0.86, 0.52, 0.13), edgecolors=(0.24, 0.86, 0.52, 0.55),
                                   linewidths=1.2, zorder=2)
        ax.add_collection(corr_coll)
        p0, pc = plan_idx[f]
        pp = plan_pts[p0:p0 + pc]
        plan_line.set_data(pp[:, 0], pp[:, 1]) if len(pp) else plan_line.set_data([], [])
        trail_drone.set_data(drone[:f + 1, 0], drone[:f + 1, 1])
        drone_dot.set_data([x], [y])
        ov_obs = PatchCollection(ov_rects, facecolors=[C_DYN if k else C_STAT for k in dyn], edgecolors="none", zorder=3, alpha=0.8)
        ov.add_collection(ov_obs)
        ov_win = PatchCollection([Rectangle((x0, -view_h / 2), view_w, view_h)], facecolors="none",
                                 edgecolors="white", linewidths=1.2, zorder=5, alpha=0.6)
        ov.add_collection(ov_win)
        ov_trail.set_data(drone[:f + 1, 0], drone[:f + 1, 1])
        ov_dot.set_data([x], [y])
        dist = float(np.linalg.norm(goal - drone[f, :3]))
        status = "GOAL REACHED" if t[f] >= t_goal else f"to goal {dist:5.1f} m"
        hud.set_text(f"Follow view (top)   t {t[f]:5.1f} s   speed {spd:4.1f} m/s   altitude {z:3.1f} m   {status}")
        emit(1, f"{f:05d}")

    # ---------------- end card (7 s) ----------------
    viol = m["vel_violations"] + m["acc_violations"] + m["jerk_violations"]
    card(fig, [
        (0.72, "Mission complete", 40, FG, "bold"),
        (0.60, f"{m['path_length']:.0f} m through the forest in {m['travel_time']:.1f} s", 26, FG, "normal"),
        (0.52, f"Collisions: {m['collision_count']}     Speed / acceleration / jerk limit violations: {viol}", 24, FG, "normal"),
        (0.45, f"Closest approach to an obstacle surface: {m['min_distance']:.2f} m "
               f"(checked against all {m['obstacles_checked']} obstacles)", 22, "#b8c2d6", "normal"),
        (0.28, "Simulation of the open-source SANDO planner (MIT Aerospace Controls Lab), "
               "running with an open-source optimisation solver.", 16, MUTED, "normal"),
        (0.24, "Not a hardware flight. All numbers are computed from this exact recording.", 16, MUTED, "normal"),
    ])
    emit(7 * FPS, "99_end")
    if sink:
        sink.stdin.close()
        sink.wait()
        print(f"wrote {out}")
    else:
        print(f"wrote preview PNGs to {out}")


if __name__ == "__main__":
    main()
