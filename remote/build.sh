#!/bin/bash
# Native build of SANDO on Ubuntu 22.04 (DigitalOcean droplet, run as root).
# Mirrors the upstream docker/Dockerfile (mit-acl/sando v0.0.3) but:
#   * no Gurobi: the open-source grb_compat solver is patched in (solver/apply_open_solver.py)
#   * headless extras for recording (Xvfb, ffmpeg, mesa software GL)
# Usage: bash build.sh <jobs>     (expects ~/sando_demo/solver uploaded beforehand)
set -euo pipefail
JOBS="${1:-4}"
export DEBIAN_FRONTEND=noninteractive NEEDRESTART_MODE=a
LOG=~/build.log
exec > >(tee -a "$LOG") 2>&1
t0=$(date +%s)
step() { echo; echo "=== [$(( $(date +%s) - t0 ))s] $* ==="; }

step "swap (protects the compile from OOM)"
if ! swapon --show | grep -q /swapfile; then
  fallocate -l 8G /swapfile && chmod 600 /swapfile && mkswap /swapfile && swapon /swapfile
fi

step "ROS 2 apt repository"
apt-get update -q
apt-get install -y -q curl gnupg lsb-release software-properties-common locales
locale-gen en_US en_US.UTF-8 >/dev/null
add-apt-repository -y universe >/dev/null
if [ ! -f /etc/apt/sources.list.d/ros2.list ]; then
  curl -sSL https://raw.githubusercontent.com/ros/rosdistro/master/ros.key -o /usr/share/keyrings/ros-archive-keyring.gpg
  echo "deb [arch=$(dpkg --print-architecture) signed-by=/usr/share/keyrings/ros-archive-keyring.gpg] http://packages.ros.org/ros2/ubuntu $(. /etc/os-release && echo $UBUNTU_CODENAME) main" > /etc/apt/sources.list.d/ros2.list
fi
apt-get update -q

step "system + ROS packages (same list as upstream Dockerfile, plus headless recording tools)"
apt-get install -y -q --no-install-recommends \
  git tmux vim wget tmuxp make gdb build-essential python3-pip \
  libomp-dev libpcl-dev libeigen3-dev nlohmann-json3-dev \
  ros-humble-desktop ros-dev-tools \
  ros-humble-gazebo-ros-pkgs ros-humble-pcl-conversions ros-humble-example-interfaces \
  ros-humble-pcl-ros ros-humble-rviz2 ros-humble-rviz-common ros-humble-rqt-gui \
  ros-humble-rqt-gui-py ros-humble-tf2-tools ros-humble-tf-transformations \
  ros-humble-gazebo-ros2-control ros-humble-xacro \
  xvfb ffmpeg x11-utils libgl1-mesa-dri libgl1-mesa-glx mesa-utils psmisc

step "clone SANDO v0.0.3 + submodules"
export SANDO_WS=/root/sando_ws LIVOX_WS=/root/livox_ws
mkdir -p $SANDO_WS/src && cd $SANDO_WS/src
if [ ! -d sando ]; then
  git clone --branch v0.0.3 --depth 1 https://github.com/mit-acl/sando.git
  (cd sando && git submodule update --init --recursive --depth 1)
fi

step "patch in open-source solver"
python3 ~/sando_demo/solver/apply_open_solver.py $SANDO_WS/src/sando ~/sando_demo/solver/grb_compat

step "symlink deps for colcon"
cd $SANDO_WS/src
for d in sando/deps/*/; do
  name=$(basename "$d")
  case "$name" in Livox-SDK2|livox_ros_driver2|grb_compat) continue;; esac
  [ ! -e "$name" ] && ln -s "$d" "$name" || true
done

step "Livox-SDK2"
if [ ! -f /usr/local/lib/liblivox_lidar_sdk_static.a ]; then
  cd $SANDO_WS/src/sando/deps/Livox-SDK2 && mkdir -p build && cd build && cmake .. >/dev/null && make -j"$JOBS" >/dev/null && make install >/dev/null
fi

step "livox_ros_driver2"
set +u; source /opt/ros/humble/setup.bash; set -u
if [ ! -d $LIVOX_WS/install/livox_ros_driver2 ]; then
  mkdir -p $LIVOX_WS/src && cd $LIVOX_WS/src
  rm -rf livox_ros_driver2 && cp -r $SANDO_WS/src/sando/deps/livox_ros_driver2 livox_ros_driver2
  cd livox_ros_driver2 && rm -f COLCON_IGNORE && cp -f package_ROS2.xml package.xml && cp -rf launch_ROS2/ launch/
  cd $LIVOX_WS && colcon build --cmake-args -DCMAKE_BUILD_TYPE=Release -DROS_EDITION=ROS2 -DHUMBLE_ROS=humble
fi

step "colcon: decomp_util"
export MAKEFLAGS="-j2"
cd $SANDO_WS
colcon build --packages-select decomp_util --parallel-workers "$JOBS" --cmake-args -DCMAKE_BUILD_TYPE=Release
step "colcon: everything except ros2_livox_simulation"
set +u; source $SANDO_WS/install/setup.bash; set -u
colcon build --packages-ignore ros2_livox_simulation --parallel-workers "$JOBS" --cmake-args -DCMAKE_BUILD_TYPE=Release
step "colcon: ros2_livox_simulation"
set +u; source $SANDO_WS/install/setup.bash; source $LIVOX_WS/install/setup.bash; set -u
colcon build --packages-select ros2_livox_simulation --parallel-workers "$JOBS" --cmake-args -DCMAKE_BUILD_TYPE=Release

step "environment"
echo 'pcm.!default { type plug; slave.pcm "null" }' >> /usr/share/alsa/alsa.conf
grep -q sando_ws/install ~/.bashrc || cat >> ~/.bashrc <<'EOF'
source /opt/ros/humble/setup.bash
source /root/sando_ws/install/setup.bash
export LD_LIBRARY_PATH=/usr/local/lib:/root/livox_ws/install/livox_ros_driver2/lib:$LD_LIBRARY_PATH
export ROS_DOMAIN_ID=20
EOF

step "T2: build verification"
set +u; source $SANDO_WS/install/setup.bash; set -u
ros2 pkg list | grep -E '^sando$' && echo "T2 PASS: sando package installed"
ldd $SANDO_WS/install/sando/lib/sando/sando | grep -i gurobi && { echo "T2 FAIL: links Gurobi"; exit 1; } || echo "T2 PASS: no Gurobi linked"
echo "BUILD DONE in $(( $(date +%s) - t0 ))s"
