# Linux candidate packaging

For 1.1.9, `release-linux-deb.yml` builds the standard edition on Ubuntu 24.04 and creates an amd64 `.deb` for Ubuntu 24.04/Linux Mint 22. Vinyl and Feria qualification flags stay off. The workflow preserves exact source/binary/QML identity and dependency notices, then tests installation, normal-user X11 startup, natural exit and uninstall in a separate Ubuntu container without the build SDK. These checks do not qualify hardware acceleration, audio, providers or every Linux distribution. Artifacts remain review candidates; the workflow does not publish releases.

`package_deb.py --stage CHECKED_APPDIR --output Colosseum-1.1.9-linux-amd64.deb` rechecks ELF closure and QML identity and rejects builds requiring glibc newer than 2.39. It installs the runtime under `/opt/colosseum`, a `/usr/bin/colosseum` launcher, menu entry and icon. Install with `sudo apt install ./Colosseum-1.1.9-linux-amd64.deb`; remove with `sudo apt remove colosseum`. User profiles are not removed. Updates remain manual. AppImage and RPM are separate future packaging targets, not products of this workflow.

Both archive and Debian assembly require the production Content API origin and the standard accounts-capable edition. The shared checksummed content-staging policy removes migrated artwork and the four catalogue databases, while retaining native extension assets and metadata. The release version comes from `native/CMakeLists.txt`.

`assemble.py` packages a fresh Release `colosseum` and its matching `qml-build.manifest`. It does not download dependencies, silently reuse an old alpha, or upload an artifact. Its destination must not exist. Supply the full compiled source commit; the release operator must verify it corresponds to the build.

Requirements: Python 3.11+, GNU binutils (`readelf`), glibc `ldd`, `patchelf`, a complete Qt 6.11.1 SDK/runtime, and the compatible application dependency libraries. Python tooling has no third-party modules. All runtime executable inputs must be trusted artifacts from the authorized build or official distributions.

Example, with explicit caller-selected paths:

```sh
python3 assemble.py --source /path/Colosseum --build /path/build \
  --qt /path/Qt/6.11.1/gcc_64 \
  --stage /path/Colosseum-1.1.8-linux-x86_64-candidate \
  --commit FULL_40_CHARACTER_CANDIDATE_COMMIT --patchelf "$(command -v patchelf)" \
  --dep-lib /path/mpvqt/lib/x86_64-linux-gnu \
  --dep-lib /path/native-dependencies/usr/lib/x86_64-linux-gnu \
  --binary /usr/bin/ffmpeg --binary /usr/bin/ffprobe \
  --dependency-notices /path/collected-notices
python3 check_package.py /path/Colosseum-1.1.8-linux-x86_64-candidate \
  --report /path/package-check.json
```

Select a coherent FFmpeg family explicitly using repeated `--runtime-lib PATH`. The Qt SDK can ship FFmpeg SONAMEs that collide with the distro's FFmpeg/libmpv dependencies. The tested candidate recipe selects the Debian family (`libavcodec.so.61`, `libavdevice.so.61`, `libavfilter.so.10`, `libavformat.so.61`, `libavutil.so.59`, `libswresample.so.5`, `libswscale.so.8`, `libpostproc.so.58`) ahead of SDK resolution. This still requires actual QtMultimedia/mpv runtime testing; matching SONAME alone does not prove compatibility.

Use repeated `--exclude-plugin plugins-relative/path.so` only for explicitly unused optional plugins; each exclusion is recorded in BUILD-IDENTITY.json. `--plugin-override plugins-relative/path.so=/path/rebuilt.so` records an explicitly rebuilt matching-Qt plugin. Both reject escaping paths and nonexistent targets. The candidate excludes unused non-SQLite database drivers and the serial-port NMEA backend.

Use `--exclude-qml-module QtQuick3D` for the unused SDK SpatialAudio module when QtQuick3D is not installed; this is recorded explicitly. Only runtime QtWebEngineProcess is copied from libexec. Qt libraries are collected through actual ELF closure, excluding unrelated SDK tools/libraries.

The assembler copies QML/assets/resources/data, Qt runtime modules/plugins/helpers/resources/translations, and the recursive non-system ELF dependency closure. RPATHs are relocated to package-relative `$ORIGIN` paths. `AppRun` sets the packaged Qt paths, preserves display/audio variables, and never disables WebEngine sandboxing. The executable's `usr/bin` layout makes its `../../qml` resource lookup resolve to the package root, and the launcher's working directory provides the same root for relative assets.

Host glibc and graphics-driver libraries (GL/EGL/GLX/OpenGL/DRM/GBM/Vulkan/VA) remain external. The checker records actual maximum undefined GLIBC, GLIBCXX and CXXABI versions and all external libraries; do not claim Ubuntu 24.04 compatibility without qualifying the resulting requirements. Bundling `libstdc++` handles its own ABI symbols but does not lower that library's glibc requirement.

`collect_notices.py` preserves available host/dependency copyrights and MpvQt license files. The Qt SDK SBOM and common license texts are copied during assembly. Supply dependency/build manifests as additional notice inputs. This is provenance collection, not legal certification. The Debian libmpv build used for this candidate is GPL-3+; do not label the combined binary package MIT-only. Corresponding-source and license-specific stable public distribution obligations require separate verification.

Run packaging tooling regression tests:

```sh
PATCHELF="$(command -v patchelf)" python3 test_packaging.py
```

The tests compile a small native fixture and prove relocation plus rejection of missing libraries/resources, QML drift, external symlinks, and absolute RPATHs. They are not Colosseum GUI or playback tests.

After actual staged-application qualification, create the candidate archive with `archive.sh STAGE OUTPUT.tar.gz`, extract it into a new directory, and repeat launch, navigation, local-content, shutdown, restart and persistence checks. No bytes may change after final archive verification. Upload the same archive and independently retrieve/hash it. `BUILD-IDENTITY.json` records both the original linked executable SHA256 and the relocated packaged executable SHA256.

For the Debian 13 candidate, the SDK TIFF plugin requires unavailable `libtiff.so.5`. The replacement is built from unmodified official QtImageformats v6.11.1 with its bundled libtiff 4.7.1 (not an older compatibility library):

```sh
cmake -S qtimageformats-6.11.1 -B qtimageformats-build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/path/Qt/6.11.1/gcc_64 \
  -DFEATURE_system_tiff=OFF -DQT_BUILD_EXAMPLES=OFF -DQT_BUILD_TESTS=OFF
cmake --build qtimageformats-build --target QTiffPlugin -j2
```

Supply `--plugin-override imageformats/libqtiff.so=/path/qtimageformats-build/plugins/imageformats/libqtiff.so`. Retain the module LICENSES and bundled libtiff COPYRIGHT and qt_attribution.json alongside the source archive hash. A real QImage TIFF save/load/pixel-equality check was executed separately from application GUI verification.
