#!/bin/bash
# Developer/source build only. Run: bash build-m1-preview.sh /path/to/Colosseum EXACT_SHA
set -euo pipefail
fail() { printf 'ERROR: %s\n' "$*" >&2; exit 1; }
[[ $# == 2 ]] || fail 'Usage: bash build-m1-preview.sh REPOSITORY EXACT_40_CHARACTER_SHA'
repo=$(cd "$1" && pwd -P)
pin=$2
[[ $pin =~ ^[0-9a-f]{40}$ ]] || fail 'An exact lowercase 40-character Git SHA is required'
[[ $(uname -s) == Darwin && $(uname -m) == arm64 ]] || fail 'Run natively on an arm64 Mac (not Rosetta)'
xcode-select -p >/dev/null || fail 'Install Command Line Tools: xcode-select --install'
command -v brew >/dev/null || fail 'Install native Homebrew using https://brew.sh/ then rerun'
[[ $(brew --prefix) == /opt/homebrew ]] || fail 'Expected native Apple-silicon Homebrew at /opt/homebrew'
[[ $(git -C "$repo" rev-parse HEAD) == "$pin" ]] || fail 'Repository HEAD does not match requested source SHA'
[[ -z $(git -C "$repo" status --porcelain --untracked-files=normal) ]] || fail 'Source changes or untracked files exist; use a separate clean checkout'
for tool in cmake ninja pkg-config curl tar shasum lipo; do
    command -v "$tool" >/dev/null || fail "Missing $tool; install prerequisites from the handoff"
done
root="$HOME/Library/Caches/Colosseum/m1-preview"
mkdir -p "$root"
work=$(mktemp -d "$root/${pin}.XXXXXX")
log="$work/build.log"
exec > >(tee -a "$log") 2>&1
trap 'result=$?; printf "\nBUILD_EXIT=%s\nSOURCE_SHA=%s\nWORK=%s\nLOG=%s\n" "$result" "$pin" "$work" "$log"' EXIT
printf 'SOURCE_SHA=%s\nSOURCE=%s\nWORK=%s\n' "$pin" "$repo" "$work"
sw_vers
xcodebuild -version 2>/dev/null || xcrun clang --version
cmake --version
brew --version
formulas=(cmake ninja pkgconf libtorrent-rasterbar boost openssl@3 mpv ffmpeg)
[[ -n ${COLOSSEUM_QT_PREFIX:-} ]] || formulas+=(python)
for formula in "${formulas[@]}"; do
    installed=$(brew list --versions "$formula") || fail "Missing formula: brew install $formula"
    [[ -n $installed ]] || fail "Missing formula: brew install $formula"
    printf '%s\n' "$installed" | tee -a "$work/dependencies.txt"
done
# Official Qt is coherent and universal (x86_64 + arm64); never mix rolling brew Qt kegs.
if [[ -n ${COLOSSEUM_QT_PREFIX:-} ]]; then
    qtbase=$(cd "$COLOSSEUM_QT_PREFIX" && pwd -P)
else
    command -v python3 >/dev/null || fail 'Install Python 3.10+: brew install python'
    python3 -c 'import sys; assert sys.version_info >= (3,10), "Python 3.10+ required"'
    python3 -m venv "$work/aqt-venv"
    py="$work/aqt-venv/bin/python"
    "$py" -m pip install 'git+https://github.com/miurahr/aqtinstall.git@8c3695d4a4e1ceabf6a74dc6c79681656dc6b74b'
    "$py" -m pip freeze > "$work/aqt-python-dependencies.txt"
    "$py" -m aqt list-qt mac desktop --arch 6.11.1
    "$py" -m aqt list-qt mac desktop --modules 6.11.1 clang_64
    "$py" -m aqt install-qt mac desktop 6.11.1 clang_64 -O "$work/qt" \
        -m qtwebengine qtwebchannel qtwebsockets qtpositioning qtserialport qtimageformats
    qtbase="$work/qt/6.11.1/macos"
fi
[[ -x "$qtbase/bin/qmake" ]] || fail "Official Qt SDK qmake missing under $qtbase"
[[ $("$qtbase/bin/qmake" -query QT_VERSION) == 6.11.1 ]] || fail 'Use one coherent Qt 6.11.1 SDK'
lipo -verify_arch arm64 "$qtbase/lib/QtCore.framework/QtCore"
for module in QtQuick/Dialogs QtQuick/Controls QtWebEngine; do
    [[ -f "$qtbase/qml/$module/qmldir" ]] || fail "Full Qt QML SDK missing $module (PySide is insufficient)"
    printf 'QML_MODULE=%s\n' "$qtbase/qml/$module/qmldir"
done
mkdir "$work/sdk-probe"
cat > "$work/sdk-probe/CMakeLists.txt" <<'CMAKE'
cmake_minimum_required(VERSION 3.16)
project(ColosseumSdkProbe LANGUAGES CXX)
find_package(Qt6 6.11.1 EXACT REQUIRED COMPONENTS Quick Qml Gui Network Sql Concurrent
    WebSockets WebEngineQuick WebChannel Positioning SerialPort Svg)
CMAKE
cmake -S "$work/sdk-probe" -B "$work/sdk-probe-build" -G Ninja \
    -DCMAKE_PREFIX_PATH="$qtbase" -DCMAKE_OSX_ARCHITECTURES=arm64
printf 'QT_PREFIX=%s\n' "$qtbase"
export PKG_CONFIG_PATH="$(brew --prefix mpv)/lib/pkgconfig:$(brew --prefix openssl@3)/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
pkg-config --modversion mpv
jobs=${COLOSSEUM_BUILD_JOBS:-2}
[[ $jobs =~ ^[1-9][0-9]*$ ]] || fail 'COLOSSEUM_BUILD_JOBS must be a positive integer'
deps="$work/deps"
fetch_source() {
    local name=$1 url=$2 sha=$3
    curl --fail --location --retry 3 "$url" -o "$work/$name.tar.xz"
    printf '%s  %s\n' "$sha" "$work/$name.tar.xz" | shasum -a 256 -c -
    mkdir "$work/$name-source"
    tar -xf "$work/$name.tar.xz" -C "$work/$name-source" --strip-components=1
}
# Same exact KDE source pins as the reviewed Linux build; no global dependency install.
fetch_source ecm https://download.kde.org/Attic/frameworks/6.15/extra-cmake-modules-6.15.0.tar.xz \
    f7cd022095a9e6bdbe5897720a24bfda81d211757b7c08b173061962bf2ee0b2
cmake -S "$work/ecm-source" -B "$work/ecm-build" -G Ninja \
    -DBUILD_TESTING=OFF -DCMAKE_INSTALL_PREFIX="$deps"
cmake --build "$work/ecm-build" --target install --parallel "$jobs"
fetch_source mpvqt https://download.kde.org/stable/mpvqt/mpvqt-1.2.0.tar.xz \
    8660ad79c0d60fed77f29b36e1742841466af5405de702c81a121e6eeb625ebb
cmake -S "$work/mpvqt-source" -B "$work/mpvqt-build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 -DBUILD_TESTING=OFF \
    -DCMAKE_INSTALL_PREFIX="$deps" -DCMAKE_PREFIX_PATH="$qtbase;$deps" \
    -DQT_ADDITIONAL_PACKAGES_PREFIX_PATH="$qtbase"
cmake --build "$work/mpvqt-build" --target install --parallel "$jobs"
cmake -S "$repo/native" -B "$work/app" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_MACOSX_BUNDLE=OFF \
    -DBUILD_TESTING=OFF -DCOLOSSEUM_BUILD_PLAYER2=OFF \
    -DCMAKE_PREFIX_PATH="$qtbase;$deps;$(brew --prefix libtorrent-rasterbar);$(brew --prefix boost);$(brew --prefix openssl@3)" \
    -DQT_ADDITIONAL_PACKAGES_PREFIX_PATH="$qtbase" \
    -DOPENSSL_ROOT_DIR="$(brew --prefix openssl@3)" -DMPVQT_PREFIX="$deps" \
    -DCOLOSSEUM_FERIA_IN_APP=OFF -DCOLOSSEUM_VINYL_IN_APP=OFF \
    -DCOLOSSEUM_PLAYER2_IN_APP=OFF -DCOLOSSEUM_NO_ACCOUNT_EDITION=OFF \
    -DCOLOSSEUM_UPDATE_TESTING=OFF -DCOLOSSEUM_ENABLE_VINYL_QUALIFICATION=OFF \
    -DCOLOSSEUM_ENABLE_VINYL_TEST_FIXTURES=OFF \
    -DCOLOSSEUM_CONTENT_API_URL=https://colosseum-content-api.colosseum-watchparty-relay.workers.dev
cmake --build "$work/app" --target colosseum_runtime_ready --parallel "$jobs"
[[ -x "$work/app/colosseum" ]] || fail 'Expected bare developer executable was not produced'
file "$work/app/colosseum"
otool -L "$work/app/colosseum"
printf '\nCOMPILED_EXECUTABLE=%s\n' "$work/app/colosseum"
printf 'Developer launch (manual, no account qualification):\ncd %q && COLOSSEUM_DEV=1 QT_FORCE_STDERR_LOGGING=1 QML_DISABLE_DISK_CACHE=1 %q qml/Main.qml\n' "$repo" "$work/app/colosseum"
printf 'Compilation complete; startup, playback, quit, accounts and distributable .app remain unverified.\n'
