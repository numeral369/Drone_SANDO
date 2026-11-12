#!/usr/bin/env python3
"""Extract a compact, render-ready timeline from a SANDO mission bag (runs on the droplet, needs ROS 2).

Trims to the actual flight: from the first commanded motion to 3 s after the drone is within 1 m of the goal
(the benchmark recorder can miss /goal_reached and keep recording until its timeout).
Output (npz): per-frame drone state, obstacle boxes, 2-D corridor slices at flight altitude, planned path.
Usage: python3 extract_bag.py <bag_dir> <out.npz> [fps=30]
"""
import sys
from collections import defaultdict

import numpy as np
from rclpy.serialization import deserialize_message
from rosbag2_py import ConverterOptions, SequentialReader, StorageOptions
from rosidl_runtime_py.utilities import get_message

GOAL = np.array([105.0, 0.0, 2.0])
TOPICS = {"/NX01/goal": "dynus_interfaces/msg/Goal", "/trajs": "dynus_interfaces/msg/DynTraj",
          "/NX01/poly_safe": "decomp_ros_msgs/msg/PolyhedronArray",
          "/NX01/traj_committed_colored": "visualization_msgs/msg/MarkerArray"}


def clip_polygon(poly, n, d):
    """Sutherland-Hodgman: keep points with n.x <= d (2-D)."""
    out = []
    for i in range(len(poly)):
        a, b = poly[i], poly[(i + 1) % len(poly)]
        fa, fb = n @ a - d, n @ b - d
        if fa <= 0:
            out.append(a)
        if fa * fb < 0:
            out.append(a + (b - a) * (fa / (fa - fb)))
    return out


def slice_polyhedron(points, normals, z):
    """2-D cross-section of {x: n_i.(x - p_i) <= 0} at height z, clipped to a 200 m square."""
    poly = [np.array(v, float) for v in ((-50, -100), (200, -100), (200, 100), (-50, 100))]
    for p, n in zip(points, normals):
        n3 = np.array([n.x, n.y, n.z]); p3 = np.array([p.x, p.y, p.z])
        n2 = n3[:2]
        d = n3 @ p3 - n3[2] * z
        if np.linalg.norm(n2) < 1e-9:
            if d < 0:
                return []
            continue
        poly = clip_polygon(poly, n2, d)
        if len(poly) < 3:
            return []
    return poly


def main():
    bag, out = sys.argv[1], sys.argv[2]
    fps = float(sys.argv[3]) if len(sys.argv) > 3 else 30.0
    r = SequentialReader()
    r.open(StorageOptions(uri=bag, storage_id="sqlite3"), ConverterOptions("", ""))
    types = {t.name: t.type for t in r.get_all_topics_and_types()}
    msg_cls = {k: get_message(types.get(k, v)) for k, v in TOPICS.items() if k in types}
    drone, obs, polys, plans = [], defaultdict(list), [], []
    while r.has_next():
        topic, data, t = r.read_next()
        if topic not in msg_cls:
            continue
        m = deserialize_message(data, msg_cls[topic])
        ts = t * 1e-9
        if topic == "/NX01/goal":
            drone.append((ts, m.p.x, m.p.y, m.p.z, m.v.x, m.v.y, m.v.z))
        elif topic == "/trajs" and not m.is_agent:
            bb = list(m.bbox) if len(m.bbox) >= 3 else [0.8, 0.8, 0.8]
            obs[m.id].append((ts, m.pos.x, m.pos.y, m.pos.z, bb[0], bb[1], bb[2]))
        elif topic == "/NX01/poly_safe":
            polys.append((ts, [(list(h.points), list(h.normals)) for h in m.polyhedrons]))
        elif topic == "/NX01/traj_committed_colored":
            pts = []
            for mk in m.markers:
                pts += [(p.x, p.y) for p in mk.points] or [(mk.pose.position.x, mk.pose.position.y)]
            plans.append((ts, pts))
    drone = np.array(drone)
    if len(drone) == 0:
        sys.exit("no /NX01/goal messages")
    pos = drone[:, 1:4]
    moving = np.where(np.linalg.norm(pos - pos[0], axis=1) > 0.05)[0]
    t_start = drone[moving[0], 0] - 1.0 if len(moving) else drone[0, 0]
    near = np.where(np.linalg.norm(pos - GOAL, axis=1) < 1.0)[0]
    t_goal = drone[near[0], 0] if len(near) else drone[-1, 0]
    t_end = min(t_goal + 3.0, drone[-1, 0])
    frames = np.arange(t_start, t_end, 1.0 / fps)

    def at(series_t, k):
        return np.clip(np.searchsorted(series_t, k, side="right") - 1, 0, len(series_t) - 1)

    di = at(drone[:, 0], frames)
    dstate = drone[di, 1:]
    ids = sorted(obs)
    obs_arr = {i: np.array(obs[i]) for i in ids}
    ob = np.zeros((len(frames), len(ids), 6))
    for j, i in enumerate(ids):
        a = obs_arr[i]
        ob[:, j, :] = a[at(a[:, 0], frames), 1:]
    dyn = np.array([np.ptp(obs_arr[i][:, 1:4], axis=0).max() > 0.05 for i in ids])
    # corridors / plans: keep the latest message per frame, sliced at the drone's current height
    pt = np.array([p[0] for p in polys]) if polys else np.zeros(0)
    lt = np.array([p[0] for p in plans]) if plans else np.zeros(0)
    corr_flat, corr_idx, plan_flat, plan_idx = [], [], [], []
    for f, tf in enumerate(frames):
        cs = []
        if len(pt) and tf >= pt[0]:
            for pts, nrm in polys[at(pt, tf)][1]:
                s = slice_polyhedron(pts, nrm, dstate[f, 2])
                if len(s) >= 3:
                    cs.append(np.array(s))
        corr_idx.append((len(corr_flat), len(cs)))
        corr_flat += cs
        pl = plans[at(lt, tf)][1] if len(lt) and tf >= lt[0] else []
        plan_idx.append((len(plan_flat), len(pl)))
        plan_flat += pl
    cp_lens = np.array([len(c) for c in corr_flat], int)
    cp_pts = np.concatenate(corr_flat) if corr_flat else np.zeros((0, 2))
    np.savez_compressed(out, t=frames - t_start, drone=dstate, obstacles=ob, dynamic=dyn,
                        corr_idx=np.array(corr_idx), corr_lens=cp_lens, corr_pts=cp_pts,
                        plan_idx=np.array(plan_idx), plan_pts=np.array(plan_flat).reshape(-1, 2),
                        t_goal=t_goal - t_start, goal=GOAL)
    print(f"frames={len(frames)} duration={frames[-1] - t_start:.1f}s goal_at={t_goal - t_start:.1f}s "
          f"obstacles={len(ids)} (dynamic {int(dyn.sum())}) corridor_msgs={len(polys)} plan_msgs={len(plans)}")


if __name__ == "__main__":
    main()
