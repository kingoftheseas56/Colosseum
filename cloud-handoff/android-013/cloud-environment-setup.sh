#!/usr/bin/env bash
# Paste into Claude Code CLOUD environment settings. Do not run on Hemanth's hosts.
# Lightweight bootstrap only; heavyweight Android installation belongs to C0.
set -u
mkdir -p /opt/colosseum-cloud
log=/opt/colosseum-cloud/bootstrap.log
{
  date -u +%FT%TZ
  uname -a
  if command -v apt-get >/dev/null 2>&1; then
    if timeout 210s bash -c 'export DEBIAN_FRONTEND=noninteractive; apt-get update && apt-get install -y --no-install-recommends ca-certificates curl git unzip zip xz-utils ninja-build build-essential pkg-config perl patch python3 python3-venv python3-pip ffmpeg libgl1 libegl1 libxkbcommon0 libdbus-1-3'; then
      printf '%s\n' bootstrap_packages=available
    else
      printf '%s\n' bootstrap_packages=unqualified-retry-in-session
    fi
  else
    printf '%s\n' bootstrap_packages=unknown-package-manager
  fi
  df -h / /tmp
} >"$log" 2>&1
# Starting a session is not proof setup passed. C0 must inspect this receipt.
exit 0
