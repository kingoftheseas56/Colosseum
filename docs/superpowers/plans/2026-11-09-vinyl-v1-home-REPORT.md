# Vinyl v1 Home execution report

## Current source and authority

Execution resumed on 2026-11-12 from `origin/master` at `049f1fbd59771082856bd057dadd131120842dac`, including the 1.3.0 release work. The archive source is `bb346e35a377caea50f89e626ec9a47e09534b14`. Hemanth directed publication to master and C:-only execution.

The local archive checkpoint is `4358b6e32df6000938c0e2e5d47327c724692fd8`. The full build and regression run used `21fd507e35ad0b173a41fecf004e2de87a12f92e`, which preserves the five newer Linux release commits through `d0b2f0d2b0cf10e93b785c405e65f8fe1c932a15`. Current integration source `89d98e5272b00fc32fd9dc469bd3e7e7179bfe92` also preserves `origin/master` at `f0f92894b7a9d426a1890e7d1b111dcc7dbb1a46`: seven README/account-QML/MCU-QML/keyboard-test paths changed after the full regression source. The approved starting SHA remains `049f1fbd59771082856bd057dadd131120842dac`.

The Slice 0 pre-publication review used that integrated source. Its latest-master runtime-ready refresh passed, the app binary remained unchanged, and the targeted Android account keyboard QML check passed 9/9. Full regression results below remain explicitly tied to `21fd507`, rather than being relabelled as a run of the later source. Publication and subsequent receiving updates are recorded by slice below.

The checkout, build, temporary files, logs, and evidence are on C:. The ignored receipt `artifacts/vinyl-v1/slice0/20261112/resume-baseline.json` records their exact locations. The shared dirty checkout is preserved.

## Slice 0 - Foundation

**Overall: the archive was pushed to master at `b353114d945573f817ac16464313db5dea2e0ac5`, with remote readback verified. The full native build, safe regression run and ON/OFF entry checks completed. Named Vinyl gaps and the unchanged-source stream-server fixture failure remain explicit; this is not an all-green verdict. Slice 1 receiving is recorded below.**

The archive is merged locally with eight resolved conflict paths. Shared-shell resolutions retain current Android presentation guards, the Universes loading surface, Collections tooltip timing, and Nuvio registrations. Four Vinyl component conflicts use the archive implementation with current tooltips restored on surviving controls.

The deterministic registry contains **97 Vinyl unit CTests** and the same **197 non-Vinyl names** as the starting source. The test ledger and harness map now identify Vinyl. Harness `inspect vinyl` succeeds; global map freshness metadata was retained.

### Changes verified during receiving

- Tagged yt-dlp extraction disables its shared dependency cache. The new behavioral regression failed before the fix; the complete bespoke video-transport suite then passed 20 tests.
- CTest supplies its selected Python interpreter to the native fake catalogue transport. A single-case discriminator passed, then the complete catalogue-source CTest passed in 30.04 seconds with no process-level Python override.
- Windows shortcut fixtures now use valid `.lnk` paths and assert their link type. Production fixture guards were preserved. The controller CTest passed. [Qt 6.11 link behavior](https://doc.qt.io/qt-6.11/qfile.html#link).
- Two Node sandboxes now supply the current QML context. Their original behavioral assertions pass. The Album prototype connects the current video signal; Album tests exercise the named song action, source controls, and retained artwork scale with scrolling.
- Artist replacement tests reveal the real tab and prove it is selected before activating the updated card. Carousel tests isolate independent cases. Navigation tests preserve the same carousel instance, verify Home-only presentation, and wait for deferred Back restoration. Their receiving CTests pass.
- A native regression exposed completed-empty mixes disappearing, leaving no retry action. Removing one filter retains their existing error/retry cards. Home listening, recommendation service, and recommendation contracts passed **3/3** after rebuilding.
- Fixture-prefix containment checks that were red for a missing leaf under a directory junction and a regular-file path now pass in the existing controller suite: **55 checks, CTest 1/1 in 0.55 seconds**. Only the owned C: junction was removed. The ON configuration restore/build and final ON/OFF Home entry runs have since passed.
- The final disposable ON and OFF Home-entry scenarios passed **13/13** and **9/9**. The ON Loader check observes the bridge's string `"Ready"`; both app sessions exited 0 without crashing and shut down gracefully. The invalid-tag control returned Lanista 4/app 2 without a crash and created neither tagged data nor cache roots. Exact receipts and boundaries are in `artifacts/vinyl-v1/slice0/20261112/fixture-runtime-observations.json`.
- The planned negative control compiled one deliberately incorrect recommendation assertion. CTest failed for actual 10 versus expected 11. The original bytes/hash were restored, the target rebuilt, and CTest passed **1/1**. The final receipt is `negative-restored-verified.*`; an earlier missing-wrapper/stale-binary attempt is retained and excluded from this claim.

### Verification evidence

| Check | Observed result |
| --- | --- |
| Starting source | `049f1fbd59771082856bd057dadd131120842dac` |
| Current local integration | `89d98e5272b00fc32fd9dc469bd3e7e7179bfe92` includes archive checkpoint `4358b6e32df6000938c0e2e5d47327c724692fd8` and latest received `origin/master` `f0f92894b7a9d426a1890e7d1b111dcc7dbb1a46`; pre-publication checkpoint. |
| Current-source and receiving configuration | MSVC / Qt 6.11.1 passed; wrappers and exits are under `configure/`. |
| Registry | 294 unit entries: 97 Vinyl and 197 other; non-Vinyl names unchanged. |
| Shared-owner preservation | Ten comparisons passed: seven named sections, two whole files, and native CMake normalized for only four reviewed Vinyl test-label additions. `postmerge-preservation.json`. |
| Focused native build | Initial and additional target builds passed. |
| Focused receiving observations | 78/97 Vinyl CTests passed; 19 retained failures are listed below. Every Vinyl entry was executed across scoped runs. `focused-observations.json` is an observation index, not a monolithic final-tree suite verdict. |
| Negative control | Correct compiled red, exact source restoration and rebuilt green. |
| Full native build | All 186 requested native/unit/runtime targets built at `21fd507`, exit 0. The controlled incomplete j1 attempt remains separate; j2 completed 1680 resumed actions without compiler/linker errors. `build-app-unit-final-j2.*`, `build-final-root-review.json`. |
| Latest-master refresh | At `89d98e5`, runtime-ready/lanista refresh exited 0 and updated the QML manifest. App SHA256 remained `890ccf980b50ee8987466c3c4d533a570dec00f74991abc44a46020420cfc38f`. Targeted account keyboard QML check passed 9/9; one Windows portrait-window geometry warning, no physical Android IME claim. `latest-master-runtime-build-02.*`, `latest-master-keyboard.*`. |
| Safe non-Vinyl CTests | 195 executed: 183 passed, 12 failed, 0 CTest skips; native CTest exit 8, 808.58 seconds. `non-vinyl-safe-final.log/.exit/.xml` and `non-vinyl-safe-final-command.json`. Two registered Vault tests were excluded for proven D-path I/O and remain unexecuted, neither passes nor baseline failures. `no-d-test-path-audit.md`. |
| Tagged Home boot with gate ON/OFF | Final ON session `20261112-232203-3cacfbcd` passed 13/13 with Loader status `Ready`, Home page, `canEnterVinyl=false`, and artist-selection gate true. Final OFF session `20261112-230948-a05034a7` passed 9/9 with Home visible and Vinyl qualification disabled. Both apps exited 0, did not crash, and stopped gracefully. |
| Invalid-tag control | `20261112-230238-4a3b4041`: Lanista exit 4, app exit 2, no crash; tagged data/cache paths were absent. This is a negative control, not a normal boot pass. |
| Commit and push | Integrated source `89d98e5272b00fc32fd9dc469bd3e7e7179bfe92` was published with its report as `b353114d945573f817ac16464313db5dea2e0ac5`; remote readback matched. `publication.json`. |

Evidence names are relative to `artifacts/vinyl-v1/slice0/20261112/`. The first receiving gate stopped at 61/98 completed to correct the fake catalogue runtime. A 41-test completion selection, core repair checks and focused UI repairs provide the current observations. No blanket green claim is made for the 19 retained archive failures.

The exact final commands, scenario/app/QML hashes, tag-derived C: roots, runner/app exits, step counts, and page replies are recorded in `fixture-runtime-observations.json`. Earlier observations remain separate: the 11-step ON capture preceded outer Loader readiness; a Welcome-timeout run remains incomplete; and a strict numeric `status == 1` wait failed because the bridge serializes Loader enum values as strings. The diagnostic probe returned `"Loading"`, then component readiness and the Home/taste-gate state; the final strict wait now checks `"Ready"`. The numeric check was a serialization mismatch, not a Vinyl product failure.

### Store and fixture isolation

Vinyl personal state uses the existing tagged/profile owners. The provider cache receives the tagged cache root; yt-dlp's separate shared cache is disabled whenever the tag variable is present. The read-only `vinylPlayerBar.playingTrackIndex` seam is zero-based within the current disc/edition, distinct from queue position.

The new Windows fixture startup validates its tag, unique Lanista pipe, Drive mode, tagged AppData, source-root Main.qml, fake helper and absolute Python before personal stores/logging. It selects the fake provider and installs a rejecting Qt proxy, with shared service warmups suppressed for this fixture route. This is a bounded Qt transport fence. The account credential implementation was also inspected: active/revocation/deletion/Stremio/Nuvio targets use the nonempty tag's hash namespace. Final ON and OFF sessions used distinct tag-derived C: data/cache roots; the invalid-tag control created neither. These boot/entry observations do not establish playback or live-provider behavior.

Current `COLOSSEUM_VINYL_IN_APP` semantics control availability; both configurations retain compiled Vinyl machinery. The ON/OFF receiving checks exercise this existing gate.

### Retained archive failures and owners

These are separate from Hemanth's accepted non-Vinyl baseline. Original failed assertions remain recorded in the receiving logs and `failure-audit.json/.md`. Source owners below distinguish obsolete harness contracts, planned shell replacement, and unresolved interaction coverage. No production fallback was added for untyped test ports. A filtered storage probe confirmed its unsave never invoked the fake store, so that failure does not establish lost membership during import.

| CTest | Cause / receiving boundary | Current owner |
| --- | --- | --- |
| `colosseum.qml.vinyl_artist_onboarding_service_native` | One-artist/Skip expectations conflict with current native minimum-five rule. | `VinylRecommendationService.cpp; tst_artist_onboarding_service.qml` |
| `colosseum.qml.vinyl_artist_page` | Old tab/sort interfaces; shared picker Return/focus remains unresolved. | `VinylArtistPage.qml; WorldTabBar.qml; DiscoverPicker.qml` |
| `colosseum.qml.vinyl_artist_repair` | Old per-pill activation/focus and obsolete sticky-scroll threshold. | `VinylArtistPage.qml; WorldTabBar.qml` |
| `colosseum.qml.vinyl_artist_scrollbars` | Calls absent tab activate() before scrollbar assertions. | `WorldTabBar.qml; tst_vinyl_artist_scrollbars.qml` |
| `colosseum.qml.vinyl_artist_shared_picker` | Popup closes; focus assertion targets a decorative child instead of the shared bar. | `VinylArtistPage.qml; WorldTabBar.qml` |
| `colosseum.qml.vinyl_catalogue_browse` | Fixture assigns removed catalogueBrowse property; behavioral assertions never run. | `VinylWorld.qml; tst_vinyl_catalogue_browse.qml` |
| `colosseum.qml.vinyl_catalogue_pages_native` | Entry setup blocks deeper paging assertions; those paths remain unqualified. | `VinylLiveWorld.qml; tst_catalogue_pages.qml` |
| `colosseum.qml.vinyl_discovery_native` | Entry setup is gated; old discover fanout contract predates the music discovery adapter. | `VinylDiscoverPage.qml; VinylMusicDiscoverAdapter.qml` |
| `colosseum.qml.vinyl_home_native` | Null preferences and old Home/search/shelf expectations. | `VinylLiveWorld.qml; VinylLiveWorldData.qml` |
| `colosseum.qml.vinyl_live_artist` | Per-pill focus expectation differs from the shared tab bar focus owner. | `VinylArtistPage.qml; WorldTabBar.qml` |
| `colosseum.qml.vinyl_live_world_native` | Empty legacy taste setup blocks entry and dependent route/control assertions. | `VinylLiveWorld.qml; tst_live_world.qml` |
| `colosseum.qml.vinyl_local_controls` | Container enabled state differs from the nested action that owns navigation. | `VinylAlbumPage.qml; VinylArtistLinks.qml` |
| `colosseum.qml.vinyl_local_navigation_native` | Legacy direct writes to read-only player fields, old shell names, gate and untyped fake ports. | `VinylLiveSession.qml; VinylBackendSession.cpp; tst_local_navigation.qml` |
| `colosseum.qml.vinyl_personal_library_native` | Fixture lacks current entry state, so saved-song browse is blocked. | `VinylLiveWorld.qml; VinylPersonalLibraryData.qml` |
| `colosseum.qml.vinyl_personal_state` | One-artist commit/reset-clear expectations conflict with current five-artist/edit contract. | `VinylLiveWorldData.qml` |
| `colosseum.qml.vinyl_personalized_home_native` | Gate-ready legacy saved-shelf/layout/search expectations require alignment with approved Slices 6/7. | `VinylLiveWorldData.qml; VinylHomePage.qml` |
| `colosseum.qml.vinyl_session_storage_seams_native` | Untyped fake store/source ports fail native signatures; snapshot version expectation is obsolete. Unsave never reached the fake store. | `VinylBackendSession.cpp; VinylLiveSession.qml; tst_session_storage_seams.qml` |
| `colosseum.qml.vinyl_world_journeys` | Removed language-filter/Skip controls; fixture predates current discovery/onboarding. | `VinylDiscoverPage.qml; VinylTastePicker.qml` |
| `colosseum.qml.vinyl_youtube_browse_native` | Obsolete Songs/Albums/Artists tabs are advertised without mounted destinations/requests; Slice 1 retires them. | `VinylWorld.qml; VinylLiveWorldData.qml` |

Artist and local-session interaction failures are not aesthetic acceptance. Shared picker keyboard routing and deeper paths blocked by obsolete fixtures remain unqualified. The obsolete standalone catalogue destinations are explicitly scheduled for Slice 1 replacement; legacy Home shelf expectations require the approved Slice 6/7 contract, rather than silently changing the v1 design.

### Accepted earlier non-Vinyl failures

Hemanth accepted the sixteen failures from the previous full baseline as pre-existing, with investigation required for code Vinyl touches. The source contract review found stale structural assertions around current owners. In particular, Home uses a visibility binding, Android navigation lives in `PhoneShellHost`, native first-frame work uses a helper lambda, and manga Back uses the current exact-series route.

The current merge comparison preserves those owner sections. The related snapshot, recorder, account and tracker APIs will also be exercised through the focused Vinyl contracts. A new failure in affected behavior requires diagnosis.

- `colosseum.reader2_runtime_contract`
- `colosseum.startup_deferral_contract`
- `colosseum.startup_responsiveness_probe`
- `colosseum.home_hidden_work_contract`
- `colosseum.world_lifecycle_contract`
- `colosseum.phone_immersive_nav_contract`
- `colosseum.phone_keyboard_avoidance_contract`
- `colosseum.wide_android_navigation_contract`
- `colosseum.android_symbol_fallback_contract`
- `colosseum.back_action_p0`
- `colosseum.qttest.account_attachment_runtime`
- `colosseum.qttest.account_adoption`
- `colosseum.qttest.livestore`
- `colosseum.qttest.tracker_anilist_sync`
- `colosseum.qttest.trakt_direct`
- `colosseum.qttest.tracker_delivery`

### Completed non-Vinyl reconciliation

The safe run preserved the 197-name baseline registry and executed 195 entries. Ten of the sixteen accepted earlier failures recurred: the reader2, startup-deferral, startup-responsiveness, hidden-Home-work, world-lifecycle, phone-immersive-navigation, phone-keyboard-avoidance, wide-Android-navigation, Android-symbol-fallback and Back-action contracts listed above. The other six earlier account/tracker failures passed on this run; no claim is made that Vinyl fixed them.

Two additional failures were investigated separately from that accepted set:

- `colosseum.qttest.journey_play_video` timed out in the aggregate run under a software/offscreen renderer. Its unchanged test, player sources and fixture passed one focused rerun with the required Windows/OpenGL renderer: CTest 1/1, native 21 passed/0 failed/0 skipped, exit 0 in 43.49 seconds. This qualifies the renderer environment; it does not change the aggregate 183/195 result. `journey-native-renderer.*`.
- `colosseum.qttest.stream_server_failfast` failed `ownedChildWithDeadListenerRestartsPendingPlay`: the fake child missed its 500 ms readiness deadline, leaving `stream.ready()` false. One focused rerun reproduced it (native CTest exit 8, 19.21 seconds). The test, fake child, StreamServer implementation/header and exact target build block are byte-identical to the starting `049f1fb` source. The exact runtime cause is unresolved; no original-base runtime was executed, so this is not presented as a proven historical baseline failure. The Windows compatibility messages came from an expected negative-start slot that passed. `stream-failfast-focused.*`, `stream-server-failfast-source-audit.md`.

The stream fixture is outside Vinyl's changed implementation. Its unresolved readiness failure is retained as an out-of-Vinyl verification gap, without blocking the bounded archive integration or expanding into unrelated repair. The two excluded tests are `colosseum.qttest.vault_stores` and `colosseum.qttest.vault_browse_detail`; `vault_index` was source-cleared and executed. The no-D review is bounded source evidence, not a whole-process filesystem trace. `non-vinyl-root-reconciliation.json` preserves the classifications.

### Layer status

| Layer | State |
| --- | --- |
| Qt Test | Core, catalogue, account-owned Vinyl contracts and scoped fixes passed; exact selection receipts retained. |
| Qt Quick Test | Registered and executed; current pass observations and named archive gaps above. |
| Existing harnesses | Accepted earlier failures and bespoke exclusions retained. |
| Lanista | Final ON passed 13/13 with string Loader status `Ready`; final OFF passed 9/9 with Vinyl disabled on Home. Earlier incomplete and diagnostic attempts are kept separate in `fixture-runtime-observations.json`. |
| Human aesthetic verdict | Not applicable to Slice 0. |
| Integration / publication | Source `89d98e5272b00fc32fd9dc469bd3e7e7179bfe92` preserves latest received master; published as `b353114d945573f817ac16464313db5dea2e0ac5`. |

### Test classification and next action

Deterministic individual native/QML cases and fake-provider scripts are unit-labelled. The standalone `tests/vinyl-live` runtime/provider family remains bespoke, with its existing source-only Node runtime-integrity exception. The remote-media native-session aggregate, capture-only visual suite, exposed-GPU MPV event tests, tagged import and standalone prototype backend projects retain their separate classifications.

The build, safe regression run and latest-master refresh are complete, and the scoped archive integration is published. Preserve the named 19 Vinyl gaps, accepted baseline classification, separate renderer discriminator, unresolved unchanged-source stream fixture and two unexecuted no-D checks. Later component work and Hemanth's square-widget appearance approval remain separate from this Slice 0 evidence. No release, deployment, live-provider or full-page acceptance is claimed.


## Slice 1 - Three-tab house shell

Slice 0 was pushed to `origin/master` at `b353114d945573f817ac16464313db5dea2e0ac5`; remote readback matched. Shell implementation and offline scenarios were received as `051a87f14` and `0c07d4204fc08fb280e0eb8d93535a714b7a2971`. Main now uses the house WorldPage with Home, Discover and Library, retaining one existing runtime/session/player/detail owner. The compatibility wrapper remains only for standalone component consumers. Discover presentation is unchanged; Home/Library are intentional intermediate placeholders.

At `0c07d4204`, runtime-ready/lanista/component harness build passed. The configured registry is 296 unit tests (99 Vinyl + 197 other). New shell, native runtime and existing world-navigation CTests passed 3/3. Component negative controls and prior receiving checks remain in `slice1/preintegration-C1`.

Actual fixture sessions `20261113-030918-64cacba6` and `20261113-030952-66733177` passed 55/55 and 13/13. Five real picker selections were saved through the current preferences owner, all three tabs were clicked, Theatre round-trip passed, and restart retained the choices with Home default. Unique pipes shared only the same task tag and C: stores. Both apps and launchers exited 0 gracefully, without a crash. Root viewed all three tab images and copied them into `slice1/receiving/captures`.

The PNGs are 1920x1080 physical pixels. CLI capture export settings did not resize the app window; exact 1280x700 composition is unverified and remains a visual receiving prerequisite. The scenario's capture labels were corrected after the run without changing actions/assertions. Logs retain Qt-proxy image failures, empty-icon fallback and GL-context warnings during shutdown; no reference/type/binding-loop errors were found. This qualifies shell routing and persistence, not warning-free rendering, live providers, playback or later Home/Library content.

Build regeneration initially failed while C: had only about 25 MB free. Only 1,616 regenerable .obj intermediates from this task's completed non-Vinyl test targets were removed (1,167,960,579 bytes); built programs/libraries/source/logs/evidence were retained. The subsequent scoped build passed. Exact checked paths and outcomes are in `build-intermediate-reclaim-plan.json`, `build-intermediate-reclaim.json` and `build-shell-runtime-03.*`. A prior wrapper-command rejection and failed-generation log remain separate, not build passes. Do not rerun the whole 186-target build merely to recreate these unnecessary intermediates.

Evidence authority: `artifacts/vinyl-v1/slice1/receiving/shell-root-review.json`, `shell-runtime-pair.json`, `shell-units.*` and the two session manifests. This is the Slice 1 publication review checkpoint. Continue the source-pool/native integration and the already appearance-approved square widgets.


## Slice 3 - Square widgets

Slice 1 was pushed to master at `c1d005a6d77d6658191da9dfed3555e2bd6bbae1`, with remote readback matched. Independent Slice 3 was then received as `e1ab180113b099e1586ab56bc6a0d16569e65baf` and `30255b7d63e8844e462438996a4fa24921bed906`. The sole ledger conflict preserved the qualified Slice 1 text and received only the new Slice 3 section.

Song, Album, Artist, Mix and Radio forms share square footprints and aligned two-line captions at 130/196 logical pixels. Song uses an over-cover title strip with actual font metrics; Artist has a record without its former glass frame; Mix has its J-card; Radio is square; Album's square jewel case is retained. All four ArtistWidget hosts preserve visible names. Card pointer actions and feedback remain covered.

Runtime-ready build passed at `30255b7d6`. Registry: 297 unit entries, 100 Vinyl plus the unchanged 197 other names. Focused square faces, widget integration, picker, Album-page and Discover-page CTests passed 5/5 in 36.53 seconds. The production-component gallery passed 3/3 and reproduced the exact previously approved PNG SHA256 `bca5e97ccbd04fd53102203db4117246c8db11c4bd6eff30e6b9f95747e2072d`. Receiving widget/caption/gallery sources equal the approved ones after line-ending normalization; raw hashes are preserved separately.

Hemanth's direct **"Looks right"** verdict covers all five forms at both sizes. This completes the slice's square-test and recorded appearance criterion. It does not approve the later full Home/Library pages, exact 1280x700 viewport, new app playback or live providers. The earlier restored song-height negative control and its two failing cases remain in `slice3/preintegration-C1`; no extra bridge run was ordered for this component look.

Detailed evidence is under `artifacts/vinyl-v1/slice3/receiving/`: `root-review.json`, `approved-source-receiving.json`, `square-units.*`, `gallery-receiving.*`, plus the copied human verdict and original source receipts. Source-pool implementation and the later hero/Crate/Spin/Home/Library slices remain unfinished; this is a scoped publication checkpoint, not Vinyl v1 completion.

## Slice 2 - Metadata adapter checkpoint

The source adapters now provide MusicBrainz identity aliases and separately bounded discography, Apple charts for ten storefronts, Last.fm global charts and per-spelling similarity, and ListenBrainz Labs similarity. External cards retain their source identity and use the existing conservative resolver; metadata does not attest playback. Release eligibility uses twelve calendar months and retains incomplete dates as incomplete.

The existing native provider now validates these operations, caches positive metadata daily, retains the last good chart on failed/empty refresh, separates source cooldowns, and starts optional time limits when a worker actually starts. MusicBrainz processes share a bounded transient pacing gate. A queued-cooldown regression first launched an extra helper, then passed after dispatch rechecked the cooldown. Earlier semantic reds, the corrected compile error and restored greens remain separate in the evidence.

Seven affected targets built. The final selected CTests passed **11/11 in 84.57 seconds**, including the native pool/service/ranker/contracts, provider lifecycle, catalogue, annotations, Discover page and three Python suites (54 identity/discography, 23 source, 10 Discover cases). The existing bespoke discovery contract passed **13/13** with network connections fenced. Registry: **299 unit entries, 102 Vinyl plus the unchanged 197 other names**. Receipts: `artifacts/vinyl-v1/slice2/receiving/pool-api-final-checks.*`, `pool-api-queued-build-02.*`, `pool-queued-cooldown-green.*` and `discovery-bespoke.*`.

**Slice 2 remains in progress.** Home does not yet request the new sources or consume the merged pool; its existing shared eight-second deadline still needs separating from optional enrichment. This checkpoint does not qualify the complete Home page, a new app binary or live providers. Continue that integration and the planned page checks; later UI components are prepared separately and remain unintegrated.
