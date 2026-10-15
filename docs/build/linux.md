# Linux build and runtime dependencies

Colosseum's Linux beta target is Ubuntu 24.04 / Linux Mint 22.x on x86_64. The first distribution format remains the Arc 17 AppImage candidate; this file defines its dependency boundary but does not claim that an AppImage has already passed M5 acceptance.

## Runtime policy

Player 1 uses `libmpv` through MpvQt. The standalone `mpv` executable is a separate helper used by Live/DVR recording.

For the beta:

- `libxcb-cursor.so.0` is a hard GUI-launch dependency for the Qt XCB platform plugin. A distributable AppDir must carry it; source-tree development hosts can provide it with Ubuntu package `libxcb-cursor0`.
- `libmpv.so.2` is a hard Player 1 dependency. A distributable AppDir must account for it rather than depend on a developer prefix.
- standalone `mpv` is optional. If it is absent, DVR must fail closed with an explicit missing-mpv error while Player 1 remains available through `libmpv`.
- a package that advertises working DVR must bundle/provision an executable `usr/bin/mpv` and pass the runtime gate with `--require-dvr`.

This deliberately keeps missing `mpv` from becoming a false Player 1 failure.

## Ubuntu/Mint dependency set

The Linux desktop CI/source-build baseline uses these build dependencies:

```text
build-essential ninja-build pkg-config ccache clang-tidy-20
libmpv-dev libtorrent-rasterbar-dev libboost-all-dev libssl-dev libsecret-1-dev
libgl1-mesa-dev libegl1-mesa-dev libxkbcommon-dev libxcb-cursor0
ffmpeg p7zip-full libarchive-tools
```

Runtime feature helpers used by the current Linux port are `ffmpeg`/`ffprobe`, `bsdtar` or `7z` for comic archives, and `xdg-open` from `xdg-utils` for desktop reveal. Stremio runtime assets are a separate Theatre package-content responsibility and must be checked by the final package acceptance run.

`libxcb-cursor0` and `libmpv2` are the minimum host packages exercised by the runtime dependency gate on Ubuntu/Mint when those libraries are not bundled in an AppDir. `mpv` is intentionally not a minimum host requirement under the fail-closed DVR policy.

## Reproducible runtime checks

Check the current host:

```bash
python3 scripts/linux_runtime_dependency_gate.py
```

Check an AppDir before producing an AppImage:

```bash
python3 scripts/linux_runtime_dependency_gate.py --appdir /path/to/AppDir
```

If the package claims Live/DVR recording:

```bash
python3 scripts/linux_runtime_dependency_gate.py --appdir /path/to/AppDir --require-dvr
```

A passing AppDir dependency gate is necessary but not sufficient for M5. Final acceptance still requires an actual AppImage/deb launch outside the checkout, no developer library-path exports, working QML/WebEngine/plugins/resources, Player 1 playback, and the Arc 17 clean-environment matrix.
