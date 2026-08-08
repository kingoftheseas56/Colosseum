#!/usr/bin/env bash
# package_release.sh — assemble a self-contained Colosseum Windows installer from the
# CURRENT build (today's native\build-msvc), NOT a git-tag rebuild.
#
# WHY THIS EXISTS: v0.1 was staged by hand and the Stremio stream-server was left out of
# the payload. The app looks for that server next to its exe FIRST (see
# StreamServer::findRuntimeDir), so bundling it here fixes Theatre torrent streaming +
# downloads for every machine — not just the developer's, where a personal Stremio
# install happened to sit at a hardcoded path. This script encodes the staging so the
# server can never silently fall out of a release again.
#
# Usage:  bash scripts/installer/package_release.sh [VERSION]
#   VERSION defaults to 0.1. Override the Stremio source dir with STREMIO_SRC=<dir>.
set -euo pipefail

VERSION="${1:-0.1}"
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
# Release integrity (1.0 lesson): the daily build dir can contain uncommitted WIP compiled
# in. Point BUILD_DIR at a clean sandbox build of committed HEAD (git archive -> cmake)
# so the shipped exe is reproducible from the release tag. Defaults to the daily dir for
# local smoke-packaging only.
BUILD_DIR="${BUILD_DIR:-$REPO/native/build-msvc}"
STREMIO_SRC="${STREMIO_SRC:-/c/Users/Suprabha/AppData/Local/Programs/StremioService}"
MAKENSIS="/c/Program Files (x86)/NSIS/makensis.exe"
DIST="$REPO/dist"
STAGE="$DIST/stage"
OUT="$DIST/Colosseum-$VERSION-setup.exe"

[ -x "$MAKENSIS" ] || { echo "makensis not found at $MAKENSIS"; exit 1; }
[ -f "$STREMIO_SRC/stremio-runtime.exe" ] || { echo "Stremio source missing: $STREMIO_SRC"; exit 1; }
[ -f "$BUILD_DIR/colosseum.exe" ] || { echo "build missing: $BUILD_DIR/colosseum.exe"; exit 1; }

# The test-key updater build is intentionally not shippable.  Check the cache
# before archiving or stripping the build so a release can never embed the
# Lanista-only trust root by accident.
if [ -f "$BUILD_DIR/CMakeCache.txt" ] \
  && grep -Eq '^COLOSSEUM_UPDATE_TESTING:BOOL=ON$' "$BUILD_DIR/CMakeCache.txt"; then
  echo "refusing to package COLOSSEUM_UPDATE_TESTING=ON build"
  exit 1
fi

echo "[1/6] clean stage -> $STAGE"
rm -rf "$STAGE"; mkdir -p "$STAGE"

echo "[2/6] source tree at HEAD (tracked files)"
git -C "$REPO" archive --format=tar HEAD | tar -x -C "$STAGE"

echo "[3/6] overlay windeployqt runtime from $BUILD_DIR"
mkdir -p "$STAGE/native/build-msvc"
cp -r "$BUILD_DIR/." "$STAGE/native/build-msvc/"
# Felt-speed Stage 0: an installer without the webp decoder ships blank covers. Refuse.
[ -f "$STAGE/native/build-msvc/imageformats/qwebp.dll" ] \
  || { echo "qwebp.dll missing from runtime — run native/deploy-runtime.bat first"; exit 1; }

echo "[4/6] strip build intermediates (keeps ALL runtime: dlls, tools/, qml/, resources/, translations/)"
( cd "$STAGE/native/build-msvc"
  rm -rf CMakeFiles ./*_autogen
  rm -f ./*_harness.exe colosseum-prev-live.exe
  rm -f ./*.obj ./*.ilk ./*.pdb ./*.lib ./*.exp CMakeCache.txt cmake_install.cmake ./*.cmake Makefile CTestTestfile.cmake 2>/dev/null || true )

echo "[5/6] bundle the Stremio stream-server next to the exe  <<< THE FIX"
DEST="$STAGE/native/build-msvc/stream_server"
mkdir -p "$DEST"
for f in stremio-runtime.exe server.js ffmpeg.exe ffprobe.exe \
         avcodec-58.dll avdevice-58.dll avfilter-7.dll avformat-58.dll avutil-56.dll \
         postproc-55.dll swresample-3.dll swscale-5.dll LICENSE.md; do
  cp "$STREMIO_SRC/$f" "$DEST/$f"
done
cat > "$DEST/NOTICE.txt" <<'EOF'
This folder contains Stremio Service (stremio-runtime.exe + server.js) and FFmpeg,
redistributed under GPL-2.0 — see LICENSE.md. Corresponding source:
  service wrapper : https://github.com/Stremio/stremio-service
  server runtime  : https://dl.strem.io/server/
Colosseum (MIT) and this GPL-2.0 component are separate programs communicating over
localhost HTTP; they are an aggregate, and Colosseum's own license is unaffected.
EOF

echo "[6/6] makensis -> $OUT"
# MSYS_NO_PATHCONV: stop Git Bash from rewriting the /D... define flags into bogus paths.
MSYS_NO_PATHCONV=1 "$MAKENSIS" \
    "/DSTAGE=$(cygpath -w "$STAGE")" "/DVERSION=$VERSION" \
    "/DOUTFILE=$(cygpath -w "$OUT")" "$(cygpath -w "$REPO/scripts/installer/colosseum.nsi")"

echo "DONE -> $OUT"
ls -la "$OUT" 2>/dev/null && du -h "$OUT" | cut -f1
