#!/bin/sh
# One command to run the app: daemon + GPUI shell.
# Generates the test clip if missing; sets build + runtime env; cleans up.
set -e
cd "$(dirname "$0")/.."

BREW="$(brew --prefix)"
export PKG_CONFIG_PATH="$BREW/lib/pkgconfig:$PKG_CONFIG_PATH"
export GST_PLUGIN_PATH="$BREW/lib/gstreamer-1.0${GST_PLUGIN_PATH:+:$GST_PLUGIN_PATH}"

TEST_CLIP=/tmp/colosseum-ui-test.mp4
if [ ! -f "$TEST_CLIP" ]; then
  echo "[app] generating test clip…"
  ffmpeg -y -hide_banner -loglevel error \
    -f lavfi -i testsrc2=size=1280x720:rate=30:duration=15 \
    -f lavfi -i sine=frequency=440 \
    -c:v libx264 -pix_fmt yuv420p -c:a aac -shortest "$TEST_CLIP"
fi

cargo build -q -p daemon -p ui-gpui

./target/debug/daemon &
DAEMON_PID=$!
trap 'kill $DAEMON_PID 2>/dev/null' EXIT

i=0
until curl -sf http://127.0.0.1:8123/healthz >/dev/null 2>&1; do
  i=$((i+1))
  [ "$i" -gt 50 ] && { echo "[app] daemon never became healthy"; exit 1; }
  sleep 0.2
done

DAEMON_URL=http://127.0.0.1:8123 ./target/debug/ui-gpui
