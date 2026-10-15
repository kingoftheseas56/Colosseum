# Vinyl A2 Linux prototype qualification

Completed 2026-10-16 IST (2026-10-15 UTC). Standalone prototype, not production integration.

## Source authority and commits
The supplied 58-file snapshot is authoritative. All six sibling directories and manifest hashes were verified. The existing application tree is only a publication transport parent; none of it was used to build this prototype.

| Stage | Local authored/tested commit | Published equivalent commit |
|---|---|---|
| Exact packet baseline | 3c421e062c6bb54c9973a63d5b23a00f63c0c9ca | afa50b4a996c0eadd8139fb09553648ee7985cc6 |
| Approved repair | 82828ed797bf628bcfb64a3461fc1bc7914dc6a0 | 8882b7e4014b26b837cb285efafdfd14ba991c04 |
| Five regression rows | eb052bc93659aa11c2fdf7c09025f861e2e8ba97 | 4243d279fcb61e5050f9d2c61c420f217e1d5110 |

Final code is under baseline/ for sibling-path compatibility. BASELINE-MANIFEST describes the imported commit; linux-evidence/FINAL-SOURCE-MANIFEST.json describes the tested result. The original packet MANIFEST describes original input paths, including evidence/ (published as supplied-evidence/).

## Results
- PASS: approved patch unchanged; no extra Linux implementation repair.
- PASS: expected baseline compilation failures independently reproduced at both repaired files.
- PASS: approved patch build, six CTest suites, original composed QtTest 10 passed / 0 failed / 0 skipped.
- PASS: final clean build, six CTest suites, expanded composed QtTest 15 passed / 0 failed / 0 skipped.
- PASS: catalogue 78 checks, artwork 38 cases, personal 5, media 15, bridge QtTest 14. Counts use different harness conventions; do not sum into a fabricated assertion total.
- PASS: independent source review, preserved original assertions and all five sibling suites.
- NOT RUN: Windows rerun, live providers, actual playback, full app wiring, QML crash work.
- No Actions dispatched. Publication commits use [skip ci] because this alternate fork still has push triggers.

Linux: Debian 13 x86_64, GCC14.2.0, CMake4.4.3, Ninja1.13.2, Qt6.11.1 Core/Test, C++17 Debug. No dependencies installed.
Composed executable SHA256: 096d27cc0215a9e455d84178bd26c1e1bab3e9adebd24edc6ba76dd55ba0d3c5.

## Reproduce
From this directory, with Qt >=6.5 Core/Test SDK:
```sh
cmake -S baseline/artist-service-composed-20261015 -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH=/path/to/qt
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
./build/test_artist_service_composed
```
See linux-evidence/LINUX-QUALIFICATION.md, commands.json, raw logs and exit files, final.diff, final-executables.sha256, and final source manifest. Original Windows evidence is supplied evidence, not a Linux claim.
