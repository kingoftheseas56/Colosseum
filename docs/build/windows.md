# Build Colosseum on Windows

This guide covers a normal source build of the Windows desktop app using explicit, portable dependency locations.

## Requirements

Use Windows 10 or 11 with:

- Visual Studio 2022 C++ Build Tools with the MSVC desktop toolchain;
- CMake 3.16 or newer;
- Ninja;
- Qt 6.11.1, MSVC 2022 64-bit, with Quick, QML, Network, GUI, SQL, Concurrent, WebEngineQuick, WebChannel, and WebSockets;
- MpvQt built for the same MSVC toolchain;
- a libmpv development package containing `include/mpv/client.h` and `lib/mpv.lib`;
- libtorrent-rasterbar, Boost, and OpenSSL built for the same architecture/toolchain.

Python 3 is useful for repository verification and catalogue tooling, but it is not required merely to compile the desktop executable. Release/runtime media features use the native Colosseum streaming runtime together with the declared Qt, MpvQt, and libmpv dependencies; the public release packager stages those runtime dependencies separately from compilation.

Catalogue databases are deployment artifacts rather than ordinary Git source. A source checkout can use local data database files; when a supported catalogue is absent, the catalogue-vault path can fetch the published database into per-user AppData and wake the corresponding surface when it becomes ready.

## Choose dependency prefixes

The examples below use `C:\colosseum-deps` only as a neutral illustration. Your dependencies may live anywhere. Pass their real paths explicitly when configuring the build.

Open a Visual Studio 2022 Developer PowerShell, change to the repository root, then define:
```powershell
$QtRoot = 'C:\Qt\6.11.1\msvc2022_64'
$Deps = 'C:\colosseum-deps'
$MpvQt = "$Deps\mpvqt"
$LibMpv = "$Deps\libmpv"
$Libtorrent = "$Deps\libtorrent"
$Boost = "$Deps\boost"
$OpenSsl = "$Deps\openssl"
```

Before configuring, verify that those prefixes contain the headers and libraries described above. All dependencies must match the 64-bit MSVC toolchain used by Qt.

## Configure

From the repository root:

```powershell
cmake -S native -B native/build-msvc -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  "-DCMAKE_PREFIX_PATH=$QtRoot" `
  "-DMPVQT_PREFIX=$MpvQt" `
  "-DLIBMPV_PREFIX=$LibMpv" `
  "-DLIBTORRENT_ROOT=$Libtorrent" `
  "-DBOOST_ROOT=$Boost" `
  "-DOPENSSL_MSVC_ROOT=$OpenSsl"
```
If CMake reports a missing package, fix the dependency prefix or toolchain mismatch before building.

Accounts remain optional for a normal desktop build. The public source intentionally compiles with no production account-service endpoint; local testing can set `COLOSSEUM_ACCOUNT_SERVICE_URL` at runtime.

## Build

```powershell
cmake --build native/build-msvc --target colosseum
```

A successful compile produces `native/build-msvc/colosseum.exe`.

## Run from the source tree

For the live-QML development loop, set the same environment that `dev.bat` uses without depending on its machine-specific Qt path:

```powershell
$env:PATH = "$QtRoot\bin;$env:PATH"
$env:COLOSSEUM_DEV = '1'
$env:QT_FORCE_STDERR_LOGGING = '1'
$env:QML_DISABLE_DISK_CACHE = '1'
& .\native\build-msvc\colosseum.exe .\qml\Main.qml
```

This is a development launch, not an installed-release simulation.

## Deploy the Qt runtime for a standalone local build

`native/deploy-runtime.bat` currently assumes Qt is installed at `C:\Qt\6.11.1\msvc2022_64`. If your Qt prefix differs, run the equivalent commands directly:

```powershell
& "$QtRoot\bin\windeployqt.exe" --qmldir qml native\build-msvc\colosseum.exe
New-Item -ItemType Directory -Force native\build-msvc\imageformats | Out-Null
Copy-Item "$QtRoot\plugins\imageformats\qwebp.dll" native\build-msvc\imageformats\qwebp.dll
```

The release installer additionally bundles the Stremio stream-server runtime and passes stricter clean-tree, exact-tag, signing, and packaging gates. Those maintainer release steps are intentionally separate from a contributor source build.

## Verification

A build is only compilation evidence. Run the tests or assembled-app journey relevant to the change you made, then report exactly what passed.

The repository's public verification references are indexed from [../README.md](../README.md). For contribution expectations, see [../../CONTRIBUTING.md](../../CONTRIBUTING.md).

## Player 2

Player 2 is experimental and not the default player. To include its in-app path, configure with `-DCOLOSSEUM_PLAYER2_IN_APP=ON` and set `COLOSSEUM_PLAYER2=1` when launching. Normal builds should use the default mpv/MpvQt player path.
