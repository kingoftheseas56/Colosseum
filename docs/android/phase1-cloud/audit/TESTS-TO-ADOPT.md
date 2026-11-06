# Test adoption plan
Source inspection and supplied receipts only; no tests executed in this audit. Adopt valid behavior assertions, adapt obsolete phone-layout/DPR/default assumptions, and keep current Beta regressions. Never count static file-presence assertions as complete device behavior.

Shared policy, Sources and popup requirements: actual component instantiation at measured default 1.15/launch 1.20 viewport; neighbor-disjoint target bounds; vertical/horizontal gesture steal; header/filter/wrapped quality states; final source/action reachability; speed/sleep/skip controls. Sources needs new true geometry fixtures, not another blanket touch patch.

## worker-backend-readers

# Test adoption supplement — source inspected, nothing executed

- **Alpha session_store_harness.cpp150–164:** adapt source-swap signal assertions, exact same episode identity/position retention and case-normalized metadata-only no swap. Add actual Main fake retained-player tests for already-active and parked source replacement, latest live position and stale response generations. Existing native signal assertion alone does not verify media reload.
- **Alpha tests/qml/tst_phone_reader_controls.qml:** useful intentions: target bounds, bookmark delete separated from row jump, narrow/short viewport selection/note/IME containment. Adapt into Beta tst_android_beta_reader_touch.qml/tst_android_reader_overlay_focus.qml; keep Beta logical window keyboard coordinates and native surface focus. Beta current host test uses960x640 and deliberately preserves desktop-sized action targets, so its success does not cover narrow-phone tolerance.
- **Alpha downloadstore_lifecycle_harness.cpp and downloadstore_local_file_availability_harness.cpp:** review overlap with Beta tests/auto/player/tst_android_download_recovery.cpp before adoption. Beta already covers exact pin, changed hash/file, invalid206, transient prefix, lease/manual pause, missing partial and cancellation failure. Use explicit AndroidPinnedTorrent in host tests; desktop default intentionally Legacy. Platform PF-04 owns missing-file Continue caller coverage.
- **Alpha tests/nuvio_fixture_admission_test.cpp:** mechanism moved into Beta tests/support header. Keep isolated test macro/private fixture target; avoid production diagnostic trigger watcher. Alpha tests/android/nuvio_cancel_requests_regression.cpp/device runner may supply device stress scenarios only after bounded adaptation and authorization; no test run in audit.
- **Keep Beta reader tests:** android_reader_gesture_cancel_test.mjs; android_reader_selection_touch_test.mjs; android_reader_fonts_test.mjs; Android ReaderPublicationSessionTest; reader2 profile privacy/generation harnesses; comicreader_resume_race_harness.qml and strip/numbered-chapter resume cases. Preserve security/generation/font boundaries even when shared by Alpha.

Missing checks: real Android native WebView overlay/keyboard stacking, narrow viewport target overlap, source-swap actual route+latest position, actual optional Nuvio ABI and foreground-service/provider behavior. Supplied host pass evidence is separate from new audit inspection and does not qualify OnePlus 11R/Nokia T20 interchangeably. No new tests were run.


## Per-transfer host/device checks

### PF-01

Existing: Alpha tests/qml/tst_phone_download_targets.qml, tst_phone_touch_chrome.qml; Beta tst_android_shared_action_touch.qml, tst_android_shared_poster_touch.qml.
Add/adapt: Adopt/adapt tst_phone_download_targets.qml, tst_phone_touch_chrome.qml; retain tst_android_shared_action_touch and poster suite. Test edge taps, neighbor separation, drag steal/cancel and desktop geometry.
Device gate: OnePlus 11R at both launch zooms; T20 independently; touch and mouse, text scaling, safe edges and landscape.

### PF-02

Existing: Existing tests/sources_sheet_empty_extensions_harness.qml and Alpha tests/qml/tst_source_picker_template.qml, tst_phone_comic_sources.qml cover partial states; no existing full installed-scale Sources budget fixture found.
Add/adapt: New actual-component Sources fixture at rows/no rows, visibleRows0, one/multiple pill rows, extensions menu and 150-unit rows. Existing empty-extensions and source-picker fixtures cover only part.
Device gate: Record actual window/sheet/list rectangles and Qt scale on OnePlus 11R default 1.15 and override 1.20; reach final source, copy/download, extension menu and Back. Independently qualify T20.

### PF-03

Existing: Alpha tests/android_theatre_nested_back_test.mjs; tests/shell_back_policy_test.mjs; tests/android_shell_back_dispatch_test.mjs; native Android Back gesture harness.
Add/adapt: Adopt android_theatre_nested_back_test.mjs and shell_back_policy_test; actual component spy order for menus, sheet, title; native dispatch fixture; no double invocation.
Device gate: Native gesture and three-button Back through IME, Sources filter/context, source sheet, title; focus restoration; repeat/cancel predictive gesture.

### PF-04

Existing: Alpha tests/android_continue_missing_download_test.mjs; tests/downloadstore_local_file_availability_harness.cpp; tests/android_stream_retry_resume_test.mjs; Beta player_recovery_resume_test.mjs and Android download recovery native suite.
Add/adapt: Adopt android_continue_missing_download_test.mjs, downloadstore_local_file_availability_harness.cpp, android_stream_retry_resume_test.mjs; test present/missing/noncatalogue files,95 percent explicit resume, stale load, both title variants.
Device gate: Delete/move controlled test media externally, Continue, choose source/cancel, verify same episode and position; no library/progress loss; offline and no-source cases.

### PF-05

Existing: Beta tests/qml/tst_player_more_touch.qml, tst_android_subtitle_viewport.qml, tst_player_seek_touch.qml protect neighboring behavior; no existing actual speedPopup short-viewport fixture found.
Add/adapt: New actual-speed popup fixture shorter than 500 and long sleep-timer row; retain player_more_touch, subtitle_viewport and seek cancellation suites.
Device gate: Reach first/last speed, sleep and skip controls in actual OnePlus landscape with HUD open, zoom 1.15/1.20, resize/IME and hardware focus.

### PF-06

Existing: Alpha tests/theatre_offline_meta_test.mjs; Beta title canonical/Collections route tests; provider-miss helper assertions need adapted exact canonical snapshot.
Add/adapt: Add loadMeta stubs for primary/extension misses, exact/mismatched id, series exclusion, canonical provider-door distinction; existing title canonical/Collections tests retained.
Device gate: Offline/cache/no-extension movie detail, retry after providers return; same source identity and artwork, no substituted film.

### PF-07

Existing: Existing tests/subtitle_menu_addon_pick_test.mjs and tst_android_subtitle_viewport.qml protect selection/pending semantics; no focused generic-title/MIME-label helper test identified.
Add/adapt: Pure helper tests: generic/custom title, language unknown, MIME known/unknown, internal cues; verify selected ID unchanged.
Device gate: Media3 movie with multiple embedded/fetched languages; select and resume same track across reload.

### PF-08

Existing: Existing tests/test_android_platform_services_contract.py and native download recovery tests cover host lifecycle boundaries; donor DownloadsPage source projection; add actual-page injected error/recovery fixture.
Add/adapt: Fixture injected host error/recovery and stale epoch; no service execution required for host check; native host-state tests retained.
Device gate: Deny notification/service prerequisite in controlled device profile, see explanation and recovery; no lost jobs.

### PF-09

Existing: No runtime launcher artwork acceptance test found. Future resource-membership/manifest-reference packaging check and device launcher mask check needed.
Add/adapt: Resource XML references and merged-manifest/icon membership check in future packaging; no build run here.
Device gate: Launcher round/square masks, dark/light/theme launcher and app label on both devices.

### PF-10

Existing: Alpha tests/test_android_unsigned_release.py and Beta tests/test_android_build_graph_contract.py/test_android_runtime_packaging.py; new negative cache-input fixture required.
Add/adapt: Adopt/adapt Android identity/packaging tests from worker test register; negative config cases in disposable fixture when implementation authorized.
Device gate: Future signed update preserves same app data and intended separate Alpha/Beta install; inspect generated manifest.

### PF-14

Existing: Alpha tests/qml/tst_comic_download_cancel.qml; related native ComicDownloader cancellation/lifecycle tests; add source-picker retained-result cancellation assertions.
Add/adapt: Adopt tst_comic_download_cancel.qml; test inspect cancel then fresh selection, late completion, ambiguous/incomplete/combined states.
Device gate: Cancel slow metadata inspection and choose different source; verify torrent/temp cleanup and no double cancellation.

### BR-SOURCE-REPLACEMENT

Existing: Alpha tests/session_store_harness.cpp:80-82,150-164 source-swap assertions absent Beta counterpart; both session harnesses exist; relevant player Android lifecycle/recovery suites remain.
Add/adapt: Reintroduce/adapt source identity test: same content different hash/file/url reloads exactly once, preserves position; metadata-only/case-normalized hash no reload; parked and already-active Main session path uses newly selected descriptor; switch different episode clears old resume; download prefetch unaffected.
Device gate: OnePlus 11R portrait and landscape: pause/minimize same episode sourceA, choose sourceB, confirm actual route changes and resume position retained; repeat failed source and app-background transitions with video download running; Nokia T20 separately unqualified.

### BR-EPUB-DEFAULTS

Existing: tests/reader2_logic_harness.qml; tests/reader2_paper_text_test.mjs; tests/android_reader_fonts_test.mjs; tests/qml/tst_android_beta_reader_touch.qml covers overlay/IME but not this typography default.
Add/adapt: Fresh/no-reader2, legacy-flat, saved default and per-book override cases; Android narrow/wide viewport fallback; desktop unchanged; fixed-layout content ignores reflow settings. Assert top/bottom and computed2..18% gap against actual innerWidth, not2*margin approximation.
Device gate: OnePlus 11R portrait+landscape at both relevant density launch settings; inspect actual EPUB WebView viewport and text, page turn/resume after settings changes; Nokia T20 separate. Compare fresh test profile and saved24/72 preferences.

### BR-READER-TARGETS

Existing: Alpha tests/qml/tst_phone_reader_controls.qml missing Beta; Beta tst_android_beta_reader_touch.qml covers IME, delete-vs-jump and note save at 960x640, and tst_android_reader_overlay_focus.qml covers focus topology. Adapt tests, do not reinstate old Android DPR division.
Add/adapt: Re-use Beta fake IME; add narrow widths, short landscape, keyboard visible, color/note/copy/delete touch bounds and no row jump from delete gutter; reparented popover Escape/Tab, save/cancel/draft retention, no action on cancelled drag; desktop geometry assertions.
Device gate: OnePlus 11R tap swatches/delete/save at normal and enlarged Android font settings; hold/select, open note keyboard, save/cancel, dictionary/footnote dismiss and Back both orientations, density 1.15/1.20; Nokia T20 independently.

### WT-01

Existing: Alpha tests/qml/tst_phone_library_selection.qml, tests/phone_library_bulk_owner_test.mjs, tests/android_library_selection_back_test.mjs; Beta tst_android_library_touch.qml and tst_android_beta_biblio_touch.qml already cover holds/cancellation.
Add/adapt: Identity persists through sort/filter/prune; synchronously mutating removal callback; false/throw retains failed rows; watched only Theatre; Back confirmation then selection then page; profile switch clears selection; rotated/clipped viewport no accidental open; desktop layout/control baseline unchanged.
Device gate: In Theatre, Biblio, Tankoban libraries enter Select from one existing card menu; tap two distinct rows, sort/filter, Select all and Invert; count/identity must follow visible canonical rows and card taps must not open media while selecting. Remove then Keep changes nothing;Remove removes membership only and files/progress remain. Inject one refused removal in an isolated test profile: only failed row stays selected and retry works. Theatre Mark watched/unwatched affects selected identities once. Native Back closes confirmation, then selection, then page on separate presses; background, rotation, world/profile departure leaves no stale selection owner. Record APK/source hash, device Android version, navigation mode, density, launch extra, orientation and measured root viewport. Use OnePlus 11R and separately Nokia T20;no device work performed in this audit.

### WT-02

Existing: Alpha tests/qml/tst_phone_account_keyboard.qml; Beta modal/reader IME tests exercise other owners, not these account forms.
Add/adapt: Portrait/short landscape/tablet with synthetic visible keyboard, nonzero and zero rectangles, switch first/last fields, rotation, close/reopen; assert focus and Save/Continue reachable; desktop no added inset. Device floating/standard keyboard and Back dismissal with no credential submit required.
Device gate: Open Account create/sign-in/recovery form with a disposable test identity;focus the last editable field using real standard then floating keyboard. The focused field and Continue/Save remain reachable by scroll without losing entered draft. Switch upper/lower focus, rotate with keyboard visible, hide/reopen keyboard; no double inset or covered action. Native Back dismisses keyboard before form/page. Repeat with Android large text and a keyboard reporting no rectangle if available; desktop host baseline gets no artificial IME margin. Do not submit real credentials merely for geometry. Record APK/source hash, device Android version, navigation mode, density, launch extra, orientation and measured root viewport. Use OnePlus 11R and separately Nokia T20;no device work performed in this audit.

### WT-03

Existing: Alpha addon_video_compatibility_test.mjs, addon_client_runtime_argument_contract_test.mjs, addon_client_runtime_context.qml/harness.cpp, android/DeviceVideoCapabilitiesTest.java, sources_sheet_codec_compatibility_harness.qml.
Add/adapt: Contradictory/missing hints=>unknown; missing/incomplete inventory=>unknown; AVC profile coverage/level, HEVC Main10, AV1, DV base-layer ambiguity; explicit runtime from .pragma library; all callers; rows retained and ordering stable; async stale callbacks unchanged.
Device gate: Open Theatre title Sources with controlled source fixtures or recorded providers: one explicitly compatible codec/profile/level, one explicitly unsupported, one absent/ambiguous hint. All three remain selectable/visible;known unsupported is labeled and softly ranked below others while unknown stays unknown. Select the known compatible sample to verify ordinary playback route still works;choosing unknown must not be blocked. Reopen after capability/config change and ensure stale request does not relabel another title. Actual decoder playback acceptance uses known licensed/local fixture media and is distinct from advisory label correctness. Record APK/source hash, device Android version, navigation mode, density, launch extra, orientation and measured root viewport. Use OnePlus 11R and separately Nokia T20;no device work performed in this audit.

### WT-04

Existing: Alpha phone_font_scaling_harness.cpp and tst_phone_text_scaling.qml; Beta reader_fonts tests protect separate reader behavior.
Add/adapt: Native-notify seam changes1->1.3->2 and verifies actual font pixels; desktop1; test long titles/captions/translated labels and actions at large text across supported viewports, dynamic change without restart, no duplicate density multiplier.
Device gate: Change Android system text size at 1.0,1.3 and 2.0 (or device supported nearest values) while app is open. Shared Back/navigation, account labels, library captions and setup status text update through native preference notification. Long labels wrap/elide with primary actions inside bounds and reachable;no double density enlargement at default 1.15/intent1.20 visual zoom. Return text size to original after observation;reader content font choice remains independent and desktop baseline stays unchanged. Record APK/source hash, device Android version, navigation mode, density, launch extra, orientation and measured root viewport. Use OnePlus 11R and separately Nokia T20;no device work performed in this audit.

### WT-05

Existing: Alpha extensions_store_torrentio_fallback_test.mjs cases initial/missing/renamed/duplicate/invalid/partial failure; Beta extensions_store_contract_test.mjs and native extension instance tests.
Add/adapt: Community list failure/timeout/partial results with honest error; known remote metadata wins; one stable fallback; no automatic install/enable; disabled installed entry stays disabled until explicit action; current Configure/replace paths unchanged.
Device gate: With an isolated extension profile, open Extensions before community response then with catalogue blocked, timeout or missing Torrentio. Exactly one discoverable Torrentio fallback remains with honest error;existing remote metadata wins when present. No provider installs/enables merely by opening page. An installed-disabled instance stays installed-disabled and can be explicitly enabled through current exact-instance management without unnecessary setup browser. Explicit Configure/browser-copy-paste return installs only intended transport and preserves old configuration if replacement fails. Record APK/source hash, device Android version, navigation mode, density, launch extra, orientation and measured root viewport. Use OnePlus 11R and separately Nokia T20;no device work performed in this audit.

### WT-06

Existing: Alpha android022_tankoban_routes_test.mjs selected slide, cold/warm load, absent/disabled GetComics; Beta manga resume/Manhwa/Collections route tests preserve newer boundaries.
Add/adapt: Primary/Details selected index not first slide; cold/warm loader exactly once; switch route/profile before onLoaded prevents late read; disabled/missing comics source opens existing Extensions path; Manhwa chapter-only and Manga preferences retained.
Device gate: In Tankoban Featured move to a later manga slide and a comic slide, then tap Read and Details separately. Each opens that selected title exactly once. Repeat first cold Loader and warm retained title;Read reaches existing reader/default mode only when ready, Details stays detail. Tap Read then immediately change world/profile before Loader completes:no late reader opens. Missing/disabled GetComics routes to existing Tankoban Extensions;Manga/Manhwa defaults and saved resume identity stay correct. Record APK/source hash, device Android version, navigation mode, density, launch extra, orientation and measured root viewport. Use OnePlus 11R and separately Nokia T20;no device work performed in this audit.

### WT-07

Existing: Alpha theatre_search_year_test.mjs; Beta discover_exact_key_test and search cancellation contracts remain applicable.
Add/adapt: Exact year outranks adjacent, adjacent outranks others; missing year; year-only/numeric literal titles; parentheses/year suffix; native and network paths; do not reorder cross-medium queries unexpectedly.
Device gate: Search a known remake title with explicit year and parenthesized year against a controlled catalogue containing both releases. Exact year leads;adjacent-year fallback follows;missing-year rows do not displace exact match. Search a numeric literal title such as a year-shaped exact title and verify it remains literal. Repeat cached/native and available network result paths without changing source identity or stale query results. Record APK/source hash, device Android version, navigation mode, density, launch extra, orientation and measured root viewport. Use OnePlus 11R and separately Nokia T20;no device work performed in this audit.

### WT-08

Existing: Alpha tst_comic_download_cancel.qml checks queued/downloading, alternate-only editions, duplicate suppression, remove reset and done Read; Beta native downloader cancellation/lifecycle tests protect data.
Add/adapt: Cancel once while queued/downloading; late progress/failure/removed, missing facade/no ID, done Read remains; no delete/forget-progress side effect; row buttons disjoint/visible at actual viewport and large text.
Device gate: Open active LOCG/Collection series ledger with an edition queued/downloading. Tap Cancel twice quickly:facade gets one cancellation, progress stops and row returns to retry/acquire after canonical removed/failure event. Cancel alternate-source-only edition too;late progress must not resurrect canceled action. Retry creates intended canonical job;completed edition retains Read and no Cancel. Downloaded files, reading progress and named Collections survive cancellation and Back. Record APK/source hash, device Android version, navigation mode, density, launch extra, orientation and measured root viewport. Use OnePlus 11R and separately Nokia T20;no device work performed in this audit.

### WT-09

Existing: Beta tst_android_beta_universe_touch.qml supplied host pass; additional tst_eastblue_retained_lifecycle.qml direct disable fails while hide/show variants pass.
Add/adapt: Actual production badge press->disable/enable->release must not preview; move-disabled/hide/show/route background; fresh tap recovery; pan stealing; exactly once preview; retain mouse/keyboard interactions and existing passing controls.
Device gate: Only if a supported real interruption can toggle the pressed badge enabled state:press East Blue badge, interrupt/re-enable then release;no preview may open until fresh tap. Separately hide/show atlas during press and retain passing no-late-preview behavior;fresh tap opens once, panning steals drag and keyboard/mouse preview still works. Record if production direct-disable path cannot be reached;host-only failure then remains hardening evidence, not device defect. Record APK/source hash, device Android version, navigation mode, density, launch extra, orientation and measured root viewport. Use OnePlus 11R and separately Nokia T20;no device work performed in this audit.

### WT-10

Existing: Alpha tst_phone_galaxy_escape.qml, tst_phone_dcau_layout.qml shell escape tests; Beta shell_back_policy_test.mjs only policy does not prove loaded-page consumption.
Add/adapt: Actual loaded page destination/hub->Back returns true and universe stays loaded; next Back root closes once; repeated Back, open/close, keyboard Escape/native signal; OnePiece and Saga/Shelves unaffected.
Device gate: Open Star Wars Galaxy destination then press native Android Back:destination closes to Galaxy landing while Universe remains open;next Back closes Universe once. Open DCAU hub:Back returns landing, second Back leaves Universe. Repeat visible Back and hardware Escape where present, rapid repeated Back and reopening. Player/modal/Collections precedence must still consume before any Universe close. Record APK/source hash, device Android version, navigation mode, density, launch extra, orientation and measured root viewport. Use OnePlus 11R and separately Nokia T20;no device work performed in this audit.

### WT-11

Existing: Alpha tst_phone_atlas_previews.qml bounds+actions; Beta universe touch pass covers gesture/persistence but not every supported viewport/font.
Add/adapt: Actual supported root dimensions plus 354x792,792x354,320 px short, large text; open every arc, index, noncanon, scroll long copy; actions inside viewport and reachable; desktop shape baseline; preview placement obstacles/focus return retained.
Device gate: Record actual East Blue root canvas under both relevant zoom launches, portrait/landscape and split/resized window if supported. Open longest arc preview and index/noncanon list at narrowest supported canvas and Android large text. Copy/summary can scroll as designed and Open Arc/Close remain inside visible viewport, reachable and route once. Pan/zoom map, return from arc and ensure preview placement avoids dock/controls and focus restores;do not infer failure from a354 px host fixture if device canvas is wider. Record APK/source hash, device Android version, navigation mode, density, launch extra, orientation and measured root viewport. Use OnePlus 11R and separately Nokia T20;no device work performed in this audit.

### WT-12

Existing: Alpha test_android_unsigned_release.py, test_android_runtime_packaging.py/test_android_runtime_packaging_eol.py; Beta runtime bundle/APK verifier and startup readiness checks protect current graph.
Add/adapt: Fixture ZIP signing blocks/JAR signatures, wrong ABI/missing libs, wrong engine hash, package/channel/version mismatch, dirty overlay attribution, source drift; legitimate Beta staging/qmldir LF/CRLF inputs; tool emits no secrets. Later build/sign/upgrade acceptance separate.
Device gate: No device interaction is needed to validate tool mode/ZIP/provenance fixtures. A later separately authorized packaged-beta acceptance records exact source+overlay/native hashes, signing certificate, package/version and data-preserving upgrade from matching Beta package;launch verifies profile/library/progress survive and current Media3/reader startup remains. This audit does not build, sign, install or qualify upgrade. Record APK/source hash, device Android version, navigation mode, density, launch extra, orientation and measured root viewport. Use OnePlus 11R and separately Nokia T20;no device work performed in this audit.


## Worlds and test-source adoption supplement

# S7 test adoption analysis

This is test-source analysis. No test/build/device command was executed. test-adoption.csv contains275 file paths: 85 Alpha-only, 79 differing shared and 111 Beta-only. This is not275 missing or adopted tests, and no tests were added. A conservative source-role split labels30 build/fixture/helper/metadata assets and 245 remaining assertion/probe sources; neither is a test-case count. The164 original S7 entries are included. Tests are coverage assets, not additional product features.

Highest-value Alpha assertions are selection identity/failure/Back (WT-01), account IME reveal(WT-02), codec evidence and explicit library argument(WT-03), native font preference consumption(WT-04), Torrentio fallback(WT-05), featured routing(WT-06), release-year ranking(WT-07), ledger cancellation(WT-08), nested Universe Back(WT-10), and constrained preview bounds(WT-11).

Do not copy tests that require Alpha phoneLayout/scaffold/poster density or Android no-Home root navigation. Split android022 routes: featured routing is useful, but old reader Back expectations conflict with newer Beta reader return/progress. Installed-state test defines installed=enabled; Beta intentionally separates installed instances from enabled state, so rewrite against exact-instance enable/disable instead. Old Nuvio default-OFF assertions are obsolete.

Avoid duplicating Beta coverage: native tst_android_download_recovery already covers pinned/legacy resume, lifecycle leases, failed cancellation, missing partial and local-file honesty; delivered touch suites are stronger than old basic long-hold tests; Beta reader generation/fonts/selection/cancellation and arc74 progress restore tests remain protected. The Alpha Nuvio fixture gate belongs under Beta tests/support, not production code.

Supplied independent host receipts:15 QML suites,118 behavior/data rows plus 30 init/cleanup rows=148 passing rows; not148 behavior tests. Additional existing comic baseline14 rows=12behaviors; single-scroller6 rows=4 cases. Node gesture15 cases, selection and boot reports; staged QML gate61 roots source compile, not APK/device execution. Additional EastBlue direct-disable probe has4 passing rows,1 failing row; normal hide/show controls pass. Its enabled binding is normally constant touchInput, so production trigger remains unresolved. Physical Android remains unverified by these receipts; user consumption observations are narrower than full acceptance.

Test registration: several Alpha-only source-method tests are CTest-registered in Alpha tests/CMakeLists5089+; absence from Beta test graph is not proof their behavior is absent. Any accepted adoption includes current runner registration, isolated fixture state and precise expected contract. Native/device utilities are never run reflexively.

Rollback for later adoption: source/test commit revert only. Fixture profiles/temp paths must be isolated; no personal store reset, no credentials/network needed for geometry tests. Device tests record source/APK/density/launch/orientation and qualify OnePlus 11R and Nokia T20 separately.


The complete per-test list is `worker-worlds-tests/test-adoption.csv` (275 test-source entries, each with adoption/skip/adaptation rationale and expected behavior). These are test assets, not 275 additional product features.
