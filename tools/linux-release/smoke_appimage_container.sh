#!/usr/bin/env bash
# Only OS graphics drivers, fonts, trust roots and desktop services; never install Qt/mpv here.
set -euo pipefail
trap 'status=$?; chmod -R a+rX /evidence || true; exit "$status"' EXIT
cat /etc/os-release > /evidence/os-release.txt
. /etc/os-release
case "$ID" in
  debian|ubuntu)
    export DEBIAN_FRONTEND=noninteractive
    apt-get update
    apt-get install -y --no-install-recommends python3 passwd util-linux dbus ca-certificates \
      libgl1 libegl1 libglx0 libopengl0 libglvnd0 libdrm2 libgbm1 libvulkan1 \
      libva2 libva-drm2 libva-x11-2 libasound2-data fontconfig fonts-dejavu-core libgtk-3-common
    ;;
  fedora)
    dnf install -y python3 shadow-utils util-linux dbus-daemon dbus-tools ca-certificates \
      mesa-libGL mesa-libEGL libglvnd-glx libglvnd-opengl libdrm mesa-libgbm vulkan-loader \
      libva alsa-lib fontconfig dejavu-sans-fonts gtk3
    ;;
  arch)
    pacman -Syu --noconfirm --needed python shadow util-linux dbus ca-certificates mesa \
      libglvnd libdrm vulkan-icd-loader libva alsa-lib fontconfig ttf-dejavu gtk3
    ;;
  opensuse-tumbleweed)
    zypper --non-interactive refresh
    zypper --non-interactive install --no-recommends python3 shadow util-linux dbus-1 ca-certificates \
      libGL1 libEGL1 libGLX0 libOpenGL0 libdrm2 libgbm1 libvulkan1 libva2 libva-drm2 \
      libva-x11-2 alsa libasound2 fontconfig dejavu-fonts gtk3-tools
    ;;
  *) echo "Unrecognised smoke distro: $ID" >&2; exit 1 ;;
esac
useradd --create-home --shell /bin/bash appimage-smoke
chown -R appimage-smoke /evidence
mapfile -t images < <(find /artifacts -maxdepth 1 -type f -name 'Colosseum-*-x86_64.AppImage')
[[ "${#images[@]}" == 1 ]] || { echo 'Expected exactly one built AppImage' >&2; exit 1; }
image="${images[0]}"
: "${SOURCE_COMMIT:?Exact checkout SHA is required}"
runuser -u appimage-smoke -- dbus-run-session -- python3 \
  /work/tools/linux-release/smoke_appimage.py --image "$image" --manifest "$image.json" \
  --source-commit "$SOURCE_COMMIT" --output /evidence
