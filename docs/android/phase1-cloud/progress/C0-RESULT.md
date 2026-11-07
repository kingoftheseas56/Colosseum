# C0 result — cloud host qualification

Captured 2026-11-02 21:42–21:50 UTC (cloud clock). Credit balance: UNKNOWN (not exposed).

## Identity

- Repository: kingoftheseas56/Colosseum, branch `codex/android-phase1-cloud-20261103` (push target accepted by origin; no claude/... rename needed).
- Starting cloud HEAD: `92e762b0ed69cd9e5c38d2f6aac5c9eec53f345f` (handoff docs commit).
- Audited application base `501b23edba022a82666bd25e64f4f02ad8bb9a9f`: `git merge-base --is-ancestor` → 0 (ancestor).
- `git diff --stat 501b23ed HEAD -- . ':!docs/android/phase1-cloud'` → empty: initial product source equals the audited base.
- `python3 docs/android/phase1-cloud/tools/inputs.py verify` → `INPUTS_VERIFIED`, 79 files; donors extracted outside the checkout to the session scratchpad (`product_source_modified: false`).

## Host

- Ubuntu 24.04.5 LTS, kernel 6.18.44, x86_64, 4 vCPU, 15 GiB RAM (15 GiB available), 30 GiB free disk, no swap.
- Python 3.13.18 on PATH (`/usr/local/bin/python3`; CI baseline is 3.12 — system `/usr/bin/python3` is 3.12.3 and was used for the aqt venv). Node v22.23.4. JDK present (`/usr/bin/java`; not needed for host). CMake 3.28.3, Ninja 1.11.1, g++ 13.3.0, pkg-config, git, curl, unzip, patch present.
- Runtime libs libEGL/libGL/libxkbcommon/libfontconfig/libX11/libdbus-1/libglib-2.0 present (cloud bootstrap installed libegl1/python3-venv; no apt correction needed by C0).

## Qt acquisition (route: pinned aqt host slice, NOT install_toolchain.sh)

- `pip install git+https://github.com/miurahr/aqtinstall.git@8c3695d4…` → 0 (15 s).
- `aqt install-qt linux desktop 6.11.1 linux_gcc_64 -m qtwebsockets qtimageformats` → 0 (34 s), 1.5 GiB in session scratchpad.
- `qmake -query QT_VERSION` → 6.11.1; `qmltestrunner -help` → 0; ldd of libQt6Gui + libqoffscreen: no missing libs; `pkg-config --modversion Qt6Quick` → 6.11.1, prefix = the aqt kit.
- `qmlimportscanner`: **only** `libexec/qmlimportscanner` exists; `bin/qmlimportscanner` absent → establishes WT-14 host discovery mismatch (conditional fix authorized).
- Env: `QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software LC_ALL=C.UTF-8`. Player fixtures additionally require `QML_XHR_ALLOW_FILE_READ=1` (they read production QML via XHR; first run without it failed in initTestCase — environment, not regression).

## Checks (each ≤120 s cap, real exit codes)

| Command | rc | Notes |
| --- | --- | --- |
| python3 tests/test_android_cache_paths.py | 0 | |
| python3 tests/test_android_toolchain_qualification.py | 0 | |
| python3 tests/test_android_runtime_bundle.py | 0 | real CMake staging ran (no skip) |
| node tests/android_reader_boot_test.mjs | 0 | |
| node tests/android_reader_selection_touch_test.mjs | 0 | |
| node tests/android_reader_gesture_cancel_test.mjs | 0 | |
| qmltestrunner tst_account_choice_accessibility.qml | 0 | 4 passed |
| python3 tests/android_qml_preflight.py --qt-root $QT_HOST | 0 | `ANDROID_QML_COMPILE_SUMMARY entries=61 failed=0`, 11 s; probe mocks PlayerItem/SeekThumbnailer/ActivityPlaybackTracker only |
| tst_android_shared_action_touch / shared_poster_touch | 0 / 0 | 5 / 5 passed |
| tst_android_beta_reader_touch | 0 | 8 passed |
| tst_player_more_touch / tst_android_subtitle_viewport / tst_player_seek_touch | 0 / 0 / 0 | 7 / 8 / 9 passed (with XHR env) |

## Classification

- SOURCE_CHECKS: PASS
- HOST_QML: PASS (Qt 6.11.1 gcc_64 aqt slice; QML compile graph evidence only, not a native app launch/first frame)
- ANDROID_TOOLCHAIN: NOT_ATTEMPTED (no SDK/NDK/android Qt kit; host-first per C0)
- ANDROID_APK: BLOCKED — no hardened Nuvio engine prefix/donor checkout supplied in cloud (`COLOSSEUM_NUVIO_ENGINE_PREFIX` absent); a default APK would omit Nuvio and not be product-equivalent. No signing keys in cloud.

Device acceptance: LOCAL_PENDING (OnePlus 11R, Nokia T20).
