#!/usr/bin/env bash
set -euo pipefail
: "${SOURCE:?}" "${BUILD:?}" "${DEPS:?}" "${EVIDENCE:?}" "${QT_ROOT_DIR:?}"
mkdir -p "$EVIDENCE" "$DEPS"
exec > >(tee "$EVIDENCE/build.log") 2>&1
test "$(git -C "$SOURCE" rev-parse HEAD)" = 36498faa7cbcf1c6f47bdea042806b1aa8135f7e
test -z "$(git -C "$SOURCE" status --porcelain --untracked-files=all)"
git -C "$SOURCE" ls-remote origin refs/tags/v1.1.7 | tee "$EVIDENCE/remote-tag.txt"
grep -q '^36498faa7cbcf1c6f47bdea042806b1aa8135f7e[[:space:]]' "$EVIDENCE/remote-tag.txt"
# Apply the reviewed source delta only after validating the pristine pinned tag.
python "$(dirname "${BASH_SOURCE[0]}")/source_patches.py" --source "$SOURCE" --evidence "$EVIDENCE"
. /etc/os-release
test "$ID:$VERSION_ID:$(uname -m)" = ubuntu:24.04:x86_64
test "$("$QT_ROOT_DIR/bin/qmake" -query QT_VERSION)" = 6.11.1
cmake --version
ninja --version
c++ --version
ccache --version
clang-tidy-20 --version | tee "$EVIDENCE/clang-version.txt"
grep -q '20.1.2' "$EVIDENCE/clang-version.txt"
pkg-config --modversion mpv
dpkg-query -W > "$EVIDENCE/build-packages.txt"
git -C "$SOURCE" rev-parse HEAD > "$EVIDENCE/source-sha.txt"
ccache --max-size=1G

# Exact versions, URLs and archive digests from the tagged linux-desktop job.
curl --fail --location --retry 3 --connect-timeout 30 --max-time 600 \
  https://download.kde.org/Attic/frameworks/6.15/extra-cmake-modules-6.15.0.tar.xz -o "$DEPS/ecm.tar.xz"
echo "f7cd022095a9e6bdbe5897720a24bfda81d211757b7c08b173061962bf2ee0b2  $DEPS/ecm.tar.xz" | sha256sum -c --strict
mkdir "$DEPS/ecm-source"
tar -xf "$DEPS/ecm.tar.xz" -C "$DEPS/ecm-source" --strip-components=1
cmake -S "$DEPS/ecm-source" -B "$DEPS/ecm-build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=OFF -DCMAKE_INSTALL_PREFIX="$DEPS/ecm-install"
cmake --build "$DEPS/ecm-build" --target install --parallel 2

curl --fail --location --retry 3 --connect-timeout 30 --max-time 600 \
  https://download.kde.org/stable/mpvqt/mpvqt-1.2.0.tar.xz -o "$DEPS/mpvqt.tar.xz"
echo "8660ad79c0d60fed77f29b36e1742841466af5405de702c81a121e6eeb625ebb  $DEPS/mpvqt.tar.xz" | sha256sum -c --strict
mkdir "$DEPS/mpvqt-source"
tar -xf "$DEPS/mpvqt.tar.xz" -C "$DEPS/mpvqt-source" --strip-components=1
cmake -S "$DEPS/mpvqt-source" -B "$DEPS/mpvqt-build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_EXAMPLES=OFF -DCMAKE_INSTALL_PREFIX="$DEPS/mpvqt-install" \
  -DCMAKE_PREFIX_PATH="$QT_ROOT_DIR;$DEPS/ecm-install"
cmake --build "$DEPS/mpvqt-build" --target install --parallel 2

cmake -S "$SOURCE/native" -B "$BUILD" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DCOLOSSEUM_BUILD_PLAYER2=OFF -DCOLOSSEUM_PLAYER2_IN_APP=OFF \
  -DCOLOSSEUM_NO_COMPILER_CACHE=ON -DCMAKE_C_COMPILER_LAUNCHER=ccache \
  -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
  -DCMAKE_PREFIX_PATH="$QT_ROOT_DIR;$DEPS/ecm-install;$DEPS/mpvqt-install" \
  -DMPVQT_PREFIX="$DEPS/mpvqt-install"
cmake --build "$BUILD" --parallel 2
cp "$BUILD/CMakeCache.txt" "$EVIDENCE/"
