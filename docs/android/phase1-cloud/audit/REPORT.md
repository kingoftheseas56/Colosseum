# Android Alpha-to-Beta audit
Beta remains the better foundation. Alpha contributes bounded input, recovery and reachability mechanisms; switching foundations would require re-porting newer canonical state, Collections, reader continuity and lifecycle fixes. The recommended next work is to repair the source picker and narrow action surfaces, restore exact source/Continue recovery and nested Back contracts, then qualify those mechanisms on the actual devices.

Source comparison is reconciled. Device acceptance and explicitly unresolved behavior choices remain open; no implementation is authorized by this audit.

Every inventory path has a ledger row; this is accountable source comparison, not a claim all 6,141 files were manually read or every Android action tested. Exact-byte matches, active behavior review, structural availability triage, exclusions and unresolved evidence are separate tiers.

## What matters for users

- Sources can have nearly no remaining list height under the installed zoom composition. This explains a concrete reachability risk; it does not prove all scrolling pauses share that cause. Use a Beta-specific height-aware fix while retaining desktop presentation.
- Selecting a new source for a retained same-content session can continue the old warm stream. Alpha has a small source-change invalidation mechanism. Preserve the latest live playback position and Beta request generations when adapting it.
- Small controls and system text scaling are separate issues. Restore input sizing without restoring Alpha phone composition; separately reconnect Android font preference and verify wrapping. Native reader overlays need their own hit/focus checks.
- Missing-video Continue recovery, nested Theatre/Universe Back, featured Tankoban actions, codec warnings and source/ranking fallbacks are specific transfers. Existing player/comic/EPUB consumption is present; later supplied human feedback reports Beta comic reader working with different controls.
- EPUB defaults, root/player Back policy, window verbs and comic initial mode need bounded decisions or previews. Branding and unsigned-release tooling are engineering/polish opportunities, not evidence Beta is the wrong product foundation.

Of the 1,006 nonidentical committed paths, **588 have an Aâ€“E comparison classification, 411 are intentionally excluded/development-only (F/G), and 7 remain behaviorally unresolved (H)**. All 1,006 have accountable entries; none are silently unassigned. The F/G group includes the 240 archived cloud-handoff paths. By inspection depth, these same 1,006 paths comprise 241 focused behavior-diff reviews, 525 structural/availability triage entries, and 240 explicit archive exclusions. Structural triage establishes scope/owner/availability, not complete algorithm correctness.

Across all layers, there are 15 H ledger rows: 7 committed paths, 7 installed overlays whose runtime/composition acceptance remains unresolved, and 1 Alpha working change. The 54 mechanism/identity records contain 9 H mechanisms; file rows and mechanism records use different denominators. Known overlay bytes/hashes are verified even where their runtime consequences remain unverified. No percentage of usable features is inferred from these counts.

## Exact source and artifact boundary

The Alpha audit used `9e896bfd5dac40ef3ceb0e26e166e85ed8beecd9`. The Beta checkout was clean at `501b23edba022a82666bd25e64f4f02ad8bb9a9f`. Alpha retains 7 entries; each raw hash/base and assessment is separate in the working-layer packet. The committed or dirty Alpha tree is not assumed to equal its shipped APK. Final source state: `final-source-state.json`.
The 44-file touch integration is exact at Beta HEAD: 27 product + 17 test files. The APK matches 26 runtime product QML/JS assets; Java source-to-DEX linkage is supplied packaging provenance. Both retained APK hashes independently match the brief; 88 native libraries are byte-identical. Candidate SHA256 75081ba22b231552ad38bf3c66a0e08cab45a5cd2abdd786b010678f23af636e; prior visual APK 3fe5f9c92c6cb4c4bbd452ba79487dbeb91b1a75b39e0c53092eb21e758d0770. All 8 overlay hashes match receipt, each base 486fb36c9407320e55c2a1d9b41d756b9cfb42a5. Details: `package-verification.json`.
Seven QML overlays are extracted from the APK; preserved Activity source matches its receipt. Sources margins do not cure its fixed vertical budget. HouseScrollBar overlay hides Android track/thumb affordance; Flickable input/keyboard owners are unchanged. Whole-Qt startup scaling is not evidence of misaligned Qt taps. Conditional geometry and actual active Loader chain are detailed in `worker-platform/GEOMETRY.md`.
Package receipt itself says NOT_PERFORMED for install/device. Later supplied CURRENT.json records installed hash/normal update/OnePlus 11R Android 16/cold zoom 1.20 and withdraws an earlier player-test confirmation. Later corrected dialogue reports comic reader works/consumption present. No current device inquiry or full acceptance matrix occurred. AlphaDesk lacked proposed 20 percent enlargement, so it was not controlled zoom parity. Nokia T20 earlier ANR recurrence remains separately unqualified.

## Coverage and evidence quality

Tracked union denominator 6,141: 5,135 same blobs,429 different shared,365 Alpha-only,212 Beta-only. Original eight lists have 13+80+21+42+25+173+164+36=554 entries and 554 unique paths; the supplied 654/554 overlap claim is incorrect for these lists. They omit all 212 Beta-only plus 240 cloud-handoff Alpha-only. Originals were preserved. A Git-escaped fixture path is decoded to `tests/fixtures/vault/accent-root/Poke Series/PokeÂ´mon 01.cbz`; same-basename move hints were not accepted as rename proof.
Current ledger: 6,156 path/layer rows; 1,006/1,006 nonidentical committed paths accountable. Depth counts: {"identical-blob": 5135, "structural-triage": 525, "behavior-diff": 256, "excluded": 240}. Layers: {"committed-alpha-beta": 6141, "installed-overlay": 8, "alpha-working": 7}.
Identical blobs establish per-file byte identity only. Changed/unique active mechanisms were traced across staging/Loader/caller boundaries. Desktop-only Vinyl/AniList deeper internals received owner/availability structural triage; DLNA is explicitly deferred. Archived cloud task artifacts and restricted operating material are excluded. No source filename/scan count was turned into a feature-equivalence percentage.
The old 46 versus 0 touch-target scan is not a usable-control denominator. Reproducible current filesystem scans with `targetSize\(|touchTargetSize` find 66 Alpha QML files and 0 Beta; definitions/inactive branches are included. `targetSize(` alone finds 42 Alpha; touchTargetSize 38; broader wrappers 80. Counts depend on pattern and layer. Actual active controls and sizing owners support PF-01; counts do not prove identical causes everywhere.
Supplied 15 host QML suites: 118 behavior/data cases + 30 fixture rows pass. Additional EastBlue direct MouseArea disable/re-enable probe: 4 passing rows + 1 failure; normal retained hide/show variants pass. Both are host evidence, neither a full Android qualification. No new builds/tests/device/desktop operations, installs, commits, pushes or deployments were performed in this audit. Event memory targeted query returned 0 matches; no memory writes.

## Priorities and preservation

Use `ROADMAP.md` for coherent units and `gap-register.csv` for every owner/dependency/test/recovery field. Keep Beta asynchronous Media3 startup; active demand epochs/download isolation; canonical account/library/progress/Collections; reader origin/CSP/script/generation/font guards; native overlay logical IME/focus mapping; rendered-page/strip confirmation; Manga/Manhwa modes and resume; secure credential commit/rollback. Never overwrite whole native/reader/store subsystems with donor files.

## Unresolved and intentionally unexamined

Source-only risks: resize/virtualization/error-HUD overlap(PF-12), heuristic automatic film filtering(PF-13), catalogue generation boundary(WT-13), comic default preference(BR-COMIC-DEFAULT), and actual native viewport/IME effects. Root Back, player minimize/close and window verbs need desired-contract decisions; they are not counted as confirmed regressions. Future Android Vault, read-along, DLNA and tablet performance work are not implemented or newly designed here.
No nonidentical tracked path is silently unassigned. Structural triage/exclusions and H uncertainty remain visible in the ledger; none certify deferred desktop algorithms.

## Complete mechanism register

### PF-01 â€” Shared control touch sizing (C)

Small actionable areas remain despite a visually larger canvas. A glyph size and an input target size are different measures.

Action: Add presentation-independent input metrics, then adapt shared TopBar/actions/download row hosts and the active account/world callers as one tested control-policy unit. Size host AND pointer owner; avoid overlapping adjacent hit regions.
Active owner: Main/WorldPage TopBar shared roots are staged instead of +android; DownloadsPage actions are active from Main.openDownloadsPage
Alpha: `9e896bfd5dac40ef3ceb0e26e166e85ed8beecd9:qml/AdaptiveLayout.qml:32-60; qml/TouchMetrics.js:3; qml/TouchTarget.qml:4-20; qml/+android/TopBar.qml:193-210; qml/DownloadsPage.qml:669-812`. Beta: `501b23edba022a82666bd25e64f4f02ad8bb9a9f:qml/AdaptiveLayout.qml:7-33; qml/TopBar.qml:145-175; qml/KeyboardAction.qml:8-100; native/cmake/android.cmake:98-120; qml/DownloadsPage.qml:813-880`.
Evidence: High source confidence; no fresh Android runtime verification. Open: None for source difference; physical acceptance remains open

### PF-02 â€” Sources geometry and action reachability (C)

With rows and one quality-pill row the clipped result ListView budget is Hâˆ’544; wrapping adds 52 per extra row. Zoom can leave almost no result viewport. This is a layout budget issue, not proof of a blocked main thread.

Action: Constrain header/banner by actual available height or include header in a single deliberate scroll owner; retain provider filters/actions and explicit source pin. Apply analogous constrained budget to ComicTorrentSourcesPage and MangaTankobanSourcesPage only after their own geometry checks.
Active owner: Main.qml:5270-5276 fullscreen Theatre title Loader -> chosen legacy/rebuilt title -> direct SourcesSheet child; SourcesSheet fills page; not WorldPage Flickable
Alpha: `9e896bfd5dac40ef3ceb0e26e166e85ed8beecd9:qml/SourcesSheet.qml:22-27,362-366,436-440; qml/ComicTorrentSourcesPage.qml:244-267`. Beta: `501b23edba022a82666bd25e64f4f02ad8bb9a9f:qml/SourcesSheet.qml:19-27,356-359,429-433,486-506,542-552,635-652; qml/TheatreSeries.qml:2437; qml/TheatreSeriesView.qml:2248; native/platform/android/src/org/colosseum/platform/DesktopPresentationScale.java:7-13`.
Evidence: High source confidence; no fresh Android runtime verification. Open: 640/z is conditional calculation, not measured current viewport; see GEOMETRY.md. Alpha source short branch does not prove shipped Alpha geometry.

### PF-03 â€” Nested transient Back dispatch (B)

System Back can leave the title instead of closing its visible picker/menu first. Physical key Escape can differ because focused handlers see key events.

Action: Restore transient delegation in both Theatre title variants and Sources; include Manga detail owner only with its worker-reviewed order. Keep IME-first and Collections draft guard.
Active owner: Native PlatformRuntime.backRequested -> shell escape command -> handleEscape theatreSeries action; native callback does not synthesize QML key dispatch
Alpha: `9e896bfd5dac40ef3ceb0e26e166e85ed8beecd9:qml/Main.qml:1069-1080; qml/TheatreSeries.qml:517-524; qml/TheatreSeriesView.qml:530-537; qml/SourcesSheet.qml:265-274`. Beta: `501b23edba022a82666bd25e64f4f02ad8bb9a9f:qml/Main.qml:1336-1383; qml/TheatreSeries.qml:505; qml/TheatreSeriesView.qml:518; qml/SourcesSheet.qml:1099-1107`.
Evidence: High source confidence; no fresh Android runtime verification. Open: None for source difference; physical acceptance remains open

### PF-04 â€” Deleted local-video Continue recovery (B)

Deleted downloads strand Continue at a playback error instead of allowing a deliberate replacement source while preserving the episode and saved position.

Action: One coherent unit: availability check + exact title/episode source route + async detail-load continuation + resumePosition forwarding + explicit-resume guard. Retain newer player local-error retry and generation/identity gates.
Active owner: Home/ContinueRow -> Main.resumeContinue -> openLocalVideoSession; missing-file fallback needs Download.localFileAvailable from native DownloadStore and current detail owner
Alpha: `9e896bfd5dac40ef3ceb0e26e166e85ed8beecd9:qml/Main.qml:3123-3155,3180; qml/TheatreSeries.qml:465-477; qml/TheatreSeriesView.qml:478-490; qml/PlayerPage.qml:273,1600,2973-2979`. Beta: `501b23edba022a82666bd25e64f4f02ad8bb9a9f:qml/Main.qml:3412-3428; qml/PlayerPage.qml:2997-3019; qml/TheatreSeries.qml:466; qml/TheatreSeriesView.qml:479`.
Evidence: High source confidence; no fresh Android runtime verification. Open: None for source difference; physical acceptance remains open

### PF-05 â€” Player speed/sleep/skip popup reachability (C)

Ten sleep presets give columnsHeight426, panel height500 before active timer extra38. At short chrome height lower/upper actions can escape viewport; physical effect requires rectangle check.

Action: One popup unit: bounds, height-aware scroll, focus reveal, resize relocation and cancellation; inspect other audio/fill/watchparty popups as same-route checklist, not blanket claimed defects.
Active owner: Player desktop SpeedMenu trigger sm -> speedPopup; Android PlayerPage is active Media3 surface via Main.usePlayer2 false
Alpha: `9e896bfd5dac40ef3ceb0e26e166e85ed8beecd9:qml/PlayerPage.qml:7031-7088`. Beta: `501b23edba022a82666bd25e64f4f02ad8bb9a9f:qml/PlayerPage.qml:7127-7164; qml/PlayerPage.qml:1122-1135`.
Evidence: High source confidence; no fresh Android runtime verification. Open: None for source difference; physical acceptance remains open

### PF-06 â€” Provider metadata failure recovery (C)

An exact movie already listed can become inaccessible when the metadata provider misses; source picker/play entry gated by failed details.

Action: Keep optional caller snapshot projection, require canonical expected ID and movie type; use only after live metadata misses. Update both title variants together.
Active owner: Theatre catalogue row -> Main.openTheatreSeries(itemData) -> selected title.load -> TheatreApi.loadMeta
Alpha: `9e896bfd5dac40ef3ceb0e26e166e85ed8beecd9:qml/TheatreApi.js:257-282,307-319; qml/TheatreSeries.qml:848-910; qml/TheatreSeriesView.qml:861-923`. Beta: `501b23edba022a82666bd25e64f4f02ad8bb9a9f:qml/TheatreApi.js:257-300; qml/TheatreSeries.qml:833-908; qml/TheatreSeriesView.qml:846-921`.
Evidence: High source confidence; no fresh Android runtime verification. Open: None for source difference; physical acceptance remains open

### PF-07 â€” Track names and MIME labels (C)

Several tracks can show indistinguishable Subtitle names or internal MIME strings; harder to choose intended language.

Action: One label helper unit shared by active track lists; keep actual track IDs/language/delay untouched.
Active owner: Media3 track dictionaries -> PlayerPage subtitleRow/track labels -> SubtitleMenu audio/subtitle controls
Alpha: `9e896bfd5dac40ef3ceb0e26e166e85ed8beecd9:qml/PlayerPage.qml:644-670,715-737; qml/SubtitleMenu.qml:92-111`. Beta: `501b23edba022a82666bd25e64f4f02ad8bb9a9f:qml/PlayerPage.qml:650-666,711-718; qml/SubtitleMenu.qml:90-103`.
Evidence: High source confidence; no fresh Android runtime verification. Open: None for source difference; physical acceptance remains open

### PF-08 â€” Background-download failure explanation (C)

Jobs can pause safely because foreground service not ready while page omits why; user sees incomplete work without actionable reason.

Action: One read-model/error-display unit with empty/error/recovered states; preserve current delete-all state machine.
Active owner: Main.openDownloadsPage -> DownloadsPage; PlatformDownloads accepts epoch-gated DownloadService host state
Alpha: `9e896bfd5dac40ef3ceb0e26e166e85ed8beecd9:qml/DownloadsPage.qml:46-50,505-519`. Beta: `501b23edba022a82666bd25e64f4f02ad8bb9a9f:qml/DownloadsPage.qml:46-49,658-666; native/platform/PlatformDownloads.h:18; native/platform/PlatformDownloads.cpp:78-85; native/main.cpp:1627-1659`.
Evidence: High source confidence; no fresh Android runtime verification. Open: None for source difference; physical acceptance remains open

### PF-09 â€” Android branding resources (nonfunctional polish) (A)

Alpha adaptive launcher artwork is absent from Beta source configuration; rendered installed launcher appearance unverified. This is branding polish, not a playback/navigation regression or a reason to change foundation.

Action: One resource+manifest unit; keep world.getcolosseum.app.beta side-by-side identity and release signer.
Active owner: Android package resource copy -> manifest generation -> launcher (not QML)
Alpha: `9e896bfd5dac40ef3ceb0e26e166e85ed8beecd9:native/platform/android/AndroidManifest.xml:22-23; native/platform/android/res/drawable/ic_launcher_foreground.xml; native/platform/android/res/mipmap-anydpi-v26/ic_launcher.xml; native/platform/android/res/mipmap-anydpi-v26/ic_launcher_round.xml; native/platform/android/res/values/ic_launcher_background.xml`. Beta: `501b23edba022a82666bd25e64f4f02ad8bb9a9f:native/platform/android/AndroidManifest.xml:22; native/cmake/android.cmake:72-84,191-211`.
Evidence: High source confidence; no fresh Android runtime verification. Open: Installed resource table/rendered launcher not inspected; do not claim actual Qt icon appearance.

### PF-10 â€” Android package/version input validation (C)

Malformed values fail later; an accidentally wrong id/channel can create a separate app or incompatible update. No current package failure shown.

Action: Single packaging-contract unit accepting current Beta lane plus explicit release/diagnostic policy; validate numeric bounds and distinct IDs.
Active owner: Android CMake cache -> Qt androiddeployqt manifest/Gradle package
Alpha: `9e896bfd5dac40ef3ceb0e26e166e85ed8beecd9:native/cmake/android.cmake:54-112,241-276`. Beta: `501b23edba022a82666bd25e64f4f02ad8bb9a9f:native/cmake/android.cmake:191-211`.
Evidence: High source confidence; no fresh Android runtime verification. Open: None for source difference; physical acceptance remains open

### PF-11 â€” Android shell root and predictive Back (H)

Root Back behavior and predictive feedback differ; minimizing playback retains warm state rather than explicit close. These are product/lifecycle choices, not automatically regressions.

Action: Decision item: choose desired Android root Back and player minimize/close behavior before implementation. Predictive progress feedback may be adapted independently only after that contract is recorded.
Active owner: PlatformRuntime native single Back owner -> Main shell; root policy has changed independently of phone composition
Alpha: `9e896bfd5dac40ef3ceb0e26e166e85ed8beecd9:qml/ShellBackPolicy.js:78; qml/PhoneShellHost.qml:167-190; qml/Main.qml:961-973`. Beta: `501b23edba022a82666bd25e64f4f02ad8bb9a9f:qml/ShellBackPolicy.js:73-79; qml/PhoneShellHost.qml:155-176; qml/Main.qml:1279-1284`.
Evidence: Source difference verified; no approved behavior contract establishing regression.. Open: User decision needed for root background-versus-HomeTop and player minimize-versus-close; source shows difference but no accepted device failure.

### PF-12 â€” Resize anchor and virtualized episode geometry (H)

Possible viewport jump or stale realized episode window after geometry changes. Source change does not establish actual failure.

Action: One geometry/virtualization unit if supported; do not add competing scroll owner or phone header.
Active owner: WorldPage retained worlds single Flickable and title episode virtualization window calculations
Alpha: `9e896bfd5dac40ef3ceb0e26e166e85ed8beecd9:qml/WorldPage.qml:315-356; qml/TheatreSeries.qml:70-77; qml/TheatreSeriesView.qml:83-91`. Beta: `501b23edba022a82666bd25e64f4f02ad8bb9a9f:qml/WorldPage.qml:278-311; qml/TheatreSeries.qml:71-79; qml/TheatreSeriesView.qml:84-92`.
Evidence: Source risk only; behavioral failure unresolved. Open: No host/runtime reproduction was authorized or performed; binding notification and actual resize effect need experiment.

### PF-13 â€” Automatic fallback film identity heuristic (H)

Automatic retry may choose wrong-film provider rows; Alpha heuristic can also reject legitimate alternate names/release labels.

Action: Separate automatic-choice policy unit with explicit identity evidence; retain manual choice and source ordering.
Active owner: Stream failure/watchdog -> nextPlayableStreamIndex -> playStreamAt
Alpha: `9e896bfd5dac40ef3ceb0e26e166e85ed8beecd9:qml/PlayerPage.qml:1296-1338`. Beta: `501b23edba022a82666bd25e64f4f02ad8bb9a9f:qml/PlayerPage.qml:1289-1307`.
Evidence: Source difference confirmed; real provider mismatch and heuristic quality unresolved. Open: Need representative provider release evidence; no live-provider request in this audit.

### PF-14 â€” Comic pack inspection cancellation (C)

User can back out but loses picker context; in-place cancel permits retrying another result without repeating search. Not an uninterruptible download claim.

Action: One picker inspection unit; retain typed outcomes, stale issue guards and safe ledger handoff.
Active owner: ComicSeriesPage alternate source route -> ComicTorrentSourcesPage.beginAutomaticDownload -> inspecting
Alpha: `9e896bfd5dac40ef3ceb0e26e166e85ed8beecd9:qml/ComicTorrentSourcesPage.qml:94-99,633-667`. Beta: `501b23edba022a82666bd25e64f4f02ad8bb9a9f:qml/ComicTorrentSourcesPage.qml:80-94,562-585`.
Evidence: High source confidence; no fresh Android runtime verification. Open: None for source difference; physical acceptance remains open

### PF-15 â€” Android window verbs in shared desktop chrome (H)

Window-only actions occupy short Android chrome and may invoke desktop-oriented actions. Removing them must rebalance actual layout rather than leave gaps.

Action: Evaluate actual Android minimize/quit effects against approved desktop composition; hide/rebalance only demonstrably irrelevant window verbs under an explicit desired chrome contract. Preserve real media fullscreen/orientation actions.
Active owner: Main/WorldPage TopBar; full-screen genre/detail inline control rows
Alpha: `9e896bfd5dac40ef3ceb0e26e166e85ed8beecd9:qml/+android/TopBar.qml; qml/TheatreGenreIndex.qml:297-310; qml/TheatreGenrePage.qml:449-466`. Beta: `501b23edba022a82666bd25e64f4f02ad8bb9a9f:qml/TopBar.qml:554-564; qml/HouseWindowControls.qml:12; qml/TheatreGenreIndex.qml:251-264; qml/TheatreGenrePage.qml:429-446; native/cmake/android.cmake:98-120`.
Evidence: Source-visible controls verified; semantic uselessness/desired composition not established for each action.. Open: None for source difference; physical acceptance remains open

### PF-16 â€” Touch integration retained and improved (E)

Completed input work must not be counted again as missing. Fifteen supplied host suites passed; East Blue direct-disable adversarial probe remains failed outside those suites.

Action: No implementation recommendation; retain exact post-touch baseline and qualify real device routes.
Active owner: Main -> Android PlayerPage and native Media3; staged Android reader adapter; see package-verification.json for44 hashes/26 runtime assets
Alpha: `9e896bfd5dac40ef3ceb0e26e166e85ed8beecd9:qml/PlayerTouchSurface.qml:28-90; qml/PlayerPage.qml:4483-4535`. Beta: `501b23edba022a82666bd25e64f4f02ad8bb9a9f:qml/PlayerTouchSurface.qml:28-110; qml/PlayerPage.qml:3141-3153,5010-5200; qml/SubtitleMenu.qml:141-203`.
Evidence: High source confidence; no fresh Android runtime verification. Open: None for source difference; physical acceptance remains open

### PF-17 â€” Platform build/lifecycle equivalence and intentional composition exclusions (D)

Different files do not imply absent playback/readers/downloads; committed phone composition is deliberately excluded. Orientation landscape is current Beta choice, not proven device incapability.

Action: No platform subsystem replacement; donor backup streamserver.pre012.bak and cloud handoff artifacts are development-only.
Active owner: Manifest QtActivity and DownloadService -> existing JNI/native owners; Android CMake stages selected shared desktop chrome/native EPUB, excludes Vault backend/Feria/Vinyl
Alpha: `9e896bfd5dac40ef3ceb0e26e166e85ed8beecd9:native/platform/android/src/org/colosseum/platform/ColosseumActivity.java:1-104; native/platform/android/build.gradle; native/platform/android/src/org/colosseum/reader/AndroidEbookHost.java:97-105`. Beta: `501b23edba022a82666bd25e64f4f02ad8bb9a9f:native/platform/android/src/org/colosseum/platform/ColosseumActivity.java:1-145; native/platform/android/build.gradle; native/platform/android/src/org/colosseum/reader/AndroidEbookHost.java:97-104; native/platform/android/AndroidManifest.xml:29-38`.
Evidence: High source confidence; no fresh Android runtime verification. Open: None for source difference; physical acceptance remains open

### BR-SOURCE-REPLACEMENT â€” Changing the source of a retained same-content session can resume its old stream (B)

A user minimizes an episode, picks another source for the same episode, and receives the old retained source despite the selected descriptor now being saved. Already-active same-id open may not dispatch at all. Source-supported deterministic path; no device reproduction in this audit.

Action: Adapt Alpha source-change invalidation into Beta, limiting reload to an actual transport/source change. Keep same episode savedState and metadata-only reuse. Do not replace SessionStore or Main wholesale.
Active owner: SourcesSheet.activateSource:90-95 -> TheatreSeriesView.onPlayRequested:2253 -> Main detail Loader.playRequested.connect(openMovieSession):5284 -> openMovieSession:3557 -> Sessions.openOrSwitch. Session activeChanged handler5534 -> activateSession -> activateMovieSession. Player resume has no descriptor argument/source reconciliation.
Alpha: `native/SessionStore.h:80-88 sourceKeyFor/openOrSwitch; qml/Main.qml:2221-2226 onSourceReplaced; tests/session_store_harness.cpp:150-164`. Beta: `native/SessionStore.h:77-86 openOrSwitch; :690-709 setActive; qml/Main.qml:3813-3848 activateMovieSession; qml/PlayerPage.qml:3102-3119 resumeFromMinimize`.
Evidence: high source-path confidence; Android runtime unverified. Open: No fresh physical reproduction. Source identity adaptation should include local/direct routes without treating expired URL re-resolution as a different content identity.

### BR-EPUB-DEFAULTS â€” Phone-specific EPUB defaults were removed; evaluate narrow-viewport defaults without migrating saved choices (C)

Different first-run/unspecified typography reduces available vertical room and changes paragraph spacing/justification on narrow effective viewports. This is a source-defined default difference, not proof of unreadable text or exact horizontal width loss.

Action: Treat as a product-preference adaptation: consider narrower Android EPUB viewport defaults from Alpha after preview, keeping existing explicitly saved defaults/per-book settings. Do not force phone layout or rewrite user settings.
Active owner: Main5445 ReaderShell->ReaderPaper49 AndroidPaper->AndroidEbookRenderer::setAppearance162->AndroidEbookHost.command200->android_boot.js34-43->paper_glue paperSetAppearance.
Alpha: `qml/reader2/Reader2Logic.js:496-516 defaultMarginPx/defaultJustify/appearanceDefaults; :645 appearanceToPaper`. Beta: `qml/reader2/Reader2Logic.js:498-504 appearanceDefaults; :633-634 appearanceToPaper; :669-697 appearanceStore/effectiveAppearance; resources/reader2/paper_glue.js:452-457`.
Evidence: high source-delta confidence; device behavior unverified. Open: Whether Hemanth prefers Alpha typography defaults at Beta effective viewport needs a concrete preview. No measured reader viewport or rendered page image gathered here.

### BR-READER-TARGETS â€” Reader actions remain present but lose Alpha larger targets and width safety (C)

Actions are reachable in source and existing host tests exercise them, but small deletion/highlight/save areas reduce finger tolerance. Note card may exceed very narrow available width; that is conditional geometry risk, not proven clipping at current OnePlus density.

Action: Adapt input targets and width containment using the shared touch-target policy, preserving desktop presentation. Extract Alpha reserved non-overlapping delete hit region and viewport cap rather than copy its phone two-row composition.
Active owner: ReaderShell SelectionMenu/ReaderChrome LeftPanel remain actual Android reader chrome. ReaderOverlay reparents popover surface to native overlay window; AndroidPaper WebView covered when full-screen panel open.
Alpha: `qml/reader2/SelectionMenu.qml:38-49,115-121,175-181,301-320; LeftPanel.qml:133-138,428-477`. Beta: `qml/reader2/SelectionMenu.qml:38-48,115-118,167-173,284-300; LeftPanel.qml:423-447; ReaderOverlay.qml:26; SearchSheet.qml:54-65`.
Evidence: high source-delta confidence; device behavior unverified. Open: Exact installed native-window dimensions/target overlap have not been device measured. Enlarging target must keep one gesture owner and avoid stealing list scroll.

### BR-COMIC-CONTINUITY â€” Beta guards saved progress until actual pixels and restoration settle (E)

Prevents a blank/failed open or premature close from replacing last successful read position; resumes same numbered chapter across source translation without rewriting source before presentation.

Action: Preserve Beta; no Alpha shell or ProgressStore transplant.
Active owner: TankobanLibrary/Manga/ComicSeries embed ComicReaderShell, native core/provider added main919; reader shell writes canonical ProgressStore.
Alpha: `qml/comicreader/ComicReaderShell.qml:1028-1051,1072-1090; ComicReaderStripSurface.qml:668-671; native/ProgressStore.h:885`. Beta: `qml/comicreader/ComicReaderShell.qml:1060-1093,1114-1130,1653-1677; ComicReaderStripSurface.qml:670-681,902-938; native/ProgressStore.h:921-934,2095-2120`.
Evidence: high source-delta confidence; device behavior unverified. Open: No additional source question recorded; runtime acceptance is separate.

### BR-COMIC-DEFAULT â€” Android lane default differs; working reader observation does not establish a layout defect (H)

User sees different starting page layout/controls. Corrected supplied observation says Beta reader works; this is not evidence of failed gestures or overflow.

Action: Device-check current default with fresh preferences before changing. If needed, adapt lane default per actual reader viewport only; preserve paired mode and existing chosen settings.
Active owner: ComicReaderShell load preferences->layout; actual ComicReaderInput/native core identical; Main/Tankoban readers active.
Alpha: `qml/comicreader/ComicReaderShell.qml:1485-1493`. Beta: `qml/comicreader/ComicReaderShell.qml:1523-1529; ComicReaderState.js:migrateReaderPrefs`.
Evidence: high source-delta confidence; device behavior unverified. Open: No fresh device capture of page fit/current reader preference; no recommendation to forcibly disable paired mode.

### BR-READER-OVERLAY â€” Beta keeps Android reader popovers operable across native-window boundaries (E)

Prevents lost Escape/Tab/focus and excessive keyboard avoidance when device density is greater than 1.

Action: Preserve these Beta improvements while adapting Alpha hit areas; no older file replacement.
Active owner: ReaderOverlay moves actual visual surface to native overlay window over Android WebView.
Alpha: `qml/reader2/ReaderOverlay.qml:23; SelectionMenu.qml:38-48; DictCard.qml:39-43; FootnoteCard.qml:35`. Beta: `qml/reader2/ReaderOverlay.qml:26; SelectionMenu.qml:38-48,66-75; SearchSheet.qml:54-65; DictCard.qml:30,41-43; FootnoteCard.qml:27,36`.
Evidence: high source-delta confidence; device behavior unverified. Open: No additional source question recorded; runtime acceptance is separate.

### BR-READER-CANCEL â€” Beta cancels partial page drags and delayed image actions (E)

Prevents cancelled drags leaving partial page position and a pinch/cancel causing delayed image popover.

Action: Preserve delivered touch mechanism; do not backport Alpha vendor files.
Active owner: Android WebView loads shared Foliate through paper_glue, not desktop-only WebEngine; native AndroidEbookHost transport remains.
Alpha: `resources/reader2/vendor/foliate-anx/src/paginator.js:843-1007; view.js:253-270`. Beta: `resources/reader2/vendor/foliate-anx/src/paginator.js:968-1036; view.js:253-281`.
Evidence: high source-delta confidence; device behavior unverified. Open: No additional source question recorded; runtime acceptance is separate.

### BR-READER-RETAINED â€” Both snapshots share guarded Android WebView reader; no whole-reader feature gap established (D)

Native Android EPUB publication script-disabled content, notes/bookmarks/search/progress routes retained; read-along explicitly unsupported by Android host in both versions, not a Beta regression.

Action: Preserve security and generation/font guards regardless of provenance. Do not claim every Android book feature accepted from opening a book.
Active owner: Main->ReaderShell->ReaderPaper Android selector->AndroidPaper->JNI foreign WebView. Readium-labelled notes do not describe this active source engine.
Alpha: `native/platform/android/src/org/colosseum/reader/AndroidEbookHost.java:95-148; qml/reader2/ReaderPaper.qml:49; resources/reader2/android_boot.js:1-43`. Beta: `same paths/functions/line ranges; native/reader2/AndroidEbookRenderer.cpp:120-163; main.cpp:905-906; native/cmake/android.cmake:15-16`.
Evidence: high source-delta confidence; device behavior unverified. Open: No comprehensive device reader feature acceptance; exact installed source provenance delegated platform overlay/build packet.

### BR-DOWNLOAD-LIFECYCLE â€” Beta preserves Alpha Android pinned torrent recovery and adds safe incomplete deletion (E)

Preserves paused bytes only for exact immutable torrent/file with valid206 offset, pauses safely without foreground/service lease, retains failed deletion evidence rather than losing job.

Action: Keep Beta; Alpha missing-file Continue recovery/UI transfer is separately PF-04(platform), not a reason to replace lifecycle owner.
Active owner: Main1607 DownloadStore default->AndroidPinnedTorrent; BackgroundDownloadBridge1621 service lease->main1627 foreground coordinator->Stream/Download execution gates; DownloadsPage mutations route owning backend.
Alpha: `native/player/downloadstore.cpp:63,264-373,485-569; native/engine/MangaDownloader.cpp:385`. Beta: `native/player/downloadstore.cpp:66-76,181-197,987-1080; native/engine/MangaDownloader.cpp:388-403; BookDownloader.cpp:349; ComicDownloader.cpp:1177; AudiobookDownloader.cpp:682`.
Evidence: high source-delta confidence; device behavior unverified. Open: Actual Android foreground-service/provider/network behavior requires device checks; no new run.

### BR-NUVIO-EQUIVALENT â€” Alpha-only Nuvio diagnostic plumbing is not an absent production stream capability (D)

No missing production capability follows from removed diagnostic files. Engine packaged availability is build/receipt-dependent; no new engine/device test in audit.

Action: Keep diagnostic isolation in tests; use bounded read-only host contracts for future changes, never restore production task trigger watcher.
Active owner: android.cmake38 opt-in artifact stamp gate->StreamServer Android facade->Nuvio worker; native trust bundle export failure closes backend; stream/dl demands remain separate.
Alpha: `native/player/NuvioFixtureAdmission.h:1-75; streamserver_android.cpp:77-291; nuviostreambackend.cpp:760+ route ownership`. Beta: `tests/support/NuvioFixtureAdmission.h:1-75 normalized-identical; native/cmake/NuvioBackendTests.cmake:27-41; nuviostreambackend.cpp:760-825; native/player/streamserver_android.cpp:74-198`.
Evidence: high source-delta confidence; device behavior unverified. Open: No additional source question recorded; runtime acceptance is separate.

### BR-BETA-STATE â€” Beta adds profile-safe Collections and Manga language/resume state (E)

Protects newer persistent user collections, language selections and read progress from broad Alpha file replacement or destructive rollback.

Action: Preserve Beta state ownership/schema/adoption and collection identity; Alpha dirty Manga library is separate hash-labelled worlds packet.
Active owner: Common Android native graph CMake387, native main1973 CollectionsIdentity/ProfileScope; profile activation owns runtime; QML adapters use exact identity/reopen owner.
Alpha: `native/account/ProfileStoreRuntime.cpp:506-519; native/engine/ColosseumTitleIdentityRegistry.cpp:70; ProgressStore.h:885`. Beta: `native/NamedCollectionsStore.cpp:515-630; account/ProfileStoreRuntime.cpp:519,571,653,985; ProfileAdoption.cpp:679-704; ColosseumTitleIdentityRegistry.cpp:73-347; ProfilePreferencesStore.cpp:673-731`.
Evidence: high source-delta confidence; device behavior unverified. Open: Account/provider/live device behavior not newly run; Alpha dirty code not proven to be shipped APK.

### BR-STREMIO-COMMIT â€” Beta confirms durable secure linking before reporting sign-in success (E)

Failed/expired/disk-full/reentrant-profile sign-in can report a meaningful failure and preserve last valid credential; no false browser success.

Action: Preserve newer transactional and lifetime guards; no wholesale credential/auth backport.
Active owner: AccountRuntime623 createAccountCredentialStore; factory51-56 actual Android default PlatformAccountCredentialStore(Android secret backend), not legacy test injection class.
Alpha: `native/stremio/StremioSync.cpp:1959-1985,2030-2098; PlatformAccountCredentialStore.cpp:256-262; AccountRuntime.cpp:695`. Beta: `native/stremio/StremioSync.cpp:1984-2014,2026,2072-2143; PlatformAccountCredentialStore.cpp:259-277; AccountRuntime.cpp:695-712; ProfilePreferencesStore.cpp:1072-1086`.
Evidence: high source-delta confidence; device behavior unverified. Open: No additional source question recorded; runtime acceptance is separate.

### BR-TRACKER-AVAILABILITY â€” Compiled AniList support does not establish Android tracker sign-in availability (D)

Do not count Beta-only AniList files as working Android sync, nor claim an Alpha donor Android tracker feature absent Beta. Shared platform limit needs separate explicit scoped product work if requested.

Action: Preserve Beta provider/canonical outbox additions; document readiness boundary, no Alpha backport.
Active owner: ProfileStoreRuntime constructs router, SyncCenter connection availability first checks Windows vault; controllers require available vault.
Alpha: `native/trackers/TrackerCredentialVault.cpp:isAvailable; account/ProfileStoreRuntime.cpp:846-866`. Beta: `native/trackers/TrackerCredentialVault.cpp:244-250; account/ProfileStoreRuntime.cpp:911-931; AniListConnectionController.cpp:127-134`.
Evidence: high source-delta confidence; device behavior unverified. Open: No external tracker authentication/provider testing performed; account/Stremio credential path is separate and does have Android backend.

### BR-ACTIVE-ROUTES â€” Native graph keeps readers/platform backend active while desktop organs remain excluded (E)

Avoids mistaking desktop code presence for an Android capability or labelling retained reader/platform machinery missing. A broad Alpha import could activate unsupported desktop dependencies or remove newer common state.

Action: Keep Android reader/JNI and production backend route; native Back/fontScale retained, QML consumption gaps belong PF/worlds findings. Source graph alone does not prove packaged optional engine ABI.
Active owner: native/CMakeLists includes android.cmake -> Android renderer/Media3/streamserver_android target sources; main Reader2Bridge/installAndroidEngineBindings and ComicReaderCore registrations -> Main ReaderShell -> ReaderPaper -> AndroidPaper. PlatformRuntime remains the native Back/font-scale owner. Desktop exclusions are compile/staging gates, not missing user routes.
Alpha: `native/cmake/android.cmake:4-57 target_sources and optional Nuvio branch; qml/reader2/ReaderPaper.qml:49 Android selector (matching committed blob); native/platform/PlatformRuntime.cpp JNI nativeFontScale/nativeBackGesture (matching blob). N/A for Alpha NamedCollections/AniList additions: absent from its tracked tree.`. Beta: `native/CMakeLists.txt:867-1000,1204-3359; native/cmake/android.cmake:4-57,89-99; native/main.cpp:903-920,1865-1875`.
Evidence: high source/graph confidence; deeper excluded algorithms unexamined. Open: Source graph does not establish installed optional engine ABI/version or physical Android acceptance; build/artifact provenance belongs platform receipt.

### BR-CATALOGUE-PRESERVE â€” Beta adds validated Manhwa filtering and richer manga shelves (E)

Preserves newer Tankoban shelf/Manhwa discovery and recovery when a cached publication predates the query. Replacing with Alpha would narrow discovery and can turn valid newer filters into empty results.

Action: Keep native format filter/fixed SQL and semantic fallback for older publications; no Alpha replacement.
Active owner: Common native engine graph constructs/exposes MAL catalogue in main; Tankoban discovery calls MalCatalog filtered-page APIs, which use local catalogue or CatalogRemote/StaticCatalogueResolver remote publication/fallback route. These native owners are not in the desktop-only exclusion block.
Alpha: `native/engine/CatalogRemote.cpp:46-49; MalCatalog.cpp:732-786 discoverPageFiltered; MalCatalog.h:38-41 landing API; StaticCatalogueResolver.cpp:594-609 and :702-705 publication-page fallback. Corresponding old/new hunks retained in native__engine__*.diff.`. Beta: `native/engine/MalCatalog.cpp:732-822; MalCatalog.h:41; StaticCatalogueResolver.cpp:595-620,710-719; CatalogRemote.cpp:49`.
Evidence: high source/graph confidence; deeper excluded algorithms unexamined. Open: Provider/network availability and current catalogue publication contents were not newly queried; preservation conclusion is source-supported.

### BR-DESKTOP-PRESERVE â€” Preserve desktop Vinyl/Vault improvements without importing them into Android scope (F)

Prevents an Android transfer from regressing desktop download-derived library state or newer Vinyl behavior. This finding does not claim those desktop features work on Android or certify their algorithms.

Action: Vault newer synthetic download-root/grouping/refresh and Vinyl recording/lyrics/radio/video/home selection are outside active Android app graph. No donor gap; API/source-hunk structural triage only for Vinyl, not deep desktop algorithm certification.
Active owner: Explicitly excluded: native/CMakeLists desktop block and main !Q_OS_ANDROID guards own Vault/Vinyl registration; android.cmake filters their QML. VaultDoor/VaultHomeWidget are presentation exceptions, not backend activation. No active Android Alpha donor route established.
Alpha: `native/engine/VaultDownloadsRoot.cpp:55-70,231-245; VaultLibrary.cpp:1571-1580 setDownloadsRoot; VaultKit.cpp:372-411 episode parsing; native/main.cpp:1038-1077 cover enrichment; native/CMakeLists.txt:3174-3187 older Vinyl target; native/main.cpp:1778-1787 old Vinyl registration. New-only service files have N/A Alpha lines because absent from tracked tree.`. Beta: `native/CMakeLists.txt:867-997,1204-3359; native/main.cpp:928-1197,1662-1763,1865-1875; native/cmake/android.cmake:93-99`.
Evidence: high source/graph confidence; deeper excluded algorithms unexamined. Open: Deeper desktop Vinyl algorithms unexamined beyond API/change-hunk/owner triage; no desktop correctness certification.

### BR-DEFERRED-DLNA â€” Desktop-only optional Beta subsystem remains deferred (F)

No Alpha Android capability is missing because these Beta-only files exist. Preserve the desktop subsystem while avoiding unsupported Android activation or claims about sharing acceptance.

Action: Explicit task defer; optional pupnp server and static resources are not active Android source path. Internal algorithm/icon audit excluded; retain Beta files untouched.
Active owner: Explicitly inactive on Android: optional DLNA target and main registration are within desktop/platform and COLOSSEUM_HAVE_DLNA gates. DLNA internal audit is explicitly deferred by current task.
Alpha: `N/A: native/dlna/* are Beta-only in reconciled committed inventory; Alpha has no corresponding subsystem to cite. This is absence evidence, not invented source lines.`. Beta: `native/CMakeLists.txt:954-997; native/main.cpp:1222-1283`.
Evidence: high source/graph confidence; deeper excluded algorithms unexamined. Open: DLNA algorithms/icons and operational sharing behavior remain unexamined; structural classification only.

### BR-DIAGNOSTICS â€” Diagnostic fixtures are not production Alpha functionality (G)

Avoids exposing fixture/file-trigger control in production or miscounting diagnostic removal as lost playback. Preserves a safer test mechanism and avoids dereferencing a UI target after a click destroys it.

Action: Keep fixture admission outside production. Collections profile fixture compile+launch containment and Lanista click lifetime fix retained; no production watcher reinstatement.
Active owner: No production user route for task diagnostics: fixture APIs are test-macro gated; diagnostic file watcher removed from Android StreamServer. Collections fixture is non-Android build+launch gated. Lanista click lifetime fix belongs optional development tooling, not a missing consumption feature.
Alpha: `native/player/NuvioFixtureAdmission.h:1-75; native/player/streamserver_android.cpp:90-291 diagnostic configuration/watcher/fixture path; native/devtools/LanistaServer.cpp:1429-1443 pre-fix click reply. N/A Alpha CollectionsTestProfileController: Beta-only.`. Beta: `tests/support/NuvioFixtureAdmission.h:1-75 (normalized-identical Alpha native fixture); native/cmake/NuvioBackendTests.cmake:27-41; native/devtools/CollectionsTestProfileController.h:24-83; native/main.cpp:2426-2469; native/devtools/LanistaServer.cpp:1439-1456`.
Evidence: high source/graph confidence; deeper excluded algorithms unexamined. Open: No new diagnostic/device run performed; fixture definition/source existence is not runtime acceptance.

### BR-TESTS â€” Beta provides focused real-owner Android contracts (E)

Preserves a focused way to detect native lifecycle/source-boundary regressions without building the whole application. Avoids claiming test definitions or prior host results qualify a phone/tablet.

Action: Preserve test integration; inspect definitions only. Nuvio production contract compiles without fixture flag/header search path. Tests not run in audit; neither host test definition nor supplied prior pass qualifies device behavior.
Active owner: These are host test targets, not APK runtime components: native CMake tests-only branch calls AndroidIntegrationTests; production adapter sources are compiled by the contracts. Definitions do not establish Android JNI/ABI or device operation.
Alpha: `native/CMakeLists.txt:19-30 old COLOSSEUM_NUVIO_BACKEND_TESTS_ONLY branch and :1421-1434 separate subtitle_sidecar_parser_test. N/A native/cmake/AndroidIntegrationTests.cmake: Beta-only file.`. Beta: `native/cmake/AndroidIntegrationTests.cmake:2-35; native/CMakeLists.txt:28-32`.
Evidence: high source/graph confidence; deeper excluded algorithms unexamined. Open: No new host tests executed and no device acceptance derived from source definitions.

### BR-READER-PRESENTATION â€” Phone opacity/title/compact styling is excluded from direct backport (F)

Explains visual/control differences without declaring the working reader broken. Copying phone styling wholesale would reverse approved desktop composition; lost system-font consumption remains independently actionable through the worlds mechanism.

Action: Do not copy Alpha phone visual composition. Lost fontScale consumption is separately worlds C mechanism; short-landscape compact command default is a source difference and optional viewport usability check, not proven missing command.
Active owner: ComicReaderShell instantiates ComicReaderHud/CommandBar and reader popovers on Android; this is active presentation, not a missing native reader engine. Phone visual composition is intentionally excluded from direct transfer.
Alpha: `qml/comicreader/ComicReaderHud.qml:101-105,373-385,563-580; ComicReaderCommandBar.qml:32-36,207-210; ComicReaderImagePopover.qml:99-102 and fontSize callsite hunks; reader popover/settings diffs retained.`. Beta: `qml/comicreader/ComicReaderHud.qml:101-105,565; ComicReaderCommandBar.qml:32-36; reader popovers/settings font and opacity diffs`.
Evidence: high source/graph confidence; deeper excluded algorithms unexamined. Open: No actual native viewport measurement or fresh command clipping reproduction; styling deltas alone do not prove unavailable controls.

### WT-01 â€” Android bulk library selection is absent (A)

Bulk library cleanup/watched marking requires repeated single-item operations. Named Collections is a separate newer product function and does not replace selecting library rows.

Action: Adapt Alpha selection state/failure handling and modal Back contract without its phone grid/layout. Keep ephemeral selection outside durable stores; use existing canonical removal callbacks.
Active owner: Main world Loader -> TheatreWorld.LibraryPage / BiblioWorld.BiblioLibraryPage / TankobanWorld.TankobanLibraryTab; owner callbacks remain Collection.remove, Progress and watched owner. Alpha Main.qml:900,1025 publishes/dispatches selection Back.
Alpha: `qml/LibrarySelectionState.qml:14-50; LibrarySelectionHost.qml:25-69,121-179; CollectionGrid.qml:30-39; LibraryPage.qml:91-98,184-199; BiblioLibraryPage.qml:164,232-243; TankobanLibraryTab.qml:100-114`. Beta: `qml/LibraryPage.qml:170-185,362; BiblioLibraryPage.qml:222-231,336; TankobanLibraryTab.qml:97-106,256; Main.qml:1354 handles newer Collections separately`.
Evidence: high source / physical Android unverified. Open: No current device evidence; supplied human reports are journey-specific.

### WT-02 â€” Account forms lost explicit keyboard avoidance and focused-field reveal (C)

Lower form fields/actions can become covered when real keyboard consumes height; actual effect depends adjustResize/density/IME geometry. No device failure inferred solely from removal.

Action: Adapt reveal/extent mechanism to Beta shared frame and current ViewportAvoidance coordinate contract; do not restore whole SettingsScaffold or phone account composition.
Active owner: Main AccountOnboarding -> AccountSignIn/Create/Recovery/DeviceApproval -> shared AccountPageFrame. Sign-in/create fields remain active on Android; native OS resize may mitigate, but not prove focused field visibility.
Alpha: `qml/SettingsScaffold.qml:7-23; account/AccountPageFrame.qml:13-50,113-126; account/AccountField.qml uses touch field`. Beta: `qml/account/AccountPageFrame.qml:7-22,72-82; account/AccountField.qml:46-70; account/AccountOnboarding.qml:142; account/AccountSignIn.qml:6`.
Evidence: high source difference / conditional usability risk. Open: No current device evidence; supplied human reports are journey-specific.

### WT-03 â€” Android decoder compatibility annotations and soft ranking are not consumed (C)

Users get less guidance before selecting sources likely unsupported by their decoder; no claim hints predict every successful/failed decode.

Action: Adapt the explicit runtime argument, conservative descriptor and soft annotation/rank contract plus all actual stream callers. Keep all rows, unknown sources, provider priority within groups; no hard filter.
Active owner: Main -> TheatreSeries -> SourcesSheet.load:198-221 -> AddonClient.loadStreams. SourcesSheet Alpha passes PlatformRuntime at end of callback call; Beta omits it. player2host/ColosseumHostServices:185-189 is excluded from Android runtime graph, so only a desktop preservation/optional caller when adapting the shared JS API.
Alpha: `qml/AddonClient.js:164-370,497-520,582-588,752-772; player2host/ColosseumHostServices.qml:185-189; tests/addon_video_compatibility_test.mjs; ACTIVE Android caller SourcesSheet.qml:198-221 (PlatformRuntime supplied). ColosseumHostServices only desktop/excluded Android.`. Beta: `qml/AddonClient.js:243-268,324-331,491-513; player2host/ColosseumHostServices.qml:185-189; native/platform/PlatformRuntime.h decoder capability property retained (S2 corroboration); ACTIVE Android caller SourcesSheet.qml:198-221 omits runtime argument. ColosseumHostServices only desktop/excluded Android.`.
Evidence: high source / physical Android unverified. Open: No current device evidence; supplied human reports are journey-specific.

### WT-04 â€” Android system text-size preference no longer reaches shared UI text (C)

Increasing Android system font preference does not enlarge these custom pixel-sized labels through the retained native preference path.

Action: Restore Android-only shared typography tokens and migrate relevant active callsites with measured wrapping/height constraints. Keep desktop font sizes unchanged; do not treat native density zoom as equivalent.
Active owner: Android Activity fontScale notification -> PlatformRuntime -> missing Theme consumer -> common UI labels. Does not establish ebook content font behavior, which has separate reader settings.
Alpha: `qml/Theme.qml:9-14; account/AccountField.qml and broad S6 typography callsites; tests/phone_font_scaling_harness.cpp and tests/qml/tst_phone_text_scaling.qml`. Beta: `qml/Theme.qml:1-39; account/AccountField.qml raw font sizes; native/platform/PlatformRuntime.h:32,59; PlatformRuntime.cpp:85,374-380; Activity.java:61,151 (S2 source corroboration)`.
Evidence: high source / physical Android unverified. Open: No current device evidence; supplied human reports are journey-specific.

### WT-05 â€” Torrentio fallback card is lost when community catalogue omits/fails (C)

Fresh/offline/failed catalogue user may not find a Torrentio setup card even though a bundled native/extension configuration exists; no claim network provider itself becomes available offline.

Action: Adapt fallback insertion and stable-identity essential classification only. Preserve Beta installation vs enabled state separation and profile/generation/instance transactions; do not copy Alpha isInstalled predicate or broad setEnabled(id, true).
Active owner: Main Extensions page -> ExtensionsStorePage StoreApi.fetch -> essentials/rails; Android setup Loader625 chooses ExtensionsSetupSheetAndroid. Existing installed-instance manager remains the authoritative manage path.
Alpha: `qml/ExtensionsStoreApi.js:91-138,142-157; ExtensionsStorePage.qml:27; tests/extensions_store_torrentio_fallback_test.mjs`. Beta: `qml/ExtensionsStoreApi.js:91-111,116-128; ExtensionsStorePage.qml:28,80-83,146-201,568-586`.
Evidence: high source / physical Android unverified. Open: No current device evidence; supplied human reports are journey-specific.

### WT-06 â€” Featured Read and Details controls have no world handlers (B)

Visible featured CTAs can do nothing; this is route wiring, independent of phone layout or reader correctness.

Action: Adapt Alpha signal wiring and deferred invocation to Beta current canonical title/source identities and Loader generation. Reuse Beta readPrimary/current preference route, never force Alpha Android reader mode.
Active owner: Main world Loader -> TankobanWorld active Discover -> FeaturedCarousel -> CarouselSlide primary/secondary signals. Other library/catalogue paths remain functional.
Alpha: `qml/TankobanWorld.qml:91,205-215; Main.qml:1490-1513,4474,4731,4771-4774; tests/android022_tankoban_routes_test.mjs`. Beta: `qml/TankobanWorld.qml:204-209; FeaturedCarousel.qml forwards slide signals:115-124; Main.qml world onLoaded around4750-4780 lacks featured handler`.
Evidence: high source / physical Android unverified. Open: No current device evidence; supplied human reports are journey-specific.

### WT-07 â€” Explicit release-year top-match ranking removed (B)

Title plus release year can lose intended top match or rank remakes poorly. Provider result set still bounds ranking; this does not expand catalogue recall.

Action: Directly adapt pure ranking helper and the two existing Theatre callers; keep request cancellation/cache identity and exact-title behavior.
Active owner: Main shared SearchSurface -> WorldSearch Theatre search -> native Imdb/catalogue delivery or provider fallback. No need to replace the newer paged SearchSurface adapter.
Alpha: `qml/WorldSearch.js:112-129,147-150,343-346; tests/theatre_search_year_test.mjs`. Beta: `qml/WorldSearch.js:110-129,323-326 generic pickTopMatch calls`.
Evidence: high source / physical Android unverified. Open: No current device evidence; supplied human reports are journey-specific.

### WT-08 â€” Comic edition ledger loses in-place cancellation (C)

While waiting for an edition download, user cannot cancel at the row they initiated it from. Other download page controls remain separately available.

Action: Adapt cancellation latch/status handling into Beta active ledger, using existing facade and preserving current download lifecycle; test all supported row geometries rather than copy phone layout.
Active owner: Main.openComicSeries:2165 -> _openComicSeriesReady:2179 -> comicSeriesLayer source ComicSeriesPage:5140 -> ledger282 when DB series exists. The PARKED comment at ComicSeriesPage1 is stale: live Main library/LOCG/Collection callers still activate it.
Alpha: `qml/ComicDbLedger.qml:224,237-240,260-272,420-438; tests/qml/tst_comic_download_cancel.qml:57-106`. Beta: `qml/ComicDbLedger.qml:213-252,357-388; native/engine/ComicDownloader.h:182 and .cpp:1113; ComicSeriesPage.qml:282-292`.
Evidence: high source / physical Android unverified. Open: No current device evidence; supplied human reports are journey-specific.

### WT-09 â€” East Blue direct-area disable permits a late preview on host (C)

Supplied host test can open a late preview after direct MouseArea disable/re-enable. Its production enabled binding is touchInput (normally platform-constant), so this does not establish an ordinary navigation/device regression; retained hide/show controls pass.

Action: Narrow investigation/hardening candidate: reproduce direct-disable lifecycle through a real production interruption if possible; explicit terminal disable latch can harden that edge. Keep passing retained hide/show controls and fresh-press recovery. No blanket touch rewrite or mandatory Alpha port.
Active owner: Main Universe Loader -> OnePieceUniversePage -> OnePieceEastBlueAtlas badge MouseArea. Probe is direct-area enabled mutation; production normal hide/show differs and passed retained host controls.
Alpha: `qml/OnePieceEastBlueAtlas.qml:409-417 original direct tap (not donor); no Alpha disable-latch proof`. Beta: `qml/OnePieceEastBlueAtlas.qml:438-457; supplied host-validation/eastblue-retained-receipt.json ONE_REPRODUCED_HOST_DEFECT:4 passing rows,1 failing row`.
Evidence: high supplied host receipt/source match; production trigger and physical Android behavior unresolved. Open: Whether any supported production lifecycle toggles this exact MouseArea.enabled while pressed; normal retained hide/show passed.

### WT-10 â€” Galaxy and DCAU nested Back bypasses their local state (B)

From Galaxy destination, shell Back closes destination then closes whole Universe in same action; from DCAU hub it closes whole Universe instead of landing.

Action: Adapt Alpha boolean consumption contract in these pages. Return false only at true page root; let existing shell own outer close. Keep visible Back behavior consistent without duplicate emissions.
Active owner: Native Back -> Main.handleEscape Universe case -> requestUniverseEscape; bespoke Loader5674/5676 selects actual DCAU/Galaxy pages. Visible in-page Back uses its own route and is not equivalent to native Back dispatch.
Alpha: `qml/GalaxyUniversePage.qml:118-123; DCAUUniversePage.qml:52-60; Main.qml:980-986`. Beta: `qml/GalaxyUniversePage.qml:115-120; DCAUUniversePage.qml:50-54; Main.qml:1294-1300,1386,5674-5676`.
Evidence: high source / physical Android unverified. Open: No current device evidence; supplied human reports are journey-specific.

### WT-11 â€” East Blue transient cards lack window width caps (C)

At root width354,360-wide preview cannot fit even before inset; very short roots/large text may hide actions. Current supplied landscape canvas may not trigger this condition.

Action: Adapt only width/height caps and content/action reachability to Beta transient surface, preserve desktop composition and map focus; no auto phone map/density transplant.
Active owner: OnePieceUniversePage->EastBlueAtlas openPreview/index. Actual installed root canvas depends Activity desktop width/density; narrow354 example is a conditional calculation, not OnePlus observed viewport.
Alpha: `qml/OnePieceEastBlueAtlas.qml:604-606,819-821,837-947; tests/qml/tst_phone_atlas_previews.qml`. Beta: `qml/OnePieceEastBlueAtlas.qml:608-615,820-827,845-932`.
Evidence: high formula / occurrence conditional on actual viewport. Open: No current device evidence; supplied human reports are journey-specific.

### WT-12 â€” Alpha has structured unsigned APK identity/provenance qualification (A)

Future beta packaging has fewer reusable guardrails against mismatched package/source/native artifact or accidental signing; do not imply current APK hash invalid.

Action: Adapt helper into current Beta graph with world.getcolosseum.app.beta identity, current endpoint/native bundle, release/version policy and honest dirty/overlay provenance. Keep signing/publishing separate authorized work.
Active owner: Engineering invocation via build_apk.sh/CI, not Android runtime. Adoption affects reproducibility/qualification, not an additional user-facing app feature.
Alpha: `scripts/android/release_apk.py:65-126,173-245,265-304,349-473; build_apk.sh release branches; tests/test_android_unsigned_release.py`. Beta: `scripts/android/build_apk.sh:1-20 debug contract; .github/workflows/android.yml debug workflow; scripts/android/release_apk.py absent`.
Evidence: high source / physical Android unverified. Open: No current device evidence; supplied human reports are journey-specific.

### WT-13 â€” IMDb shorts generation difference needs content/provenance decision (H)

Possible catalogue content coverage opportunity, not proven missing decoder/playback functionality.

Action: Do not regenerate/publish during audit. Decide intended short-film catalogue scope and compare pinned datasets before bounded generation change.
Active owner: Android ImdbCatalog/CatalogRemote runtime consumes bundled or configured publication, not this Python script; generation only affects app after data publication/staging.
Alpha: `scripts/theatre_brain/build_imdb_db.py KEEP_TYPES includes short:movie and self-tests; server/content-api/source-manifest.json pinned dataset`. Beta: `scripts/theatre_brain/build_imdb_db.py KEEP_TYPES only movie,tvSeries,tvMiniSeries; source-manifest pins another dataset`.
Evidence: source difference high; shipped content consequence unresolved. Open: Intended shorts scope and exact installed/remote dataset membership.

### WT-14 â€” Host scanner lookup no longer accepts libexec kit layout (H)

An otherwise valid host kit using only libexec/qmlimportscanner would be reported missing; no demonstrated effect on current Windows kit/device.

Action: First confirm supported Linux host-kit layouts. If libexec is supported, adapt the one candidate list and add isolated fake-kit fixtures;otherwise document the intentionally supported paths. Do not install tools or run packaging from audit.
Active owner: Engineering CLI main -> find_host_qt -> check_qt -> first_existing host scanner candidates. This runs on packaging host, not inside Android app.
Alpha: `scripts/android/qualify_toolchain.py:125-133 check_qt,193 main caller; first_existing:84`. Beta: `scripts/android/qualify_toolchain.py:125-132 check_qt,192 main caller; first_existing:84`.
Evidence: High source difference; actual affected host/layout/production trigger unresolved. Open: Actual supported target host layout requiring libexec is not supplied;classification H until that prerequisite is established.

### WT-15 â€” WatchParty ancestor geometry and Wallpaper narrow selection remain conditional (H)

Ancestor-only movement could leave WatchParty available height stale unless another production callback recomputes it;near-top placement can reduce height to1. Wallpaper roots below 412 logical units can place selected panel off left. These calculations/hypotheses are not supplied OnePlus/device failures.

Action: Investigation/test adoption first. Reproduce current production owners at actual supported root sizes and ancestor movement before any implementation. If confirmed, adapt only reactive geometry dependencies and contained transient actions, not Alpha phone composition.
Active owner: PlayerPage HUD -> WatchPartyPanel with overlayParent for transient popover. Main Home/world wallpaperClicked -> openWallpaperSearch -> wallpaperLayer WallpaperSearch -> pickSelected/action wiring. Both are real routes;occurrence of affected layout remains conditional.
Alpha: `qml/WatchPartyPanel.qml:38-43 panelEffectiveHeight,136-144 controlTopInOverlay; qml/WallpaperSearch.qml:33-43 selectedPanel geometry,728-732 panel,815/827 buttons; tests/qml/tst_phone_overlays.qml:317-355 moving-ancestor case`. Beta: `qml/WatchPartyPanel.qml:37-41 panelEffectiveHeight,134-138 controlTopInOverlay; qml/PlayerPage.qml:6504 active WatchPartyPanel; qml/WallpaperSearch.qml:687-690 panel,769/781 buttons; qml/Main.qml:3374-3378 openWallpaperSearch,4135/4801 callers,6439-6452 Loader/action wiring`.
Evidence: High source difference; actual affected host/layout/production trigger unresolved. Open: Whether production HUD ancestor-only movement bypasses existing reposition notifications;actual supported Android root minimum and Wallpaper selection state at that size;no current device reproduction.
