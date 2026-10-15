#!/usr/bin/env bash
# Build the real desktop candidate. Dependency installation is intentionally explicit.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
: "${QT_ROOT_DIR:?Set QT_ROOT_DIR to a full Qt 6.11.1 GCC SDK, not a PySide runtime}"
: "${MPVQT_PREFIX:?Set MPVQT_PREFIX to MpvQt 1.2.0 built against that Qt SDK}"
build="${COLOSSEUM_BUILD_DIR:-$root/build-linux-release}"
version="$($QT_ROOT_DIR/bin/qmake -query QT_VERSION)"
[[ "$version" == 6.11.1 ]] || { echo "Expected Qt 6.11.1; got $version" >&2; exit 2; }
prefix="$QT_ROOT_DIR;$MPVQT_PREFIX${CMAKE_PREFIX_PATH:+;$CMAKE_PREFIX_PATH}"
cmake -S "$root/native" -B "$build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON \
  -DCOLOSSEUM_BUILD_PLAYER2=OFF -DCOLOSSEUM_PLAYER2_IN_APP=OFF \
  -DCOLOSSEUM_NO_COMPILER_CACHE=ON \
  -DCMAKE_CXX_FLAGS=-fPIC -DCMAKE_C_FLAGS=-fPIC \
  -DCMAKE_PREFIX_PATH="$prefix" -DMPVQT_PREFIX="$MPVQT_PREFIX"
cmake --build "$build" --target colosseum_runtime_ready lanista --parallel "${BUILD_JOBS:-2}"
printf 'Source commit: '; git -C "$root" rev-parse HEAD
printf 'Working-tree changes:\n'; git -C "$root" status --short
