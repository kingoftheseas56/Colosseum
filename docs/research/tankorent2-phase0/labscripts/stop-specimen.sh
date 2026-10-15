#!/usr/bin/env bash
# Stop the lab specimen. Kills by the WINDOWS pid that owns :11480 — never by image
# name (house Rule 1: taskkill //IM would also kill a brother's runtime), and never
# by the bash job pid, which is not the Windows pid the OS knows.
set -u
LAB_PORT=11480

pids=$(netstat -ano 2>/dev/null | grep LISTENING | grep ":${LAB_PORT} " | awk '{print $5}' | sort -u)
if [ -z "$pids" ]; then
  echo "nothing listening on :$LAB_PORT — already stopped"
  exit 0
fi
for p in $pids; do
  echo "stopping lab pid $p"
  powershell -NoProfile -Command "Stop-Process -Id $p -Force -ErrorAction SilentlyContinue"
done
sleep 2
if netstat -ano 2>/dev/null | grep LISTENING | grep -q ":${LAB_PORT} "; then
  echo "WARN: :$LAB_PORT still listening" >&2
  exit 1
fi
echo "lab stopped; :$LAB_PORT clear"
