# Linux AppImage review candidate

This workflow adds an x86_64 AppImage without changing native or QML application code. It is a candidate build pipeline, not evidence that a release or every Linux desktop has been qualified.

## Build and identity

`release-linux-appimage.yml` builds on Ubuntu 22.04. It uses the full conda-forge Qt 6.11.1 C++ SDK, including WebEngine, Quick 3D, Positioning, SerialPort, Wayland and the GTK platform theme. This avoids treating the official Ubuntu 24.04 Qt SDK as an older-glibc binary merely by changing the runner label. The SDK layout is obtained from `qmake -query`, not guessed.

The existing AppDir assembler still owns dependency closure, relative RPATHs, packaged QML identity and GdkPixbuf loader verification. The AppImage wrapper additionally requires Qt Quick 3D, OpenSSL, ffmpeg/ffprobe, libmpv, the offscreen platform plugin and unchanged license notices. A measured undefined-symbol GLIBC requirement above 2.35, or an unknown requirement, fails packaging. Host glibc and graphics drivers remain external.

The application version comes from `native/CMakeLists.txt` through the existing assembler. One source SHA and one compiled AppDir produce both `Colosseum-VERSION-linux-amd64.deb` and `Colosseum-VERSION-x86_64.AppImage`. The `.deb` retains its existing conservative Ubuntu 24.04/Mint 22 dependency policy; it is not advertised as an Ubuntu 22.04 installer. Existing `.deb` release version literals are not edited by this task.

`appimage-tools.json` pins SHA256s for micromamba, appimagetool and the type-2 runtime. The runtime's upstream continuous URL is deliberately hash-checked: replacement bytes fail rather than being silently accepted. Refresh the pin only after reviewing the new runtime. Qt versions and the mpv/FFmpeg family are constrained in `appimage-environment.yml`; the full resolved package URLs, SHA256s, versions, recipes and notices are recorded in each artifact. Transitive dependencies are not yet a repository-wide immutable lockfile, so identical rebuild bytes are not claimed.

## Dispatch without publishing

The registered Debian workflow has a narrowly scoped boolean bridge so this new reusable workflow can be exercised on an unmerged branch:

```sh
gh workflow run release-linux-deb.yml --ref YOUR_REVIEW_BRANCH -f appimage_candidate=true
```

Without that input, the existing Debian workflow keeps its normal behavior. With it, the old Debian build is skipped and the new AppImage workflow runs. Both workflows have read-only repository permissions; neither path added here creates a release or uploads release assets.

Merge only after the complete matrix is green and Claude has reviewed the branch. A successful compile, unit test or package creation alone does not satisfy that gate.

## Smoke evidence and boundaries

The exact uploaded AppImage is hashed, extracted and checked against its embedded identity before launch in `fedora:latest`, `archlinux:latest`, `opensuse/tumbleweed`, `debian:stable`, and `ubuntu:22.04`. The actual container image digests are retained. Matrix failures do not cancel other distros, and logs are uploaded even when preparation or startup fails.

Each container installs only OS services, fonts, trust roots and graphics libraries, not a Qt SDK or libmpv. A normal user launches the original AppImage with `--appimage-extract-and-run`, `QT_QPA_PLATFORM=offscreen`, a disposable profile and the existing startup probe. PASS requires the production `shell-interactive` log marker, a nonzero visible window, a hidden boot splash, profile isolation, the real Ctrl+Q shutdown route and exit code zero. A timeout, early exit, missing marker, failed IPC command or forced termination is FAIL.

The AppImage-only launcher supplies packaged data search paths and uses the host CA/font configuration without disabling TLS verification or WebEngine sandboxing. The existing shared AppRun is retained unchanged as `AppRun.colosseum` inside the image. The Debian candidate is built before this wrapper is applied.

Offscreen startup is not visual or functional proof of Qt Quick 3D, GPU acceleration, audio, media playback, Wayland, native file pickers, providers or every distro/version. No Windows Harness or installed-desktop completion claim follows from this pipeline.

## Notices and release gate

`licenses/runtime/manifest.json` records the exact dependency builds. Their unchanged license files and source/build recipes, MpvQt notices, FFmpeg configuration and license output, OpenSSL build identity, and host dependency copyright records are included. The installed build inventory is a superset; the ELF closure report identifies libraries actually shipped.

Colosseum's original source remains MIT, but the combined binary must not be labeled MIT-only. In particular, the open-source Qt Quick 3D package carries GPL terms; not every Qt module is LGPL. Corresponding-source delivery, relinking and the obligations of the actual bundled combination must be reviewed before public release. Notice collection is not a compliance certification.

## Three-step real-machine check

1. On a normal Fedora or Arch desktop, verify the artifact SHA256 against its `.sha256` file, run `chmod +x ./Colosseum-*-x86_64.AppImage`, then launch it without a developer Qt SDK. When FUSE is unavailable, append `--appimage-extract-and-run`.
2. Open Theatre, Tankoban, Biblio and Universes; check the Dexter 3D tribute, browse a local folder through the native picker, and play a known local media file with sound. Record distro/version, desktop session, GPU/driver and any failure log.
3. Quit normally, launch the same file again, and confirm preferences/progress persist. Report the exact image SHA256 with the result; this manual check remains separate from the offscreen matrix.
