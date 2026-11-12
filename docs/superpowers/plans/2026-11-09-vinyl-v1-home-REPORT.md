# Vinyl v1 Home execution report

## Current source and authority

Execution resumed on 2026-11-12 from `origin/master` at `049f1fbd59771082856bd057dadd131120842dac`, including the 1.3.0 release work. The archive source is `bb346e35a377caea50f89e626ec9a47e09534b14`. Hemanth directed publication to master and C:-only execution.

At this report update, `origin/master` is `d0b2f0d2b0cf10e93b785c405e65f8fe1c932a15`, five Linux release commits beyond the starting snapshot, across 13 non-overlapping paths. The recorded initial `049f1fbd59771082856bd057dadd131120842dac` source SHA is unchanged; integration of the newer origin ancestry into the local receiving tree remains pending.

The checkout, build, temporary files, logs, and evidence are on C:. The ignored receipt `artifacts/vinyl-v1/slice0/20261112/resume-baseline.json` records their exact locations. The shared dirty checkout is preserved.

## Slice 0 - Foundation

**Overall: archive implementation and deterministic registration are local; runtime and regression verification remain incomplete. No integration commit or push yet.**

The archive is merged locally with eight resolved conflict paths. Shared-shell resolutions retain current Android presentation guards, the Universes loading surface, Collections tooltip timing, and Nuvio registrations. Four Vinyl component conflicts use the archive implementation with current tooltips restored on surviving controls.

The deterministic registry contains **97 Vinyl unit CTests** and the same **197 non-Vinyl names** as the starting source. The test ledger and harness map now identify Vinyl. Harness `inspect vinyl` succeeds; global map freshness metadata was retained.

### Changes verified during receiving

- Tagged yt-dlp extraction disables its shared dependency cache. The new behavioral regression failed before the fix; the complete bespoke video-transport suite then passed 20 tests.
- CTest supplies its selected Python interpreter to the native fake catalogue transport. A single-case discriminator passed, then the complete catalogue-source CTest passed in 30.04 seconds with no process-level Python override.
- Windows shortcut fixtures now use valid `.lnk` paths and assert their link type. Production fixture guards were preserved. The controller CTest passed. [Qt 6.11 link behavior](https://doc.qt.io/qt-6.11/qfile.html#link).
- Two Node sandboxes now supply the current QML context. Their original behavioral assertions pass. The Album prototype connects the current video signal; Album tests exercise the named song action, source controls, and retained artwork scale with scrolling.
- Artist replacement tests reveal the real tab and prove it is selected before activating the updated card. Carousel tests isolate independent cases. Navigation tests preserve the same carousel instance, verify Home-only presentation, and wait for deferred Back restoration. Their receiving CTests pass.
- A native regression exposed completed-empty mixes disappearing, leaving no retry action. Removing one filter retains their existing error/retry cards. Home listening, recommendation service, and recommendation contracts passed **3/3** after rebuilding.
- Fixture-prefix containment checks that were red for a missing leaf under a directory junction and a regular-file path now pass in the existing controller suite: **55 checks, CTest 1/1 in 0.55 seconds**. Only the owned C: junction was removed. The app-guard rebuild and final Home boots remain pending.
- The planned negative control compiled one deliberately incorrect recommendation assertion. CTest failed for actual 10 versus expected 11. The original bytes/hash were restored, the target rebuilt, and CTest passed **1/1**. The final receipt is `negative-restored-verified.*`; an earlier missing-wrapper/stale-binary attempt is retained and excluded from this claim.

### Verification evidence

| Check | Observed result |
| --- | --- |
| Starting source | `049f1fbd59771082856bd057dadd131120842dac` |
| Current-source and receiving configuration | MSVC / Qt 6.11.1 passed; wrappers and exits are under `configure/`. |
| Registry | 294 unit entries: 97 Vinyl and 197 other; non-Vinyl names unchanged. |
| Shared-owner preservation | Ten comparisons passed: seven named sections, two whole files, and native CMake normalized for only four reviewed Vinyl test-label additions. `postmerge-preservation.json`. |
| Focused native build | Initial and additional target builds passed. |
| Focused receiving observations | 78/97 Vinyl CTests passed; 19 retained failures are listed below. Every Vinyl entry was executed across scoped runs. `focused-observations.json` is an observation index, not a monolithic final-tree suite verdict. |
| Negative control | Correct compiled red, exact source restoration and rebuilt green. |
| App-first native build | `colosseum_runtime_ready` plus `lanista` MSVC build passed: 280/280 actions, exit 0. |
| Remaining 197 non-Vinyl CTests | Pending; the saved selection preserves every baseline name. |
| Tagged Home boot with gate ON/OFF | First ON session `20261112-222522-39132498` passed 11/11; tagged C: data/cache and graceful app/Lanista exit 0 were observed. Its capture preceded outer Loader readiness. Final ON capture and compiled OFF remain pending. A repeat timed out at Welcome and is incomplete, not a pass. |
| Commit and push | Pending final verification/report. |

Evidence names are relative to `artifacts/vinyl-v1/slice0/20261112/`. The first receiving gate stopped at 61/98 completed to correct the fake catalogue runtime. A 41-test completion selection, core repair checks and focused UI repairs provide the current observations. No blanket green claim is made for the retained archive failures.

The first ON session passed 11/11 and closed its tagged app and Lanista process with exit 0, but the screenshot was taken before the enclosing Vinyl Loader reached `Ready`. The repeat session `20261112-223839-83a707b8` timed out at Welcome: PowerShell treated native stderr as terminating under `Stop`, interrupting launcher cleanup. Root verified the child PID, executable path, and tag, then gracefully closed only that owned child and recorded the cleanup. This attempt is incomplete, not a pass. The fixture now waits for `worldLoader_Vinyl.status == 1` and for `accountWelcomeContinueLocal.visible == true`; the wrapper handles native stderr separately while preserving the real Lanista exit.

### Store and fixture isolation

Vinyl personal state uses the existing tagged/profile owners. The provider cache receives the tagged cache root; yt-dlp's separate shared cache is disabled whenever the tag variable is present. The read-only `vinylPlayerBar.playingTrackIndex` seam is zero-based within the current disc/edition, distinct from queue position.

The new Windows fixture startup validates its tag, unique Lanista pipe, Drive mode, tagged AppData, source-root Main.qml, fake helper and absolute Python before personal stores/logging. It selects the fake provider and installs a rejecting Qt proxy, with shared service warmups suppressed for this fixture route. This is a bounded Qt transport fence. The account credential implementation was also inspected: active/revocation/deletion/Stremio/Nuvio targets use the nonempty tag's hash namespace. The first ON session observed tagged C: data/cache; final ON capture and compiled OFF verification remain pending.

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

### Layer status

| Layer | State |
| --- | --- |
| Qt Test | Core, catalogue, account-owned Vinyl contracts and scoped fixes passed; exact selection receipts retained. |
| Qt Quick Test | Registered and executed; current pass observations and named archive gaps above. |
| Existing harnesses | Accepted earlier failures and bespoke exclusions retained. |
| Lanista | Initial ON scenario passed 11/11 with process exit 0; its capture preceded outer Loader readiness. A repeat timed out at Welcome and is incomplete. Fresh ON capture and compiled OFF are pending. |
| Human aesthetic verdict | Not applicable to Slice 0. |
| Integration / publication | Local merge; uncommitted and unpushed. Newer origin ancestry integration remains pending. |

### Test classification and next action

Deterministic individual native/QML cases and fake-provider scripts are unit-labelled. The standalone `tests/vinyl-live` runtime/provider family remains bespoke, with its existing source-only Node runtime-integrity exception. The remote-media native-session aggregate, capture-only visual suite, exposed-GPU MPV event tests, tagged import and standalone prototype backend projects retain their separate classifications.

Capture a fresh ON boot after the existing Loader and welcome readiness waits, then verify the compiled OFF boot. Resume any remaining native-unit compilation and the 197 non-Vinyl checks. Reconcile the newer origin ancestry, update session manifests and outcomes, and inspect the final diff before integration. The 19 named Vinyl failures and accepted 16 non-Vinyl baseline failures remain distinct; no final-green verdict is recorded.
