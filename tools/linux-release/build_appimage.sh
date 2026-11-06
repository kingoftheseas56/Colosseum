#!/usr/bin/env bash
# Same source snapshot and AppDir for the AppImage and companion Debian candidate.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
: "${CONDA_PREFIX:?Run through the pinned AppImage micromamba environment}"
: "${RUNNER_TEMP:?Use an isolated CI scratch directory}"
: "${APPIMAGE_TOOLS_DIR:?Set the directory of SHA256-verified packaging tools}"
work="$RUNNER_TEMP/colosseum-appimage-build"
out="$root/appimage-output"
[[ ! -e "$work" ]] || { echo 'Refusing to reuse an old build directory' >&2; exit 1; }
mkdir -p "$work" "$out"
commit="$(git -C "$root" rev-parse HEAD)"
[[ "$commit" == "${GITHUB_SHA:-$commit}" ]] || { echo 'Checkout differs from workflow source SHA' >&2; exit 1; }
[[ "$(getconf GNU_LIBC_VERSION)" == 'glibc 2.35' ]] || { echo 'Expected Ubuntu 22.04 build baseline' >&2; exit 1; }
export LD_LIBRARY_PATH="$CONDA_PREFIX/lib"
# Conda wrapper forces --define-prefix, invalid for Ubuntu multiarch .pc files.
export PKG_CONFIG=/usr/bin/pkg-config
[[ -x "$PKG_CONFIG" ]] || { echo "Host pkg-config missing" >&2; exit 1; }
export PKG_CONFIG_PATH="$CONDA_PREFIX/lib/pkgconfig:/usr/lib/x86_64-linux-gnu/pkgconfig:/usr/share/pkgconfig"
export LDFLAGS="-L$CONDA_PREFIX/lib -Wl,-rpath-link,$CONDA_PREFIX/lib"
python "$root/tools/linux-release/prepare_appimage_sdk.py" --prefix "$CONDA_PREFIX" \
  --overlay "$work/qt-sdk" --notices "$work/notices"

fetch_source() {
  local url="$1" digest="$2" file="$3"
  curl --fail --location --retry 3 "$url" -o "$file"
  printf '%s  %s\n' "$digest" "$file" | sha256sum --check --strict
}
# These are the same verified KDE source releases used by release-linux-deb.yml.
fetch_source https://download.kde.org/Attic/frameworks/6.15/extra-cmake-modules-6.15.0.tar.xz \
  f7cd022095a9e6bdbe5897720a24bfda81d211757b7c08b173061962bf2ee0b2 "$work/ecm.tar.xz"
fetch_source https://download.kde.org/stable/mpvqt/mpvqt-1.2.0.tar.xz \
  8660ad79c0d60fed77f29b36e1742841466af5405de702c81a121e6eeb625ebb "$work/mpvqt.tar.xz"
mkdir "$work/ecm-source" "$work/mpvqt-source"
tar -xf "$work/ecm.tar.xz" -C "$work/ecm-source" --strip-components=1
tar -xf "$work/mpvqt.tar.xz" -C "$work/mpvqt-source" --strip-components=1
cmake -S "$work/ecm-source" -B "$work/ecm-build" -G Ninja \
  -DCMAKE_INSTALL_PREFIX="$work/ecm-install" -DBUILD_TESTING=OFF
cmake --build "$work/ecm-build" --target install
cmake -S "$work/mpvqt-source" -B "$work/mpvqt-build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$work/mpvqt-install" \
  -DCMAKE_PREFIX_PATH="$CONDA_PREFIX;$work/ecm-install" -DBUILD_EXAMPLES=OFF -DBUILD_TESTING=OFF
cmake --build "$work/mpvqt-build" --target install --parallel 2
mkdir -p "$work/notices/AppImage-runtime"
cp "$APPIMAGE_TOOLS_DIR/runtime-license" "$work/notices/AppImage-runtime/LICENSE"
cp "$root/tools/linux-release/appimage-tools.json" "$work/notices/packaging-tools.json"
cp -a "$work/mpvqt-source/LICENSES" "$work/notices/MpvQt-LICENSES"
sha256sum "$work/ecm.tar.xz" "$work/mpvqt.tar.xz" | sed "s|$work/||g" > "$work/notices/kde-sources.sha256"
ffmpeg -L > "$work/notices/ffmpeg-license.txt" 2>&1
ffmpeg -buildconf > "$work/notices/ffmpeg-buildconf.txt" 2>&1
mpv --version > "$work/notices/mpv-version.txt"
openssl version -a > "$work/notices/openssl-version.txt"
dpkg-query -W -f='${binary:Package}\t${Version}\t${source:Package}\t${source:Version}\n' \
  > "$work/notices/host-package-sources.tsv"
mkdir "$work/notices/host-copyright"
while IFS= read -r -d '' file; do
  relative="${file#/usr/share/doc/}"
  mkdir -p "$work/notices/host-copyright/$(dirname "$relative")"
  cp "$file" "$work/notices/host-copyright/$relative"
done < <(find /usr/share/doc -name copyright -type f -print0)

cmake -S "$root/native" -B "$work/app-build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
  -DCOLOSSEUM_BUILD_PLAYER2=OFF -DCOLOSSEUM_PLAYER2_IN_APP=OFF \
  -DCOLOSSEUM_NO_ACCOUNT_EDITION=OFF -DCOLOSSEUM_UPDATE_TESTING=OFF \
  -DCOLOSSEUM_FERIA_IN_APP=OFF -DCOLOSSEUM_VINYL_IN_APP=OFF \
  -DCOLOSSEUM_ENABLE_VINYL_QUALIFICATION=OFF -DCOLOSSEUM_ENABLE_VINYL_TEST_FIXTURES=OFF \
  -DCOLOSSEUM_NO_COMPILER_CACHE=ON \
  -DCOLOSSEUM_CONTENT_API_URL=https://colosseum-content-api.colosseum-watchparty-relay.workers.dev \
  -DCMAKE_PREFIX_PATH="$CONDA_PREFIX;$work/ecm-install;$work/mpvqt-install" \
  -DMPVQT_PREFIX="$work/mpvqt-install"
cmake --build "$work/app-build" --target colosseum_runtime_ready --parallel 2
args=()
for plugin in "$work/qt-sdk/plugins/sqldrivers/"*.so; do
  [[ "$(basename "$plugin")" == libqsqlite.so ]] || args+=(--exclude-plugin "sqldrivers/$(basename "$plugin")")
done
# All Qt Quick 3D modules stay present. Never copy the old exclusion.
loaders="$("$PKG_CONFIG" --variable=gdk_pixbuf_moduledir gdk-pixbuf-2.0)"
query="$("$PKG_CONFIG" --variable=gdk_pixbuf_query_loaders gdk-pixbuf-2.0)"
[[ -d "$loaders" && -x "$query" ]] || { echo 'Matching GdkPixbuf loaders/query missing' >&2; exit 1; }
python "$root/tools/linux-release/assemble.py" \
  --source "$root" --build "$work/app-build" --qt "$work/qt-sdk" --stage "$work/AppDir" \
  --commit "$commit" --patchelf "$(command -v patchelf)" \
  --dep-lib "$work/mpvqt-install/lib" --dep-lib "$work/mpvqt-install/lib/x86_64-linux-gnu" \
  --binary "$CONDA_PREFIX/bin/ffmpeg" --binary "$CONDA_PREFIX/bin/ffprobe" --binary "$CONDA_PREFIX/bin/mpv" \
  --gdk-pixbuf-loaders-dir "$loaders" --gdk-pixbuf-query-loaders "$query" "${args[@]}"
cp -a "$work/notices" "$work/AppDir/licenses/runtime"
mkdir -p "$work/AppDir/usr/share"
for data in glib-2.0 icons; do
  if [[ -d "$CONDA_PREFIX/share/$data" ]]; then
    cp -a "$CONDA_PREFIX/share/$data" "$work/AppDir/usr/share/"
  fi
done
if [[ -d "$work/AppDir/usr/share/glib-2.0/schemas" ]]; then
  glib-compile-schemas "$work/AppDir/usr/share/glib-2.0/schemas"
fi
python "$root/tools/linux-release/check_package.py" "$work/AppDir" --report "$out/package-check.json"
python -c 'import json,sys;sys.path.insert(0,sys.argv[1]);from package_appimage import verify_abi;verify_abi(json.load(open(sys.argv[2])))' \
  "$root/tools/linux-release" "$out/package-check.json"
python "$root/scripts/linux_runtime_dependency_gate.py" --appdir "$work/AppDir" --require-dvr
version="$(python -c 'import json,sys;print(json.load(open(sys.argv[1]))["version"])' "$work/AppDir/BUILD-IDENTITY.json")"
# No hard-coded application version. Both packages contain this exact binary/source SHA.
python "$root/tools/linux-release/package_deb.py" --stage "$work/AppDir" \
  --output "$out/Colosseum-$version-linux-amd64.deb"
python "$root/tools/linux-release/package_appimage.py" --stage "$work/AppDir" --output-dir "$out" \
  --source-commit "$commit" --appimagetool "$APPIMAGE_TOOLS_DIR/appimagetool" --runtime "$APPIMAGE_TOOLS_DIR/runtime"
cp "$work/AppDir/BUILD-IDENTITY.json" "$out/BUILD-IDENTITY.json"
cp "$work/notices/manifest.json" "$out/dependency-manifest.json"
cp "$root/tools/linux-release/appimage-tools.json" "$out/packaging-tools.json"
git -C "$root" diff --exit-code -- native qml
printf 'version=%s\nimage=Colosseum-%s-x86_64.AppImage\n' "$version" "$version" >> "$GITHUB_OUTPUT"
