# Colosseum 1.1.8 Linux candidate build

Source baseline: `15fd79af6331f2b4726ab2350a69a0c896cdbecd`.
Build the candidate commit identified in its delivery record, not a moving branch.
The real desktop CMake entry point is `native/`.

## Verified build inputs

- Debian GNU/Linux 13.6 x86_64, GCC 14.2.0, glibc 2.41, CMake 3.31.6, Ninja.
- Full official Qt **6.11.1** GCC SDK, including base, declarative, SVG, tools,
  translations, Wayland, WebEngine, WebChannel, WebSockets, Positioning,
  Multimedia, ImageFormats and TaskTree. The SDK includes headers, CMake
  configuration, moc/rcc, QML tools, libraries, plugins and WebEngine resources.
- Extra CMake Modules **6.15.0**.
- MpvQt **1.2.0**, official archive SHA-256
  `8660ad79c0d60fed77f29b36e1742841466af5405de702c81a121e6eeb625ebb`,
  built against that Qt SDK, not a library compiled with another Qt minor.
- Debian libmpv **0.40.0**, libtorrent-rasterbar **2.0.11**, Boost **1.83**,
  OpenSSL, GL/EGL/Vulkan development files and libsecret development files.
- FFmpeg/ffprobe and their matching media libraries are required for packaging.

Official sources: https://download.qt.io/online/qtsdkrepository/,
https://download.kde.org/stable/mpvqt/mpvqt-1.2.0.tar.xz,
https://download.kde.org/Attic/frameworks/6.15/extra-cmake-modules-6.15.0.tar.xz,
https://deb.debian.org/debian/.
The delivery retains exact downloaded package versions and checksums.

The normal `.github/workflows/desktop-ci.yml` documents Ubuntu 24.04 dependency
installation and ECM/MpvQt builds. This candidate was built on Debian 13.6;
Ubuntu compatibility must not be inferred from that workflow.

## Build

Set `QT_ROOT_DIR` to the complete SDK, `MPVQT_PREFIX` to the matching MpvQt install,
and `CMAKE_PREFIX_PATH` to additional dependency prefixes. Make Qt's `lib` and
MpvQt's library directory available to their build-time executables. A normal
system package installation supplies native headers/libraries; extracted Debian
packages additionally require `PKG_CONFIG_PATH` and `PKG_CONFIG_SYSROOT_DIR`.
Then run `bash tools/linux-release/build_release.sh` from the checkout.
`COLOSSEUM_BUILD_DIR` and `BUILD_JOBS` are optional overrides.

`-fPIC` avoids data COPY relocations when using the official Qt SDK on this
Debian toolchain. Player 2 is Windows-only and disabled; the Linux app uses mpv.
The build writes `qml-build.manifest`; package it next to the executable along
with the unchanged QML tree. Do not bypass the normal startup fingerprint gate.

The development environment is not the portable package. Test a clean extracted
archive through its AppRun, with a disposable profile and its own dependency
closure. A successful build alone does not qualify rendering, playback, network,
private providers, DRM, Windows, or release readiness.
