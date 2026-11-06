# Cloud progress — final checkpoint

State: QUEUE_ACCOUNTED (all authorized units implemented, decision-recorded or investigated). Not "Phase 1 finished":
every unit's physical acceptance is LOCAL_PENDING (OnePlus 11R, separately Nokia T20) and no APK was produced.

- Repository/branch: kingoftheseas56/Colosseum `codex/android-phase1-cloud-20261103` (pushed after every unit; not merged to master; no PR opened).
- Audited base 501b23edba022a82666bd25e64f4f02ad8bb9a9f (ancestor); Alpha donor 9e896bfd5dac40ef3ceb0e26e166e85ed8beecd9; starting cloud HEAD 92e762b0.
- Credit balance: UNKNOWN (not exposed).
- Host environment (session-only): aqt Qt 6.11.1 gcc_64; `QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software LC_ALL=C.UTF-8 QML_XHR_ALLOW_FILE_READ=1`. Details: `C0-RESULT.md`.

## Build/verification states

| Gate | State |
| --- | --- |
| SOURCE_CHECKS | PASS |
| HOST_QML | PASS (`tests/android_qml_preflight.py`: 61 entries, 0 failed after every unit) |
| NATIVE_COMPILE | Partial: `tests/session_store_harness.cpp` (SessionStore + Feria ops) and the WT-04 PlatformRuntime harness compiled/run with g++; no full native/desktop build; Java not compiled (no Android SDK) |
| ANDROID_TOOLCHAIN | NOT_ATTEMPTED |
| ANDROID_APK | BLOCKED — no hardened Nuvio engine prefix/donor in cloud; default APK would not be product-equivalent; no signing keys |
| DEVICE | LOCAL_PENDING for every unit |

## Units (details, tests and exit codes per unit in `units.json`)

| Unit | State | Main evidence |
| --- | --- | --- |
| C0 | HOST_QML_PASS | C0-RESULT.md |
| B0 | HOST_QML_PASS | overlay applied (8/8 hashes), tst_android_visual_baseline |
| PF-02 (+PF-02b) | HOST_QML_PASS | tst_android_sources_budget, tst_android_comic_manga_sources_budget |
| PF-01 | HOST_QML_PASS | tst_android_download_targets (TopBar width limited by pre-existing picker overlap) |
| PF-05 | HOST_QML_PASS | tst_android_speed_popup_viewport |
| BR-READER-TARGETS | HOST_QML_PASS | tst_android_beta_reader_touch |
| WT-02 | HOST_QML_PASS | tst_android_account_keyboard |
| WT-04 | HOST_QML_PASS | tests/android_font_scale_test.py (real PlatformRuntime) |
| BR-SOURCE-REPLACEMENT | HOST_QML_PASS | session_store_harness (g++), tst_android_source_replacement |
| PF-04 | PORTABLE_TEST_PASS | android_continue_missing_download_test.mjs |
| PF-03 | PORTABLE_TEST_PASS | android_theatre_nested_back_test.mjs |
| WT-10 | PORTABLE_TEST_PASS | android_universe_nested_back_test.mjs |
| WT-06 | PORTABLE_TEST_PASS | android_featured_routes_test.mjs |
| WT-05 | PORTABLE_TEST_PASS | extensions_store_torrentio_fallback_test.mjs |
| WT-07 | PORTABLE_TEST_PASS | theatre_search_year_test.mjs |
| PF-06 | PORTABLE_TEST_PASS | theatre_catalogue_movie_fallback_test.mjs |
| WT-03 | HOST_QML_PASS | addon_video_compatibility_test.mjs + Sources label case |
| PF-08 | HOST_QML_PASS | tst_android_background_download_message |
| PF-14 | HOST_QML_PASS | comic_torrent_sources_page_harness (qml runtime) |
| WT-08 | HOST_QML_PASS | tst_comic_download_cancel |
| WT-11 | HOST_QML_PASS | tst_android_east_blue_containment |
| WT-01 | DECISION_REQUIRED | DECISIONS.md (Theatre bulk remove vs Stremio-aware removal owner) |
| PF-10 | PORTABLE_TEST_PASS | test_android_package_identity.py |
| PF-07 | PORTABLE_TEST_PASS | android_track_labels_test.mjs |
| PF-09 | PORTABLE_TEST_PASS | test_android_launcher_icon.py (rendered launcher pending) |
| WT-12 | PORTABLE_TEST_PASS | test_android_apk_qualification.py (no real APK) |
| WT-14 (conditional) | PORTABLE_TEST_PASS | real aqt kit: gate False -> True |
| WT-09 (conditional) | HOST_ONLY_FINDING_NO_CHANGE | DECISIONS.md |
| Decision-only items | not altered | DECISIONS.md |

Pre-existing failures (identical on the audited base, not introduced here): see `HOST-QML-SUITE.md`, plus
test_android_build_graph_contract.py (10), player_recovery_resume / player_local_error_retry node tests,
dcau/one_piece/universe page node tests (SyntaxError), extensions_store_svelte_contract (missing Store.svelte),
manga_tankoban_page_harness ("must render all 3 sources, got 0"), tst_topbar_spatial_navigation width 1172.
test_android_desktop_scale.py needs ANDROID_SDK_ROOT (not run).

## Composed source (overlay layer now in tracked source)

The eight visual overlays are integrated in commit B0 and later units edited some of the same files; the branch
HEAD is the single composed source — do not re-copy old overlay files over it during packaging. Current sha256
(first 16 hex): ColosseumActivity.java f67df3048153c7d8, AdaptiveLayout.qml 0111e68114486b4c, ContinueRow.qml
917d0d5feedc8ab6 (= overlay), HouseScrollBar.qml bda417e1cb62da2a (= overlay), Main.qml 8e186bcdc0554cf7,
SourcesSheet.qml 063a3643fae4edc4, WidgetHeader.qml 6770502feeef4d53, WorldPage.qml 8ff51b4704d6ed34.
Use `python3 scripts/android/qualify_apk.py snapshot` for source provenance of a local build.

## Next actions

1. Decide WT-01 Theatre bulk-remove semantics (DECISIONS.md), then implement the three-library selection unit.
2. Local build of this branch with the hardened Nuvio prefix (`scripts/android/build_apk.sh arm64-v8a` with
   `COLOSSEUM_ANDROID_NUVIO_BACKEND=ON`), installed with the Beta signer/compatible versionCode; qualify with
   `scripts/android/qualify_apk.py inspect/identity/provenance`.
3. Physical acceptance below on OnePlus 11R (default 1.15 and `colosseum_visual_zoom` 1.20), then Nokia T20.

## Local acceptance checklist (LOCAL_PENDING)

- Home/World/Sources edge-to-edge composition; Sources, Comic and Manga pickers show a full row + final row reachable at 1.15 and 1.20.
- Downloads row actions and TopBar icons tap reliably; no neighbour mis-taps; picker vs system cluster overlap at phone width (pre-existing) noted.
- Player speed/sleep/skip popup fully reachable; Back closes Theatre transients before the title; Galaxy/DCAU Back goes one level.
- Reader selection swatches/Note/Save and bookmark delete gutter; note editor above real IME.
- Account sign-in/create fields stay above standard and floating keyboards; no credential submission needed.
- Android text size 1.0/1.3/2.0 changes Back/headers/account typography live without clipping.
- Same-episode source switch (parked and active) plays the new source at the latest position.
- Continue on a deleted download opens that episode's Sources with saved position; explicit resume past 80%.
- Featured Read/Details; Torrentio card offline; year-qualified search; exact movie opens when metadata misses.
- Codec "Unsupported on this device" labels only with explicit evidence; background-download pause explanation.
- Comic inspection Cancel and edition Cancel; East Blue preview/index containment; readable track labels; launcher icon.
