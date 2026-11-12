#!/bin/bash
# Record one dynamic mission, verify it with the authors' analysis code, and render the investor video
# with matplotlib (no RViz/GPU): title card, follow view + whole-course overview, end card with real metrics.
# Usage: bash make_video.sh <easy|medium|hard> [existing_bag_dir]
set -uo pipefail
CASE="${1:-medium}"; BAG="${2:-}"
set +u; source /opt/ros/humble/setup.bash; source /root/sando_ws/install/setup.bash; set -u
export LD_LIBRARY_PATH=/usr/local/lib:/root/livox_ws/install/livox_ros_driver2/lib:${LD_LIBRARY_PATH:-}
export ROS_DOMAIN_ID=20
V=~/video; mkdir -p $V
cd /root/sando_ws
python3 -c "import pandas, matplotlib" 2>/dev/null || { export DEBIAN_FRONTEND=noninteractive; apt-get update -q >/dev/null && apt-get install -y -q python3-pandas python3-matplotlib >/dev/null; }

if [ -z "$BAG" ]; then
  echo "[video] 1/4 recording mission (dynamic $CASE)"
  python3 src/sando/scripts/run_sim.py --mode benchmark-record --benchmark-cases dynamic \
    --benchmark-difficulties "$CASE" --timeout 60 --no-rviz -s install/setup.bash > $V/record.log 2>&1
  python3 src/sando/scripts/kill_ros_processes.py >/dev/null 2>&1 || true
  BAG=$(ls -d src/sando/benchmark_data/visualization_bags/*/dynamic_${CASE}/recording | tail -1)
fi
echo "[video] bag: $BAG"
echo "[video] 2/4 verifying the recorded mission (authors' collision + violation checks)"
python3 ~/sando_demo/remote/video_metrics.py "$BAG" $V/metrics.json | tee $V/metrics.log
echo "[video] 3/4 extracting timeline"
python3 ~/sando_demo/remote/extract_bag.py "$BAG" $V/timeline.npz 30 | tee $V/extract.log
echo "[video] 3b/4 preview frames"
python3 ~/sando_demo/remote/render_mpl.py $V/timeline.npz $V/metrics.json "$CASE" $V/preview --png 4
echo "[video] 4/4 rendering MP4"
python3 ~/sando_demo/remote/render_mpl.py $V/timeline.npz $V/metrics.json "$CASE" $V/sando_mission_${CASE}.mp4
ls -la $V
echo "[video] DONE $V/sando_mission_${CASE}.mp4"
