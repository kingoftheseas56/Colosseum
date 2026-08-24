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
if [[ ! "$VERSION" =~ ^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$ ]]; then
  echo "version must be canonical X.Y.Z: $VERSION"
  exit 1
fi
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

if [ -n "$(git -C "$REPO" status --porcelain --untracked-files=all)" ]; then
  echo "refusing to package dirty source tree"
  exit 1
fi
EXPECTED_TAG="v$VERSION"
HEAD_TAG="$(git -C "$REPO" describe --exact-match --tags HEAD 2>/dev/null || true)"
if [ "$HEAD_TAG" != "$EXPECTED_TAG" ]; then
  echo "refusing tag mismatch: HEAD=$HEAD_TAG expected=$EXPECTED_TAG"
  exit 1
fi

[ -x "$MAKENSIS" ] || { echo "makensis not found at $MAKENSIS"; exit 1; }
[ -f "$STREMIO_SRC/stremio-runtime.exe" ] || { echo "Stremio source missing: $STREMIO_SRC"; exit 1; }
[ -f "$BUILD_DIR/colosseum.exe" ] || { echo "build missing: $BUILD_DIR/colosseum.exe"; exit 1; }
[ -f "$BUILD_DIR/CMakeCache.txt" ] || { echo "clean CMake build cache missing: $BUILD_DIR/CMakeCache.txt"; exit 1; }
grep -Eq '^CMAKE_BUILD_TYPE:STRING=Release$' "$BUILD_DIR/CMakeCache.txt" \
  || { echo "BUILD_DIR is not a Release build: $BUILD_DIR"; exit 1; }

# The test-key updater build is intentionally not shippable.  Check the cache
# before archiving or stripping the build so a release can never embed the
# Lanista-only trust root by accident.
if [ -f "$BUILD_DIR/CMakeCache.txt" ] \
  && grep -Eq '^COLOSSEUM_UPDATE_TESTING:BOOL=ON$' "$BUILD_DIR/CMakeCache.txt"; then
  echo "refusing to package COLOSSEUM_UPDATE_TESTING=ON build"
  exit 1
fi

# Data-vault adoption (2026-08-22): the app now fetches all four catalogue dbs
# (mal, tankoban, comics, imdb) from the public kingoftheseas56/Colosseum-Data GitHub
# release into AppData on first launch (CatalogVaultClient). The installer no longer
# carries any catalogue db — nothing to guard or overlay here.

echo "[1/6] clean stage -> $STAGE"
rm -rf "$STAGE"; mkdir -p "$STAGE"

echo "[2/6] source tree at HEAD (tracked files)"
git -C "$REPO" archive --format=tar HEAD | tar -x -C "$STAGE"

echo "[3/6] overlay windeployqt runtime from $BUILD_DIR"
mkdir -p "$STAGE/native/build-msvc"
cp -r "$BUILD_DIR/." "$STAGE/native/build-msvc/"
# Felt-speed runtime sentinels: an installer without these files is not shippable.
for sentinel in \
  "$STAGE/native/build-msvc/colosseum.exe" \
  "$STAGE/native/build-msvc/qml-build.manifest" \
  "$STAGE/native/build-msvc/imageformats/qwebp.dll" \
  "$STAGE/native/build-msvc/platforms/qwindows.dll" \
  "$STAGE/native/build-msvc/Qt6Core.dll"; do
  [ -f "$sentinel" ] || { echo "runtime sentinel missing: $sentinel"; exit 1; }
done

# Function 0001 bootstrap integrity: the executable's build manifest must describe
# the exact qml/ tree staged from the release tag. This catches a clean native build
# accidentally packaged with QML from a different commit before NSIS can ship it.
STAGE_QML_MANIFEST="$STAGE/.qml-stage.manifest"
cmake "-DQML_ROOT=$STAGE/qml" \
      "-DOUTPUT_FILE=$STAGE_QML_MANIFEST" \
      -P "$STAGE/native/bootstrap/write_qml_build_manifest.cmake"
if ! cmp -s "$STAGE/native/build-msvc/qml-build.manifest" "$STAGE_QML_MANIFEST"; then
  echo "refusing native/QML build mismatch: qml-build.manifest does not match staged qml/"
  diff -u "$STAGE/native/build-msvc/qml-build.manifest" "$STAGE_QML_MANIFEST" || true
  exit 1
fi
rm -f "$STAGE_QML_MANIFEST"

echo "[4/6] strip build intermediates (keeps ALL runtime: dlls, platforms/, imageformats/, tls/, translations/, resources/, qml/, tools/, QtWebEngineProcess.exe, stream_server/)"
( cd "$STAGE/native/build-msvc"
  rm -rf CMakeFiles Testing artifacts
  rm -rf ./*_autogen
  rm -f ./*_harness.exe ./*harness*.exe tst_*.exe ./*_test*.exe colosseum-capture.exe colosseum-prev-live.exe
  rm -f ./_a0_* ./_slice* ./_engine_harness* ./*.log
  rm -f ./*.ninja_log ./*.ninja_deps ./*.ninja*
  rm -f ./*.obj ./*.ilk ./*.pdb ./*.lib ./*.exp CMakeCache.txt cmake_install.cmake ./*.cmake Makefile CTestTestfile.cmake 2>/dev/null || true )

echo "[4b/6] prune source-tree dirs the installed app never reads at runtime"
# The installed exe's own resource root is $STAGE itself (main.cpp cdUp()s twice off
# applicationDirPath to native/build-msvc/../.. and treats that as cwd for qml/, data/).
# Grepped native/*.cpp + qml/**/*.qml,js for hardcoded "tests/", "docs/", "agents/",
# "scripts/comics_brain", "release/presentation" references: the only hits are in
# native/tools/lanista.cpp (the standalone `lanista` dev CLI, never linked into the
# `colosseum` target — see native/CMakeLists.txt) and a doc-comment string literal in
# VaultForensics.cpp that is never opened as a path. Nothing in the shipped app reads
# these trees at runtime, so they are safe to drop from the stage.
rm -rf "$STAGE/tests" "$STAGE/docs" "$STAGE/agents" \
       "$STAGE/scripts/comics_brain" "$STAGE/release/presentation"

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

echo "[5b/6] stage weight (top-level, before makensis) -- watch this for future bloat"
du -sh "$STAGE"/*/ "$STAGE"/* 2>/dev/null | sort -rh | head -20 || true

echo "[6/6] makensis -> $OUT"
# MSYS_NO_PATHCONV: stop Git Bash from rewriting the /D... define flags into bogus paths.
MSYS_NO_PATHCONV=1 "$MAKENSIS" \
    "/DSTAGE=$(cygpath -w "$STAGE")" "/DVERSION=$VERSION" \
    "/DOUTFILE=$(cygpath -w "$OUT")" "$(cygpath -w "$REPO/scripts/installer/colosseum.nsi")"

echo "DONE -> $OUT"
EXPECTED_NAME="Colosseum-$VERSION-setup.exe"
[ "$(basename "$OUT")" = "$EXPECTED_NAME" ] || { echo "installer filename drift: $OUT"; exit 1; }
[ -f "$OUT" ] || { echo "installer output missing: $OUT"; exit 1; }
INSTALLER_BYTES="$(wc -c < "$OUT" | tr -d '[:space:]')"
# Size gate (1.1.2 lesson): that release shipped 647MB of build garbage vs 1.1.1's 211MB.
# Refuse loudly rather than publish a bloated installer nobody eyeballed.
MAX_INSTALLER_BYTES=$((300 * 1024 * 1024))
if [ "$INSTALLER_BYTES" -gt "$MAX_INSTALLER_BYTES" ]; then
  echo "refusing oversized installer: $INSTALLER_BYTES bytes (limit $MAX_INSTALLER_BYTES)"
  exit 1
fi
INSTALLER_SHA256="$(sha256sum "$OUT" | awk '{print $1}')"
printf 'INSTALLER_PATH=%s\nINSTALLER_BYTES=%s\nINSTALLER_SHA256=%s\n' \
  "$OUT" "$INSTALLER_BYTES" "$INSTALLER_SHA256"
