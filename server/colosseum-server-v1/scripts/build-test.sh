#!/usr/bin/env bash
# External dependencies must be acquired separately; no SDK/binaries are bundled.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
: "${QT_ROOT:?Set QT_ROOT to the extracted Qt 6.11.1 prefix}"
: "${LT_ROOT:?Set LT_ROOT to the static libtorrent 2.0.11 install prefix}"
: "${BOOST_ROOT:?Set BOOST_ROOT to Boost 1.83.0 headers}"
NODE_BIN="${NODE_BIN:-$(command -v node)}"
BUILD_DIR="${BUILD_DIR:-$ROOT/build}"
ORACLE="$ROOT/reference/source-pack/oracle/stremio-service-v4.21.1-server-bundle/server.js"
test -f "$ORACLE" || { echo 'First run python3 scripts/acquire-oracle.py'; exit 2; }
test "$("$NODE_BIN" -p 'process.versions.node.split(".")[0]')" = 24 || { echo 'All 158 registered tests require Node 24.'; exit 2; }
command -v pwsh >/dev/null
command -v node >/dev/null
test "$(node -p 'process.versions.node.split(".")[0]')" = 24 || { echo 'Put Node 24 on PATH for existing bare-node test helpers.'; exit 2; }
command -v python >/dev/null || { echo 'Provide python as Python 3 (activate a venv or install python-is-python3).'; exit 2; }
python -c 'import sys; assert sys.version_info.major == 3, "python must be Python 3"'
test -x /usr/bin/ffmpeg && test -x /usr/bin/ffprobe
export HOME="$ROOT/.local-home"
export XDG_CACHE_HOME="$HOME/.cache" XDG_CONFIG_HOME="$HOME/.config"
export LD_LIBRARY_PATH="$QT_ROOT/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export SERVER1_TEST_FIXED_INTERFACE=1
export PYTHON="$(command -v python3)"
mkdir -p "$XDG_CACHE_HOME" "$XDG_CONFIG_HOME"
cmake -S "$ROOT/native/colosseum_server_v1" -B "$BUILD_DIR" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$QT_ROOT;$LT_ROOT" \
  -DLibtorrentRasterbar_DIR="$LT_ROOT/lib/cmake/LibtorrentRasterbar" \
  -DBOOST_ROOT="$BOOST_ROOT" -DBoost_NO_SYSTEM_PATHS=ON -DBoost_NO_BOOST_CMAKE=ON \
  -DCMAKE_POLICY_DEFAULT_CMP0167=OLD -DSERVER1_K12_ORACLE="$ORACLE" \
  -DSERVER1_NODE_EXECUTABLE="$NODE_BIN"
cmake --build "$BUILD_DIR" --parallel "${BUILD_JOBS:-4}"
ctest --test-dir "$BUILD_DIR" --output-on-failure --parallel "${TEST_JOBS:-2}"
