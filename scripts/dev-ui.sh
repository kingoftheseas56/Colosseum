#!/bin/sh
# Dev loop: daemon on a fixed port + the GPUI shell against it.
set -e
export GST_PLUGIN_PATH="$(brew --prefix)/lib/gstreamer-1.0${GST_PLUGIN_PATH:+:$GST_PLUGIN_PATH}"

DAEMON_PORT=8123 cargo run -p daemon --quiet &
DAEMON_PID=$!
trap 'kill $DAEMON_PID 2>/dev/null' EXIT

i=0
until curl -sf http://127.0.0.1:8123/healthz >/dev/null 2>&1; do
  i=$((i+1))
  [ "$i" -gt 50 ] && { echo "daemon never became healthy"; exit 1; }
  sleep 0.2
done

DAEMON_URL=http://127.0.0.1:8123 cargo run -p ui-gpui
