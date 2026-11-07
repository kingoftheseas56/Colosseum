# Mechanism roadmap
Beta remains the better foundation. Switching to Alpha would require re-porting newer canonical state, Collections, reader continuity and lifecycle work. Use Alpha as a donor of bounded mechanisms while retaining Betaâ€™s approved desktop presentation.

This audit authorizes no implementation. Group coupled state changes into coherent units; do not require an APK build after each trivial edit. Source-supported defects come first; typography/defaults and root navigation choices require a concrete decision or preview. Branding and release tooling are separate from functional gaps.

## Shared qualification and recovery protocol

For each accepted unit, use isolated host fixtures against the actual owner and run the smallest applicable existing regressions. Assert unchanged desktop geometry/behavior where shared components change. Then package the coherent unit with exact source/overlay/APK identity, preserve the signer/package/update path and qualify its specific behavior on OnePlus 11R; Nokia T20 requires a separate session. Record actual Qt/native surface dimensions, Android version, orientation, text scale and launch zoom. Portrait checks apply where the route supports portrait; the current Beta manifest is landscape-oriented.
A revert must preserve application storage, credentials, downloads, Collections, library and progress. Revert coupled signal/caller changes together; use a normal same-package, same-signer, version-compatible update rather than uninstalling or clearing data. No store migration is proposed by these units. Full per-finding dependencies, risks, tests and recovery text remain in gap-register.csv.

## 1. Constrained surfaces and input policy

### PF-02 â€” Sources geometry and action reachability

Constrain header/banner by actual available height or include header in a single deliberate scroll owner; retain provider filters/actions and explicit source pin. Apply analogous constrained budget to ComicTorrentSourcesPage and MangaTankobanSourcesPage only after their own geometry checks.
Integration: qml/SourcesSheet.qml bannerStrip, topBar, pills, table/tableHead/list; qml/TheatreSeries.qml SourcesSheet2437 and qml/TheatreSeriesView.qml2248; analogous separate budgets in ComicTorrentSourcesPage/MangaTankobanSourcesPage.
Preserve: Preserve desktop look at sufficient size, provider ordering/codec flags, download pinning, no auto source choice, same identity/progress.
Host acceptance: New actual-component Sources fixture at rows/no rows, visibleRows0, one/multiple pill rows, extensions menu and 150-unit rows. Existing empty-extensions and source-picker fixtures cover only part.
Device acceptance: Record actual window/sheet/list rectangles and Qt scale on OnePlus 11R default 1.15 and override 1.20; reach final source, copy/download, extension menu and Back. Independently qualify T20.

### PF-01 â€” Shared control touch sizing

Add presentation-independent input metrics, then adapt shared TopBar/actions/download row hosts and the active account/world callers as one tested control-policy unit. Size host AND pointer owner; avoid overlapping adjacent hit regions.
Integration: qml/AdaptiveLayout.qml touch metric API; qml/TopBar.qml SysIcon and active action Items; qml/DownloadsPage.qml action hosts; shared KeyboardAction remains input owner. Coordinate SelectionMenu/LeftPanel BR-READER-TARGETS and Theme WT-04.
Preserve: Preserve desktop dimensions, existing KeyboardAction semantics/accessibility, Collections actions, active canonical owners; no phone composition.
Host acceptance: Adopt/adapt tst_phone_download_targets.qml, tst_phone_touch_chrome.qml; retain tst_android_shared_action_touch and poster suite. Test edge taps, neighbor separation, drag steal/cancel and desktop geometry.
Device acceptance: OnePlus 11R at both launch zooms; T20 independently; touch and mouse, text scaling, safe edges and landscape.

### PF-05 â€” Player speed/sleep/skip popup reachability

One popup unit: bounds, height-aware scroll, focus reveal, resize relocation and cancellation; inspect other audio/fill/watchparty popups as same-route checklist, not blanket claimed defects.
Integration: qml/PlayerPage.qml speedPopup7127+, columnsHeight/skipStepTop, height, placement and scroll body; sm opener; existing More overflowPanel and SubtitleMenu viewport techniques.
Preserve: Playback state, desktop geometry, session demand epoch, watchparty capability checks and existing gesture ownership.
Host acceptance: New actual-speed popup fixture shorter than 500 and long sleep-timer row; retain player_more_touch, subtitle_viewport and seek cancellation suites.
Device acceptance: Reach first/last speed, sleep and skip controls in actual OnePlus landscape with HUD open, zoom 1.15/1.20, resize/IME and hardware focus.

### BR-READER-TARGETS â€” Reader actions remain present but lose Alpha larger targets and width safety

One reader overlay/action reachability change, sharing policy with general controls; tests across short/narrow viewport and native-like reparented window.
Integration: SelectionMenu swatch/TextAction/save hit items and cardW; LeftPanel bookmark activation/delete target ownership. Coordinate with shared sizing implementation to avoid stacking redundant enlarged MouseAreas.
Preserve: Beta nativeOverlay Keys.forwardTo, surface-based focus target and logical keyboard mapping; no note-dismissal on Android selectionCleared; desktop dimensions/style unchanged; dictionary/footnote keys.
Host acceptance: Re-use Beta fake IME; add narrow widths, short landscape, keyboard visible, color/note/copy/delete touch bounds and no row jump from delete gutter; reparented popover Escape/Tab, save/cancel/draft retention, no action on cancelled drag; desktop geometry assertions.
Device acceptance: OnePlus 11R tap swatches/delete/save at normal and enlarged Android font settings; hold/select, open note keyboard, save/cancel, dictionary/footnote dismiss and Back both orientations, density 1.15/1.20; Nokia T20 independently.

### WT-02 â€” Account forms lost explicit keyboard avoidance and focused-field reveal

One shared account form viewport/focus unit, using inputMethod injection for deterministic host checks; preserve active panel and form draft across resize.
Integration: qml/account/AccountPageFrame.qml Flickable72/contentHeight80 and focus tracker; qml/account/AccountField.qml TextField; AccountOnboarding142 and AccountSignIn/Create/Recovery descendants. Reuse qml/ViewportAvoidance.js current logical-coordinate helpers; coordinate Main keyboard-first Back without changing credential native owner.
Preserve: Keep native Android credential vault, profile adoption/generation boundaries and existing account intents; never log field values. Keep Beta newer account rail/window controls.
Host acceptance: Portrait/short landscape/tablet with synthetic visible keyboard, nonzero and zero rectangles, switch first/last fields, rotation, close/reopen; assert focus and Save/Continue reachable; desktop no added inset. Device floating/standard keyboard and Back dismissal with no credential submit required.
Device acceptance: Open Account create/sign-in/recovery form with a disposable test identity;focus the last editable field using real standard then floating keyboard. The focused field and Continue/Save remain reachable by scroll without losing entered draft. Switch upper/lower focus, rotate with keyboard visible, hide/reopen keyboard; no double inset or covered action. Native Back dismisses keyboard before form/page. Repeat with Android large text and a keyboard reporting no rectangle if available; desktop host baseline gets no artificial IME margin. Do not submit real credentials merely for geometry. Record APK/source hash, device Android version, navigation mode, density, launch extra, orientation and measured root viewport. Use OnePlus 11R and separately Nokia T20;no device work performed in this audit.

### WT-04 â€” Android system text-size preference no longer reaches shared UI text

One shared Theme consumption policy plus coherent common-control/account/library typography adaptations; extend content-driven heights where scaling would clip. Do not bulk-copy Alpha files.
Integration: qml/Theme.qml add Android-only fontScale consumer/fontSize token over retained PlatformRuntime; migrate common BackAction/WidgetHeader/account/library/active detail typography callsites and content heights. Preserve native/platform/PlatformRuntime.cpp374-380 and Activity notifications, no duplicate bridge.
Preserve: Keep Beta desktop composition, Collections/reader font guards, single scroll owners and active card geometry; scaling without containment checks is incomplete.
Host acceptance: Native-notify seam changes1->1.3->2 and verifies actual font pixels; desktop1; test long titles/captions/translated labels and actions at large text across supported viewports, dynamic change without restart, no duplicate density multiplier.
Device acceptance: Change Android system text size at 1.0,1.3 and 2.0 (or device supported nearest values) while app is open. Shared Back/navigation, account labels, library captions and setup status text update through native preference notification. Long labels wrap/elide with primary actions inside bounds and reachable;no double density enlargement at default 1.15/intent1.20 visual zoom. Return text size to original after observation;reader content font choice remains independent and desktop baseline stays unchanged. Record APK/source hash, device Android version, navigation mode, density, launch extra, orientation and measured root viewport. Use OnePlus 11R and separately Nokia T20;no device work performed in this audit.

## 2. Exact playback and Continue recovery

### BR-SOURCE-REPLACEMENT â€” Changing the source of a retained same-content session can resume its old stream

One coupled native signal + QML warm invalidation change, plus native and host fake-player assertions in one coherent commit.
Integration: SessionStore.openOrSwitch same-content update branch and source identity helper/signal; Main existing targetReplaced Connections and activateMovieSession warm ownership.
Preserve: Beta canonical contentKey, one-tab-per-show, browser/session generation, saved position; latest Nuvio foreground/download isolation and Media3 teardown. Compare source URL/localPath/hash/file identity deliberately; do not reload for title/art/candidate list changes. Capture/checkpoint the latest currently active player position before replacing transport; do not restore stale savedState. Integrate existing media request/demand/lifecycle generations so old callbacks cannot land.
Host acceptance: Reintroduce/adapt source identity test: same content different hash/file/url reloads exactly once, preserves position; metadata-only/case-normalized hash no reload; parked and already-active Main session path uses newly selected descriptor; switch different episode clears old resume; download prefetch unaffected.
Device acceptance: OnePlus 11R portrait and landscape: pause/minimize same episode sourceA, choose sourceB, confirm actual route changes and resume position retained; repeat failed source and app-background transitions with video download running; Nokia T20 separately unqualified.

### PF-04 â€” Deleted local-video Continue recovery

One coherent unit: availability check + exact title/episode source route + async detail-load continuation + resumePosition forwarding + explicit-resume guard. Retain newer player local-error retry and generation/identity gates.
Integration: qml/Main.qml resumeContinue/openTheatreSeries/openMovieSession and theatreSeriesLayer.onLoaded; TheatreSeries/View.openResumeSources; SourcesSheet playbackContext; PlayerPage playTorrent/explicit-resume state/prepareResumeChoice.
Preserve: No silent replacement, no progress rewrite, preserve Collections canonicalSourceId checks and current local-player retry.
Host acceptance: Adopt android_continue_missing_download_test.mjs, downloadstore_local_file_availability_harness.cpp, android_stream_retry_resume_test.mjs; test present/missing/noncatalogue files,95 percent explicit resume, stale load, both title variants.
Device acceptance: Delete/move controlled test media externally, Continue, choose source/cancel, verify same episode and position; no library/progress loss; offline and no-source cases.

## 3. Native Back consumption and visible routes

### PF-03 â€” Nested transient Back dispatch

Restore transient delegation in both Theatre title variants and Sources; include Manga detail owner only with its worker-reviewed order. Keep IME-first and Collections draft guard.
Integration: qml/Main.qml handleEscape theatreSeries branch; qml/TheatreSeries.qml and TheatreSeriesView.qml requestEscape; qml/SourcesSheet.qml requestEscape/closeSourceContext/closeExtMenu.
Preserve: Preserve single native Back owner, Collections drafts, catalogue cancel semantics and desktop Escape routes.
Host acceptance: Adopt android_theatre_nested_back_test.mjs and shell_back_policy_test; actual component spy order for menus, sheet, title; native dispatch fixture; no double invocation.
Device acceptance: Native gesture and three-button Back through IME, Sources filter/context, source sheet, title; focus restoration; repeat/cancel predictive gesture.

### WT-10 â€” Galaxy and DCAU nested Back bypasses their local state

One two-page nested Back contract unit + shell integration tests; no new navigation owner.
Integration: qml/GalaxyUniversePage.qml requestEscape115-120 and qml/DCAUUniversePage.qml navigateBack50-54/restore boolean requestEscape; preserve qml/Main.qml requestUniverseEscape1294-1300 and native handleEscape Universe1386; bespoke Loader5674-5676.
Preserve: Keep Beta shell precedence Collections/modals/player and world navigation; do not restore Alpha Android no-Home policy.
Host acceptance: Actual loaded page destination/hub->Back returns true and universe stays loaded; next Back root closes once; repeated Back, open/close, keyboard Escape/native signal; OnePiece and Saga/Shelves unaffected.
Device acceptance: Open Star Wars Galaxy destination then press native Android Back:destination closes to Galaxy landing while Universe remains open;next Back closes Universe once. Open DCAU hub:Back returns landing, second Back leaves Universe. Repeat visible Back and hardware Escape where present, rapid repeated Back and reopening. Player/modal/Collections precedence must still consume before any Universe close. Record APK/source hash, device Android version, navigation mode, density, launch extra, orientation and measured root viewport. Use OnePlus 11R and separately Nokia T20;no device work performed in this audit.

### WT-06 â€” Featured Read and Details controls have no world handlers

One featured route unit: two carousel handlers, Main route/helper and cold/warm deferred read cancellation; selected-slide identity through all paths.
Integration: qml/TankobanWorld.qml FeaturedCarousel204-209 add primary/secondary forwarding; qml/Main.qml world onLoaded current handlers and current openSeries/openWestern/openExtensionsPage; series Loader cold/warm generation and existing readPrimary entry. Do not override mode/resume/Collections.
Preserve: Keep Beta canonical catalogue IDs, Manga/Manhwa modes, named Collections return, reader resume/progress/generation and pixel-gated presented behavior.
Host acceptance: Primary/Details selected index not first slide; cold/warm loader exactly once; switch route/profile before onLoaded prevents late read; disabled/missing comics source opens existing Extensions path; Manhwa chapter-only and Manga preferences retained.
Device acceptance: In Tankoban Featured move to a later manga slide and a comic slide, then tap Read and Details separately. Each opens that selected title exactly once. Repeat first cold Loader and warm retained title;Read reaches existing reader/default mode only when ready, Details stays detail. Tap Read then immediately change world/profile before Loader completes:no late reader opens. Missing/disabled GetComics routes to existing Tankoban Extensions;Manga/Manhwa defaults and saved resume identity stay correct. Record APK/source hash, device Android version, navigation mode, density, launch extra, orientation and measured root viewport. Use OnePlus 11R and separately Nokia T20;no device work performed in this audit.

## 4. Provider fallback and informed source choice

### WT-03 â€” Android decoder compatibility annotations and soft ranking are not consumed

One codec evidence pipeline from existing JNI capability DTO through AddonClient callers and SourcesSheet label; coordinate with platform owner for DTO completeness and shared Sources geometry.
Integration: qml/AddonClient.js parseStream243, _sortRows324, loadStreams491; qml/SourcesSheet.qml load198-221 and row compatibility label; existing native PlatformRuntime.videoDecodeCapabilities. qml/player2host/ColosseumHostServices185 is desktop preservation only and excluded Android.
Preserve: Keep Beta provider identity, cancellation/generation paths, queued Media3 init and retry lifecycle. Do not equate advisory support with real hardware acceptance.
Host acceptance: Contradictory/missing hints=>unknown; missing/incomplete inventory=>unknown; AVC profile coverage/level, HEVC Main10, AV1, DV base-layer ambiguity; explicit runtime from .pragma library; all callers; rows retained and ordering stable; async stale callbacks unchanged.
Device acceptance: Open Theatre title Sources with controlled source fixtures or recorded providers: one explicitly compatible codec/profile/level, one explicitly unsupported, one absent/ambiguous hint. All three remain selectable/visible;known unsupported is labeled and softly ranked below others while unknown stays unknown. Select the known compatible sample to verify ordinary playback route still works;choosing unknown must not be blocked. Reopen after capability/config change and ensure stale request does not relabel another title. Actual decoder playback acceptance uses known licensed/local fixture media and is distinct from advisory label correctness. Record APK/source hash, device Android version, navigation mode, density, launch extra, orientation and measured root viewport. Use OnePlus 11R and separately Nokia T20;no device work performed in this audit.

### WT-05 â€” Torrentio fallback card is lost when community catalogue omits/fails

One pure catalogue normalization/fallback unit plus page initial list, retaining current native Extensions management and explicit user install/enable action.
Integration: qml/ExtensionsStoreApi.js fetch completion/error normalization, essentials116/rows125; qml/ExtensionsStorePage.qml catalogue28 only. Preserve isInstalled80, finishInstallOperation146, ExtensionsInstalledPage568 and Android setup Loader625;never replace management transaction owner.
Preserve: Keep Beta per-transport instance identity, expectedId guard, URL redaction, replacement failure recovery and profile invalidation.
Host acceptance: Community list failure/timeout/partial results with honest error; known remote metadata wins; one stable fallback; no automatic install/enable; disabled installed entry stays disabled until explicit action; current Configure/replace paths unchanged.
Device acceptance: With an isolated extension profile, open Extensions before community response then with catalogue blocked, timeout or missing Torrentio. Exactly one discoverable Torrentio fallback remains with honest error;existing remote metadata wins when present. No provider installs/enables merely by opening page. An installed-disabled instance stays installed-disabled and can be explicitly enabled through current exact-instance management without unnecessary setup browser. Explicit Configure/browser-copy-paste return installs only intended transport and preserves old configuration if replacement fails. Record APK/source hash, device Android version, navigation mode, density, launch extra, orientation and measured root viewport. Use OnePlus 11R and separately Nokia T20;no device work performed in this audit.

### PF-06 â€” Provider metadata failure recovery

Keep optional caller snapshot projection, require canonical expected ID and movie type; use only after live metadata misses. Update both title variants together.
Integration: qml/TheatreApi.js catalogueMovieMeta/loadMeta optional record; qml/TheatreSeries.qml load callback and TheatreSeriesView.qml load callback; retain expectedCanonicalId match guard.
Preserve: Collections sourceId versus canonicalSourceId semantics; no second authority/cache; no fabricated series episodes.
Host acceptance: Add loadMeta stubs for primary/extension misses, exact/mismatched id, series exclusion, canonical provider-door distinction; existing title canonical/Collections tests retained.
Device acceptance: Offline/cache/no-extension movie detail, retry after providers return; same source identity and artwork, no substituted film.

### WT-07 â€” Explicit release-year top-match ranking removed

One WorldSearch ranking unit plus deterministic production-function tests.
Integration: qml/WorldSearch.js insert adapted pickTheatreTopMatch and call from searchTheatre110-129 plus native IMDb result mapping323-326; leave qml/SearchSurface.qml paged provider adapter intact.
Preserve: Keep Beta typed data IDs, explicit-content policy, async generation/cancel and no per-keystroke broad network changes.
Host acceptance: Exact year outranks adjacent, adjacent outranks others; missing year; year-only/numeric literal titles; parentheses/year suffix; native and network paths; do not reorder cross-medium queries unexpectedly.
Device acceptance: Search a known remake title with explicit year and parenthesized year against a controlled catalogue containing both releases. Exact year leads;adjacent-year fallback follows;missing-year rows do not displace exact match. Search a numeric literal title such as a year-shaped exact title and verify it remains literal. Repeat cached/native and available network result paths without changing source identity or stale query results. Record APK/source hash, device Android version, navigation mode, density, launch extra, orientation and measured root viewport. Use OnePlus 11R and separately Nokia T20;no device work performed in this audit.

### PF-07 â€” Track names and MIME labels

One label helper unit shared by active track lists; keep actual track IDs/language/delay untouched.
Integration: qml/PlayerPage.qml subtitleDisplayTitle/subtitleRow/trackCodecLabel; qml/SubtitleMenu.qml codecLabel display helper if shared use warranted.
Preserve: Track identity, selection, sidecar subtitle URL/security and stream restoration.
Host acceptance: Pure helper tests: generic/custom title, language unknown, MIME known/unknown, internal cues; verify selected ID unchanged.
Device acceptance: Media3 movie with multiple embedded/fetched languages; select and resume same track across reload.

### PF-08 â€” Background-download failure explanation

One read-model/error-display unit with empty/error/recovered states; preserve current delete-all state machine.
Integration: qml/DownloadsPage.qml backgroundDownloadMessage binding + dedicated visible error Text; leave native/platform/PlatformDownloads.cpp acceptHostState and native/main.cpp1627-1659 gating unchanged.
Preserve: Native service readiness gates, error epochs, job persistence and delete-all cancel-drain/yield semantics.
Host acceptance: Fixture injected host error/recovery and stale epoch; no service execution required for host check; native host-state tests retained.
Device acceptance: Deny notification/service prerequisite in controlled device profile, see explanation and recovery; no lost jobs.

## 5. Cancellation and transient completion

### WT-08 â€” Comic edition ledger loses in-place cancellation

One edition action/lifecycle unit, optionally aligned with source-inspection Cancel owner reviewed by platform S4.
Integration: qml/ComicDbLedger.qml inFlight row/action area213-252,357-388; canonical facade native/engine/ComicDownloader.h182/cpp1113; active qml/ComicSeriesPage.qml ledger282 and Main.openComicSeries2165/_openComicSeriesReady2179 remain owners.
Preserve: Keep Beta downloader safe move/durable path/cancellation ownership, named Collections and exact source IDs. Do not change backend teardown merely to add a UI action.
Host acceptance: Cancel once while queued/downloading; late progress/failure/removed, missing facade/no ID, done Read remains; no delete/forget-progress side effect; row buttons disjoint/visible at actual viewport and large text.
Device acceptance: Open active LOCG/Collection series ledger with an edition queued/downloading. Tap Cancel twice quickly:facade gets one cancellation, progress stops and row returns to retry/acquire after canonical removed/failure event. Cancel alternate-source-only edition too;late progress must not resurrect canceled action. Retry creates intended canonical job;completed edition retains Read and no Cancel. Downloaded files, reading progress and named Collections survive cancellation and Back. Record APK/source hash, device Android version, navigation mode, density, launch extra, orientation and measured root viewport. Use OnePlus 11R and separately Nokia T20;no device work performed in this audit.

### PF-14 â€” Comic pack inspection cancellation

One picker inspection unit; retain typed outcomes, stale issue guards and safe ledger handoff.
Integration: qml/ComicTorrentSourcesPage.qml cancelInspection beside hide/beginAutomaticDownload and inspecting body562+; retain applyAcquisitionFailure/closeOnSafeProgress issue guards.
Preserve: Comics pack identity/security, typed acquisition state, no automatic weak source acceptance.
Host acceptance: Adopt tst_comic_download_cancel.qml; test inspect cancel then fresh selection, late completion, ambiguous/incomplete/combined states.
Device acceptance: Cancel slow metadata inspection and choose different source; verify torrent/temp cleanup and no double cancellation.

### WT-09 â€” East Blue direct-area disable permits a late preview on host

One East Blue badge lifecycle change + retained negative-control fixture; no blanket touch rewrite.
Integration: qml/OnePieceEastBlueAtlas.qml badge MouseArea438-457 moved latch/onPressed/onCanceled/onClicked; supplied retained lifecycle fixture. First establish production enabled transition; do not replace map or shared touch owner.
Preserve: Keep delivered Android persistent previews and focus return/arc routing. Do not replace Beta with Alpha onClicked-only badge.
Host acceptance: Actual production badge press->disable/enable->release must not preview; move-disabled/hide/show/route background; fresh tap recovery; pan stealing; exactly once preview; retain mouse/keyboard interactions and existing passing controls.
Device acceptance: Only if a supported real interruption can toggle the pressed badge enabled state:press East Blue badge, interrupt/re-enable then release;no preview may open until fresh tap. Separately hide/show atlas during press and retain passing no-late-preview behavior;fresh tap opens once, panning steals drag and keyboard/mouse preview still works. Record if production direct-disable path cannot be reached;host-only failure then remains hardening evidence, not device defect. Record APK/source hash, device Android version, navigation mode, density, launch extra, orientation and measured root viewport. Use OnePlus 11R and separately Nokia T20;no device work performed in this audit.

### WT-11 â€” East Blue transient cards lack window width caps

One constrained East Blue index+preview surface unit after shared typography/target sizing policy; reuse existing ViewportAvoidance/clamp owner where suitable.
Integration: qml/OnePieceEastBlueAtlas.qml index608-615, preview820-827, preview content845-932 and existing schedulePreviewGeometry/placement/focus methods. Adapt caps/scrollable copy/action region only, not phone zoom or map layout.
Preserve: Keep delivered preview persistence, hover suppression, panning and newer focus restoration; do not copy phone zoom/default layout.
Host acceptance: Actual supported root dimensions plus 354x792,792x354,320 px short, large text; open every arc, index, noncanon, scroll long copy; actions inside viewport and reachable; desktop shape baseline; preview placement obstacles/focus return retained.
Device acceptance: Record actual East Blue root canvas under both relevant zoom launches, portrait/landscape and split/resized window if supported. Open longest arc preview and index/noncanon list at narrowest supported canvas and Android large text. Copy/summary can scroll as designed and Open Arc/Close remain inside visible viewport, reachable and route once. Pan/zoom map, return from arc and ensure preview placement avoids dock/controls and focus restores;do not infer failure from a354 px host fixture if device canvas is wider. Record APK/source hash, device Android version, navigation mode, density, launch extra, orientation and measured root viewport. Use OnePlus 11R and separately Nokia T20;no device work performed in this audit.

## 6. Packaging, branding and engineering tools

### PF-10 â€” Android package/version input validation

Single packaging-contract unit accepting current Beta lane plus explicit release/diagnostic policy; validate numeric bounds and distinct IDs.
Integration: native/cmake/android.cmake191-211 cache validation before QT_ANDROID_PACKAGE_NAME/QT_ANDROID_VERSION_CODE/QT_ANDROID_VERSION_NAME target assignment.
Preserve: Current authorized separate beta package, versionCode monotonic updates, signer and installed data.
Host acceptance: Adopt/adapt Android identity/packaging tests from worker test register; negative config cases in disposable fixture when implementation authorized.
Device acceptance: Future signed update preserves same app data and intended separate Alpha/Beta install; inspect generated manifest.

### PF-09 â€” Android branding resources (nonfunctional polish)

One resource+manifest unit; keep world.getcolosseum.app.beta side-by-side identity and release signer.
Integration: native/platform/android/AndroidManifest.xml application icon/roundIcon; four res/drawable, mipmap-anydpi-v26, values donor resources copied by existing Android package loop.
Preserve: Package identity/signature/data and desktop icon unchanged.
Host acceptance: Resource XML references and merged-manifest/icon membership check in future packaging; no build run here.
Device acceptance: Launcher round/square masks, dark/light/theme launcher and app label on both devices.

### WT-12 â€” Alpha has structured unsigned APK identity/provenance qualification

One release qualification tool unit: mode validation, source snapshot, artifact checks, manifest generation, fixture-only tests, CI opt-in; no execution in audit.
Integration: scripts/android/build_apk.sh current debug config; .github/workflows/android.yml explicit optional qualification lane; add adapted scripts/android/release_apk.py helper/tests to current CMake runtime manifest/native staging graph with current Beta identity. No automatic workflow dispatch.
Preserve: Keep Beta queued Media3 startup/runtime manifest/reader boundaries and debug workflow; do not copy obsolete candidate Nuvio default-OFF assertions or preview package naming.
Host acceptance: Fixture ZIP signing blocks/JAR signatures, wrong ABI/missing libs, wrong engine hash, package/channel/version mismatch, dirty overlay attribution, source drift; legitimate Beta staging/qmldir LF/CRLF inputs; tool emits no secrets. Later build/sign/upgrade acceptance separate.
Device acceptance: No device interaction is needed to validate tool mode/ZIP/provenance fixtures. A later separately authorized packaged-beta acceptance records exact source+overlay/native hashes, signing certificate, package/version and data-preserving upgrade from matching Beta package;launch verifies profile/library/progress survive and current Media3/reader startup remains. This audit does not build, sign, install or qualify upgrade. Record APK/source hash, device Android version, navigation mode, density, launch extra, orientation and measured root viewport. Use OnePlus 11R and separately Nokia T20;no device work performed in this audit.

## 7. Decisions and investigations before code

### PF-11 â€” Android shell root and predictive Back (decision/preview)

One shell Back contract unit including root, transient ladder and player policy; no separate Android navigation stack.
Integration: One shell Back contract unit including root, transient ladder and player policy; no separate Android navigation stack.
Preserve: Warm-demand lifecycle, IME-first, Collections drafts, native callback single delivery and desktop Escape.
Host acceptance: Adopt back harness with updated desired root/player expectations; cancellation/end phases; no duplicate Shortcut dispatch.
Device acceptance: Gesture cancel versus commit, Home root Back, warm player resume, service/download background and fresh launch; both devices.

### PF-15 â€” Android window verbs in shared desktop chrome (decision/preview)

One shared chrome + remaining inline verbs unit, independent of target sizing; preserve real player fullscreen/orientation actions.
Integration: One shared chrome + remaining inline verbs unit, independent of target sizing; preserve real player fullscreen/orientation actions.
Preserve: Desktop unchanged; genuine media fullscreen retained; canonical navigation and Collections.
Host acceptance: Host Android/desktop visibility and layout spacing fixtures across shared and full-screen pages.
Device acceptance: Home/world/detail/genre pages show correct remaining actions; no accidental task quit; player still rotates/fullscreens correctly.

### PF-12 â€” Resize anchor and virtualized episode geometry (decision/preview)

One geometry/virtualization unit if supported; do not add competing scroll owner or phone header.
Integration: One geometry/virtualization unit if supported; do not add competing scroll owner or phone header.
Preserve: Current one-scroller/virtualization owners, desktop composition and canonical episode identity.
Host acceptance: Future fixtures resize/IME, change hero synopsis/ordering, long episode list and nonzero contentY; assert final row/action realized and viewport anchor.
Device acceptance: Orientation as allowed, split/window resize and IME on OnePlus/T20; distinguish stutter/input from position change.

### PF-13 â€” Automatic fallback film identity heuristic (decision/preview)

Separate automatic-choice policy unit with explicit identity evidence; retain manual choice and source ordering.
Integration: Separate automatic-choice policy unit with explicit identity evidence; retain manual choice and source ordering.
Preserve: Provider replaceability, current Media3 demand/restore epochs, no silent canonical identity rewrite.
Host acceptance: Table fixtures remakes/collections/foreign title/short tokens/no year/episodes; verify no legitimate source silently pruned.
Device acceptance: Controlled multi-source failure/switch same title, explicit picker still includes all eligible rows.

### BR-EPUB-DEFAULTS â€” Phone-specific EPUB defaults were removed; evaluate narrow-viewport defaults without migrating saved choices (decision/preview)

One appearance fallback policy plus normalization/migration-precedence regression tests, previewed in actual renderer; no schema migration.
Integration: Reader2Logic appearanceDefaults/appearanceToPaper; ReaderShell passes effective saved appearance unchanged. If viewport-dependent fallback introduced, derive from actual reader surface, not desktopPresentation.
Preserve: Reader2Bridge profile stores, generation stamping, source origin/CSP/publication script guard, font load safeguards; saved defaults and per-book sparse patches; desktop defaults and layout.
Host acceptance: Fresh/no-reader2, legacy-flat, saved default and per-book override cases; Android narrow/wide viewport fallback; desktop unchanged; fixed-layout content ignores reflow settings. Assert top/bottom and computed2..18% gap against actual innerWidth, not2*margin approximation.
Device acceptance: OnePlus 11R portrait+landscape at both relevant density launch settings; inspect actual EPUB WebView viewport and text, page turn/resume after settings changes; Nokia T20 separate. Compare fresh test profile and saved24/72 preferences.

### BR-COMIC-DEFAULT â€” Android lane default differs; working reader observation does not establish a layout defect (decision/preview)

None authorized now. A later accepted fallback adaptation would be one policy change with precedence/rotation checks, not a reader transplant.
Integration: No implementation planned pending concrete viewport preview. If evidence supports a change, adapt only unspecified lane fallback in ComicReaderShell preference loading; do not rewrite saved preferences or copy Alpha shell.
Preserve: Preserve explicit global/per-series user choices, paired mode as an available choice, desktop defaults, Beta numbered-chapter resume and pixel/strip restoration progress guards.
Host acceptance: Proposed bounded check, not executed: fresh unspecified preference versus explicit single/paired/strip and global/per-series precedence at narrow/wide and short/tall reader viewports; reopen/rotation must retain explicit choice and no provisional progress overwrite.
Device acceptance: Fresh test profile portrait/landscape manga and western comic, paired/single/strip; chosen mode survives reopen/rotation; inspect decode presentation and controls.

### WT-13 â€” IMDb shorts generation difference needs content/provenance decision (decision/preview)

Conditional dataset policy+fixture validation+publication provenance only after evidence/decision.
Integration: scripts/theatre_brain/build_imdb_db.py KEEP_TYPES and fixture policy; server/content-api/source-manifest.json pinned dataset identity and existing native ImdbCatalog/CatalogRemote consumption. Data publication is separate authorization.
Preserve: Keep Beta catalogue filters/schema/static fallback and no implicit D1 data writes.
Host acceptance: Read-only fixture membership checks and exact dataset revisions; prove normalized movie query reachability and vote/adult policy.
Device acceptance: After intended short-film scope and exact dataset revision are established, choose a known admitted short and verify native/cached versus configured remote catalogue returns same normalized movie identity and opens ordinary detail/playback route. This remains conditional future acceptance;do not regenerate/publish or touch personal data during audit. Record APK/source hash, device Android version, navigation mode, density, launch extra, orientation and measured root viewport. Use OnePlus 11R and separately Nokia T20;no device work performed in this audit.

### WT-14 â€” Host scanner lookup no longer accepts libexec kit layout (decision/preview)

One host-kit discovery subunit, independent of Android product behavior; can join WT-12 artifact qualification preparation without coupling unrelated builds.
Integration: scripts/android/qualify_toolchain.py check_qt:131 and first_existing:84; retain main:192 validation sequence. No CMake/runtime/backend change needed.
Preserve: Keep Beta current kit/version/ABI/SDK/NDK readiness checks and debug pipeline;do not restore obsolete Nuvio candidate options.
Host acceptance: Fake host roots containing bin scanner only, exe scanner only, libexec scanner only, no scanner, and conflicting alternatives. Assert deterministic precedence/missing result and preserve Qt version/target-kit/SDK/NDK checks. Read source or test pure helper;do not invoke adb/SDK installation merely for discovery check.
Device acceptance: No physical Android check applies to pure host path discovery. Later authorized packaging on an actually supported Linux host records chosen scanner path/version and artifact provenance;device upgrade acceptance remains WT-12 separate.

### WT-15 â€” WatchParty ancestor geometry and Wallpaper narrow selection remain conditional (decision/preview)

One transient containment investigation unit with separate WatchParty and Wallpaper fixture cases;no immediate implementation commitment or global phone-layout transplant.
Integration: qml/WatchPartyPanel.qml panelEffectiveHeight/controlTopInOverlay/positionPanel; qml/WallpaperSearch.qml selected panel/button geometry; preserve Main wallpaperLayer and PlayerPage controller ownership. Add conditional bounds/reflow tests against these current components.
Preserve: Keep Beta desktop coordinate/density policy, WatchParty native controller authority/sync/lifecycle/focus containment and wallpaper canonical preferences. Do not change room/network behavior or apply Alpha whole phone layout.
Host acceptance: Instantiate actual WatchPartyPanel with carrier move only, near-top control, open/close and short resize;assert positive reachable popover/action bounds and current placement/focus. Instantiate actual WallpaperSearch with fake selected item at current supported minimum,354/792 and wide baseline;assert selected panel and both actions stay reachable, route once and no wallpaper/network side effect. Test before choosing fix.
Device acceptance: On OnePlus 11R record actual root viewport/density/default 1.15 versus intent1.20 before testing. Open WatchParty from player HUD, resize/rotate while open and verify all visible allowed actions remain reachable without altering room authority;open WallpaperSearch from Home and a world, select a fixture image and verify both selection actions visible/reachable, then Cancel preserves preference. If device canvas never reaches narrow trigger, record it rather than calling it reproduced. Nokia T20 separate;no device work performed here.
