# Build Colosseum on macOS

This guide covers a source build of the desktop app on Apple-silicon Macs using Homebrew for the toolchain and Qt module set. It follows the same explicit dependency-prefix shape as the [Windows guide](windows.md).

## Requirements

Use an arm64 Mac with:

- macOS 14 or newer (verified on macOS 26, AppleClang 21);
- Xcode Command Line Tools;
- CMake 3.16 or newer, Ninja, and pkg-config;
- Qt 6.11.x from Homebrew's module formulas: `qtbase`, `qtdeclarative`, `qtwebengine`, `qtwebchannel`, `qtwebsockets`, `qtimageformats`, `qtsvg`, `qtpositioning`;
- `libmpv` (the `mpv` formula) for the MpvQt player path, and `ffmpeg` on `PATH` for runtime media features;
- libtorrent-rasterbar, Boost, and OpenSSL 3;
- MpvQt built from the KDE source tarball — Homebrew does not package it.

Catalogue databases are deployment artifacts, exactly as on Windows: a source checkout runs with local data; a missing catalogue leaves the corresponding lane dormant.

## Install dependencies

```sh
brew install cmake ninja pkgconf extra-cmake-modules \
  qtbase qtdeclarative qtwebengine qtwebchannel qtwebsockets \
  qtimageformats qtsvg qtpositioning \
  libtorrent-rasterbar boost openssl@3 mpv ffmpeg
```

## Build MpvQt

Homebrew has no MpvQt formula, so build the KDE tarball into the prefix `native/CMakeLists.txt` already expects off Windows (`$HOME/.local/colosseum-deps`):

```sh
mkdir -p ~/.local/colosseum-deps/src && cd ~/.local/colosseum-deps/src
curl -sSLO https://download.kde.org/stable/mpvqt/mpvqt-1.1.1.tar.xz
# sha256: bdd1ea69338cf3017f628a886218b8c185ca24e8257f03207a3cf1bbb51e3d32
tar xf mpvqt-1.1.1.tar.xz
cmake -S mpvqt-1.1.1 -B mpvqt-build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="/opt/homebrew/opt/qtbase;/opt/homebrew/opt/extra-cmake-modules" \
  -DQT_ADDITIONAL_PACKAGES_PREFIX_PATH="/opt/homebrew/opt/qtdeclarative" \
  -DCMAKE_INSTALL_PREFIX="$HOME/.local/colosseum-deps"
cmake --build mpvqt-build && cmake --install mpvqt-build
```

## The split-Qt prefix rule

Homebrew ships each Qt module as its own keg, and Qt's CMake component resolution requires that split to be expressed a specific way:

- `qtbase` goes in `CMAKE_PREFIX_PATH`;
- every other module keg goes **only** in `QT_ADDITIONAL_PACKAGES_PREFIX_PATH`.

Putting a module keg (for example `qtdeclarative`) into `CMAKE_PREFIX_PATH` breaks Qt component resolution with misleading `Qt6 ... could not be found` errors from inside module config files. `libtorrent-rasterbar`, `boost`, and `openssl@3` are ordinary `CMAKE_PREFIX_PATH` entries.

## Configure

From the repository root:

```sh
cmake -S native -B native/build-mac -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="/opt/homebrew/opt/qtbase;/opt/homebrew/opt/libtorrent-rasterbar;/opt/homebrew/opt/boost;/opt/homebrew/opt/openssl@3" \
  -DQT_ADDITIONAL_PACKAGES_PREFIX_PATH="/opt/homebrew/opt/qtdeclarative;/opt/homebrew/opt/qtwebengine;/opt/homebrew/opt/qtwebchannel;/opt/homebrew/opt/qtwebsockets;/opt/homebrew/opt/qtimageformats;/opt/homebrew/opt/qtsvg;/opt/homebrew/opt/qtpositioning" \
  -DOPENSSL_ROOT_DIR="/opt/homebrew/opt/openssl@3"
```

Player 2 stays off: it is Windows-only (D3D11/WASAPI) and the configure fails by design if enabled on this platform. The mpv/MpvQt player is the shipped player.

## Build

```sh
cmake --build native/build-mac --target colosseum
```

This produces `native/build-mac/colosseum`. All load commands resolve to Homebrew kegs and the MpvQt prefix through absolute install paths, so the bare executable runs without `DYLD_LIBRARY_PATH` adjustments.

## Run from the source tree

The same development launch the Windows guide describes, without `dev.bat`:

```sh
COLOSSEUM_DEV=1 QT_FORCE_STDERR_LOGGING=1 QML_DISABLE_DISK_CACHE=1 \
  ./native/build-mac/colosseum qml/Main.qml
```

This is a development launch, not an installed-release simulation. There is no macOS bundling/deployment step (`macdeployqt`) in the tree yet.

## Verification

A build is only compilation evidence. State the strongest level you actually reached, as with any other platform.

The state this guide was verified at: the `colosseum` target builds clean (arm64, AppleClang 21, Qt 6.11.1, libtorrent-rasterbar 2.1.1, Boost 1.92, OpenSSL 3, MpvQt 1.1.1), and a development launch boots the QML shell with the logging, connection-concierge, webp-decoder, and catalogue-vault lanes reporting ready, including live poster fetching.

Known macOS boundaries at this stage:

- **Exit-time crash (open defect)**: quitting a session can fault in `Colosseum::WatchParty::UiController::~UiController()` while `QGuiApplication` tears down its children — a use-after-free in teardown order, not a build defect. Under investigation; a symbolized backtrace is on file.
- **Windows-only lanes are dormant**, as on Linux: account credential storage (Windows Credential Manager) and the sensitive-clipboard lane no-op rather than using a Keychain; reveal-in-file-manager returns false; the Stremio stream-server runtime and the self-update/install flow are Windows-shaped and disabled in dev builds.
- The harness/test targets in `tests/` now configure on this platform but are not yet exercised on it.

The repository's public verification references are indexed from [../README.md](../README.md).
