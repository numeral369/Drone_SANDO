#!/bin/bash
# Run SANDO dynamic missions (paper Sec. IX-D: rviz-only, ground-truth obstacles) on the droplet.
# Usage: bash run_missions.sh <easy|medium|hard> <num_trials> [start_seed]
# Output: ~/missions/<timestamp>_<case>/ (benchmark CSV + JSON + logs)
set -uo pipefail
CASE="$1"; TRIALS="${2:-1}"; SEED="${3:-0}"
set +u; source /opt/ros/humble/setup.bash; source /root/sando_ws/install/setup.bash; set -u
export LD_LIBRARY_PATH=/usr/local/lib:/root/livox_ws/install/livox_ros_driver2/lib:${LD_LIBRARY_PATH:-}
export ROS_DOMAIN_ID=20
# headless display (RViz is off for metric runs, but some nodes still want a DISPLAY)
pgrep -x Xvfb >/dev/null || (Xvfb :99 -screen 0 1920x1080x24 >/dev/null 2>&1 &)
export DISPLAY=:99 LIBGL_ALWAYS_SOFTWARE=1
OUT=~/missions/$(date -u +%Y%m%d_%H%M%S)_${CASE}
mkdir -p "$OUT"
cd /root/sando_ws
echo "[run_missions] case=$CASE trials=$TRIALS seed=$SEED -> $OUT"
python3 src/sando/scripts/run_benchmark.py --setup-bash install/setup.bash \
  --cases "$CASE" --num-trials "$TRIALS" --start-seed "$SEED" --timeout 90 \
  --output-dir "$OUT" --config-name "$CASE" 2>&1 | tee "$OUT/run.log"
python3 src/sando/scripts/kill_ros_processes.py >/dev/null 2>&1 || true
# collision + violation check from bags (authors' analysis functions), computation stats
python3 -c "import pandas" 2>/dev/null || apt-get install -y -q python3-pandas >/dev/null
python3 ~/sando_demo/remote/postprocess.py "$OUT/$CASE" 2>&1 | tee "$OUT/postprocess.log"
ls -la "$OUT"
