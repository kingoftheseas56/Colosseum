# Local Windows arm64 diagnostic APK build (task014)

Lives on D:\ColosseumAndroid (Seagate). Nothing on C:\Qt or global Python changes.

- `sdk\` — separate SDK root: NDK 27.2.12479018, platforms;android-36, build-tools 36.0.0.
- `Qt\6.11.1\android_arm64_v8a` — copy of the C:\Qt kit plus Qt Image Formats from Qt's
  official archive (SHA-256 9c00c78f... matched download.qt.io). Without it the APK
  silently lacks the WebP/TIFF/TGA/ICNS/WBMP plugins.
- `deps\arm64-v8a` — OpenSSL/Boost/libtorrent prefix from Actions run 39610117024
  (`task014-android-deps-arm64-v8a-*`, checksum verified). Host-independent static libs.
- `donor-working\`, `nuvio-prefix\` — pinned donor restored with restore_donor.py and the
  engine prepared + stamp-verified on this host (never transplanted from CI).
- Host Qt: C:\Qt\6.11.1\msvc2022_64. JDK 21 (Microsoft). READELF -> NDK llvm-readelf.

Run (Git Bash): `bash build-local-apk.sh <repo> org.qtproject.example.colosseum.nuvio<id> [out]`.
Runs the same configure and gates as Actions: correspondence gate (skips fail), runtime
bundle, OpenSSL pair. First build ~13-17 min, incremental ~3 min. Signed with the local
Qt debug key (stable across local builds, differs from every CI run).
Member list matched the CI APK exactly (1000/1000); APK bytes differ (host paths).
