# Vinyl Development Roadmap and Implementation Plan

> For agentic workers: use `superpowers:executing-plans` for the direct implementation method chosen in this conversation. Turn each milestone into a bounded execution plan when it begins. Complete its integrated app journey and evidence before declaring it accepted.

**Goal:** Deliver Vinyl as a complete music mode in the normal Colosseum app, reusing the existing backend and approved QML designs, with local music, online catalogue browsing, torrent-preferred playback, a YouTube fallback, and the planned listening and discovery features.

**Architecture:** Keep the current native/QML application and its canonical identity, Collection, Session, Progress, history/activity, and profile owners. Adapt the existing Vinyl prototypes into these owners and the existing playback/streaming services; do not install parallel state systems. Every milestone connects backend behavior to the actual app pages and receives an integrated runtime check.

**Tech stack:** C++, Qt/QML, the existing mpv integration and StreamServer, existing profile stores, the current replaceable Python catalogue/fallback transport, and the authored Vinyl backend components.

**Spec:** Hemanth's 2026-10-20 request in this conversation; the artist-page native QML captures and matching snapshot explicitly recognized by Hemanth; the already covered Home, Discover, Library, Album, Song, Artist, player, queue, and taste surfaces. This is the umbrella roadmap, not authorization to redesign those surfaces.

**Evidence baseline:** inspected repository revision `c059c6238ed8bd2b226a7606fd4a9f2cb039de73`, plus the four pending desktop-availability changes on `codex/vinyl-production-flow`. The subsequent complete backend source review covered 178 authored code/test/fixture/build files across 32 source units and 246 unique files including contracts and retained evidence. No backend test reruns or external provider qualification occurred in that review.

**Revision after the complete backend review:** this is primarily an adoption roadmap. The existing algorithms, state machines, codecs and composed journeys should be reused. Start with the repairs and native host boundary below before extending the existing QML session into another parallel backend. See the [review and source findings](../reviews/2026-10-20-vinyl-backend-review.md) and [coverage/hashes](../reviews/2026-10-20-vinyl-backend-coverage.json).

**Recovered visual reference:** Arc 62 `qa/artist-native-windows-qualify-20261015/screenshots/A-albums-1280x720.png` and matching `trees/A/qml/vinyl/ArtistPreview.qml` / `VinylArtistPage.qml`, with that snapshot's matching artwork. Hemanth recognized the captures and running page in this conversation. This establishes the design reference, not acceptance of its remaining rough edges or historical code defects.

## Global constraints

- Develop and refine Vinyl inside the actual Colosseum app. Standalone previews are visual references and diagnostics; they do not satisfy a milestone.
- Preserve the recovered native artist design. Improve its execution in context rather than replacing it with another prototype or an HTML surface.
- Core local listening requires no third-party account, network, catalogue, or helper process.
- YouTube Music is the primary catalogue. Available local music plays directly; remote playback prefers a qualified torrent source, with YouTube as the secondary fallback. Catalogue identity and playback source are separate.
- Search labels remain provider-neutral. Source/provenance details belong where they help the user understand an actual choice or failure.
- Colosseum remains authoritative for identity, library, playlists, queue/session state, history, progress, preferences, and profile boundaries.
- Prefer C++ for durable backend machinery and QML for presentation and interaction. Retain current working ownership until a justified, tested migration is complete.
- Do not import an entire prototype subsystem merely because it has tests. Reuse its algorithms, contracts and useful tests through the current owner.
- No fabricated playback, rankings, counts, credits, release dates, audio quality, artist photographs, or recommendations. Missing content gets an intentional thin-data state.
- Keep artist navigation keyboard-first, without visible page scrollbars. Use the current wallpaper when no eligible artist photo exists; retain working wheel/key scrolling.
- Keep test fixtures and synthetic profiles out of ordinary releases. User data and unrelated worktrees remain untouched.
- Review and commit bounded changes; merge and release only after the corresponding integrated checks. Authored, compiled, tested, runtime-verified, merged, and released are distinct states.

## Current position

| Surface | Existing work | What the current evidence establishes |
|---|---|---|
| App entry | `native/main.cpp`, `qml/Main.qml`, `qml/TopBar.qml`, `native/vinyl/VinylAvailability.h` | M0 desktop availability is accepted at `008aeb812` on the development branch. The ordinary full app opens Vinyl without qualification or fixture flags. Main-branch integration and release remain separate. |
| Actual music host | `qml/vinyl/VinylLiveWorld.qml` | Real routes exist for Home/Discover/Library, Album, Artist and Song, plus file import, identity decisions, player and queue. This is the implementation path to extend. |
| Local import | `native/vinyl/VinylLocalSource.{h,cpp}`, `VinylMusicMetadata.*` | M1 reads tags/duration/artwork asynchronously, groups local albums/discs, and preserves identities across partial/repeat imports. Hemanth's Illmatic FLAC folder passed actual-app receiving as one Nas album with ten tracks. Physical Windows picker selection remains unverified. |
| Listening session | `native/vinyl/VinylBackendSession.*`, `qml/vinyl/VinylLiveSession.qml` | M0F adopts the native queue/session/seek/checkpoint path through existing mpv and profile stores. Actual-app import/play/replacement/repeat/seek/cold-restore/resume/privacy/clear passed with normal exits. Ordinary picker and metadata usability remain M1. |
| Online catalogue | `scripts/vinyl/provider.py`, `native/vinyl/VinylProvider.h`, `VinylCatalogueSource.*` | M2 connects real YouTube Music search, Artist shelves, complete release/video continuations and full albums/versions through the ordinary app. MusicBrainz is optional identity/grouping enrichment. Cached restart and local listening with the helper unavailable passed. Global search is explicitly limited. |
| Torrent playback | `VinylTorrentProvider`, `MusicTorrentMatcher`, `MusicTorrentQueryPlanner` | M3 promotes the adapter through bounded scoped APIs on the existing StreamServer. Exact individual-file torrent playback and separately demonstrated real YouTube fallback passed in the normal app. Whole-album CUE spans remain unsupported in this adapter. |
| Rich artist backend | Artist read, artwork, personal, media and composed-service artifacts | M2 adapts the accepted ArtistServiceBridge to authoritative MusicBrainz snapshots and the current Collection owner; real provider data fills the existing Artist page. Provider saves keep stable keys with external identity metadata. Exact canonical recording/edition playback remains unavailable without a verified mapping; in-app video and final visual qualification remain later gates. |
| Frontend reference | Recognized native artist capture and its matching `trees/A` snapshot | The reference has matching artwork assets. It differs from current production QML. It is a design reference with historical runtime defects, not a safe wholesale replacement for newer app code. |
| Advanced features | BC/BD/BE/BF/BG backend artifacts and existing QML presentation | Substantial reusable components exist. Their presence is not evidence of live provider integration or working app controls. |

Fresh results cover M0 availability, M0R backend receiving, M0F native adoption and M1 tagged local import. M1 passed seven focused targets and two actual-app journeys (Illmatic FLAC and native PCM regression), each with three normal-exit phases. See [M1 receiving](../reviews/2026-10-20-vinyl-m1-receiving.md) for exact hashes and limits. Human ear audibility, physical Windows picker selection, online sources, main-branch landing and release are separate checks.

M2 additionally passed nine focused targets and eight final actual-app phases: real catalogue/cached restart, helper-unavailable Illmatic listening, and native queue/history regression. See [M2 receiving](../reviews/2026-10-21-vinyl-m2-receiving.md). This qualifies catalogue browsing and Artist queue admission, not remote audio decoding or installed release packaging.

## Reuse map: authored backend to product owner

`Arc 62 backend` below denotes the existing external development artifacts, not a runtime dependency or a new instruction source. Promote selected source and tests into portable repository locations when integrating them. Implementations, tests, contracts and composed journeys have now been reviewed; this roadmap does not claim fresh component passes or a sealed backend.

| Backend family | Authored components | Product use and ownership decision |
|---|---|---|
| BA0, BA1 | `VinylDomainTypes.h`, `VinylQueue.h` | Reuse identity/occurrence and queue invariants. Reconcile with current Collection and session records rather than creating another canonical domain store. |
| BA2, BA2c, BA2d | `PlaybackEngineAdapter.h`, `VinylPlaybackSession.h`, `QueueSessionCoordinator.h`, `ColosseumMpvBridge.h` | Reuse state and coordination behavior through the existing mpv/session path. No second player or sidecar player in production. |
| BA3 | `VinylSessionPersistence.h` | Reuse persistence semantics through the current Session/Progress owners; retain paused cold restore. |
| BA4 | `VinylListenEventLedger.h`, persistence component | Feed the existing activity/history ownership, respecting retention and profile scope. Do not create a duplicate listening-history database. |
| BA5 | `VinylLibrary.h`, `VinylPlaylist.h`, personal persistence | Reuse collection and playlist operations through Collection and profile stores. |
| BB1–BB5 | Candidate contract, exact-track resolver, cue/whole-album model, resolved playback source, source-resolution session | Supply source admission, selection, cancellation and fallback behavior behind the current playback path. The StreamServer mismatch must be resolved first. |
| BC1–BC3 | `TimedTextCore.h`, `TimedTextTranslation.h`, `AnnotationMeaningPlane.h` | Supply real timed text, aligned translations and annotations to the existing Song/lyrics/notes surfaces. Content retrieval adapters are a separate requirement. |
| BD1–BD2 | `VinylSignalPathTypes.h`, `VinylMpvObservability.h` | Map actual decoder/output observations into an honest signal-path presentation. |
| BE derivation, BE1–BE3 | Candidate contract, feedback/taste state, derivation, deterministic ranker | Build profile-specific Home/Discover recommendations from real catalogue and listening evidence. Current profile preferences remain authoritative. |
| BF1–BF3 | Context graph, related-video contract, video-playback adapter | Supply factual relationships and related videos, using the existing app player and audio arbitration. |
| BG1–BG2 | `ICatalogueClient`, typed entities/pages, discovery enrichment | Provide typed catalogue results, identity/edition mapping, pagination and enrichment. `FakeVinylCatalogueClient` is a test adapter, not the runtime service. |
| Artist read/artwork/personal/media/bridge and composed service | `ArtistCatalogueQuery`, `ArtistArtworkResolver`, `ArtistPersonalState`, `ArtistMediaQuery`, `ArtistServiceBridge`, `IArtistSource`, composed factory/adapters | Connect the approved Artist page to real catalogue, personal state and playback. Reuse current stores and source lifecycle; repair exact-source defects before promotion. |
| W4 composition | End-to-end composition and real/fake cue journey sources | Reuse scenario coverage when adapting components. A standalone W4 pass does not substitute for running those scenarios in Colosseum. |

### Exact external source folders

All entries are relative to the existing Arc 62 `backend/` artifact area. Build-output directories are excluded from this inventory.

- BA core: `ba0-domain-prototype`, `ba1-queue-core-prototype`, `ba2-engine-adapter-prototype`, `ba2-playback-session-prototype`, `ba2c-queue-session-coordination-prototype`, `ba2d-mpv-adoption-prototype`, `ba3-session-persistence-prototype`, `ba4-listen-event-ledger-prototype`, `ba5-library-playlists-prototype`.
- BB acquisition: `bb1-source-candidate-prototype`, `bb2-exact-track-resolver-prototype`, `bb3-cue-whole-album-prototype`, `bb4-mpv-playback-adapter-prototype`, `bb5-source-resolution-session-prototype`.
- BC text: `bc1-timed-text-prototype`, `bc2-translation-prototype`, `bc3-annotation-prototype`.
- BD observations: `bd1-signal-path-prototype`, `bd2-mpv-observability-prototype`.
- BE discovery: `be-derivation-prototype`, `be1-recommendation-candidate-prototype`, `be2-feedback-taste-state-prototype`, `be3-deterministic-ranker-prototype`.
- BF context/video: `bf1-context-graph-prototype`, `bf2-related-video-prototype`, `bf3-video-playback-adapter-prototype`.
- BG catalogue: `bg1-catalogue-client-prototype`, `bg2-discovery-enrichment-prototype`.
- Artist composition: `artist-live-prototypes-20261015`, `artist-service-composed-20261015`.
- Composed journeys: `w4-end-to-end-prototype`; inspect source composition separately from archived build artifacts.

## Review focus

1. The same recording appears in multiple releases, editions or playlists: a saved occurrence must retain its intended edition, disc and track, and reimport must not silently retarget it. Owned by M1/M2/M4.
2. A profile changes or the app closes while a browse, import, resolution or write is pending: old work must not mutate the new profile or outlive shutdown. Owned by M1/M2/M3/M8.
3. A provider times out, paginates incompletely, loses artwork or expires a URL: retain usable cached/local content, report the actual state and reject stale responses. Owned by M2/M3/M5/M7.
4. A torrent/fallback resolves the wrong recording, live version or cue span: admit exact-enough media or report no suitable source; never silently play a different song. Owned by M3/M6.
5. A long or thin-data page is used with keyboard/back navigation, narrow width, display scaling or rapid route changes while audio plays: preserve focus, route state, readability and the one playback session. Owned by every UI milestone, finalized in M8.

## Milestone order and deliverables

### M0 — Establish the normal in-app development baseline

**Outcome:** one reproducible ordinary desktop Colosseum build opens Vinyl, with the recovered design available as a reference and no developer opt-in needed.

**Files:** `native/vinyl/VinylAvailability.h`, `native/CMakeLists.txt`, `native/main.cpp`, `qml/Main.qml`, `qml/TopBar.qml`, `scripts/installer/package_release.sh`, `tests/vinyl_availability_harness.cpp`.

**Consumes/produces:** preserves `VinylLiveEnabled`, `worldSourceFor("Vinyl")` and the existing world loader; enables the normal desktop path without enabling synthetic fixtures.

- [x] Finish review and commit the existing availability change; keep Android disabled until its own milestone.
- [x] Preserve the fixture/qualification packaging guards and ordinary offline/local launch.
- [x] Pin the recognized artist reference and matching artwork for comparison; retain current app fixes rather than copying the old snapshot wholesale.
- [x] Capture the normal app entry and its actual empty-library state, with build/profile identity recorded.
- [x] Record this as availability accepted, not complete-mode acceptance.

**Status (2026-10-20): M0 accepted.** The ordinary desktop build opens Vinyl with both qualification and synthetic fixtures OFF. Hemanth confirmed entry and supplied the actual empty-library screenshot. Release/qualification availability checks were rerun during closeout: 2/2 passed. The earlier full-app build and packaging-policy checks are retained evidence; audible playback, import and restart remain later gates. The recognized visual reference is the Arc 62 artist-native-windows-qualify-20261015 A snapshot with its matching artwork, identified in this conversation; it is not the current production page implementation. This branch still requires main-branch integration and release qualification.

**Exit gate:** a normal build opens Vinyl from the app's own navigation with qualification/test fixtures OFF and the Vinyl environment variable unset. A native preview is insufficient.

### M0R — Repair and receive the authored backend baseline

**Outcome:** a portable, pinned source set with its adoption-blocking defects repaired and exact-source evidence restored. Preserve the original external artifacts; perform implementation in isolated promoted copies.

**Sources:** the existing BA/BB/BC/BE/BF/Artist implementations and W4 build/journey sources identified in the backend review. Do not rewrite their working algorithms.

- [x] Recover/apply the known Artist composition fixes for plain-int `lengthMs` and non-default-constructible `SourceBatch`; prove the exact target build rather than relying on header shims or an unrelated repaired copy.
- [x] Bind BB5 durable source-memory restore to the expected profile with a typed mismatch. Coalesce expiry refresh and preserve current-work identity when forwarding playback failure.
- [x] Reconcile BA2C's continuing playback plus pending replacement with BA3 capture/restore. Preserve both resume and pending intent; a valid checkpoint must not fail during source replacement.
- [x] Give bounded/invalid seeks, failed commands, missing acknowledgments and timeout/EOF/cancellation defined terminal behavior while retaining exact request identity.
- [x] Repair the source-proven numeric/persistence, text-anchor/hash, graph normalization and rank-input boundary findings in their owning components, with focused counterexample tests from review R6–R8.
- [x] Repair Artist cross-lane cancellation tickets, continuation usability/revision checks and media unavailable envelope handling; qualify authoritative edition occurrences instead of the fake's permissive fallback.
- [x] Run the required real CUE seek-time case without weakening its invariant, then run the retained composed/reference checks plus new regressions. Assert required real-test registration; an absent fixture must not reduce the qualifying set silently.
- [x] Freeze hashes after passing exact-source receiving evidence. The new adopted baseline supersedes the retained W4-I 36/37 receipt; it does not retroactively seal the old Arc artifacts.

**Status (2026-10-20): M0R accepted.** The promoted baseline clean-built on Windows Qt 6.11.1/MSVC, passed all 47 harnesses plus 40 repeated real-engine runs and six inventory-guard cases, and has a pinned 151-file source/control manifest. Independent review found no Critical/Important findings and one protected standalone artwork nil-UUID validation inconsistency deferred to M0F. The historical CUE timing failure did not reproduce; deterministic startup/sequence/queued-seek regressions did and are repaired. See `docs/reviews/2026-10-20-vinyl-m0r-receiving.md` and its source manifest. This qualifies the backend baseline, not the live facade or actual-app journey.

**Exit gate:** the repaired adopted source builds, focused counterexamples pass, required W4 journeys actually execute, and the artifact/evidence inventory is reproducible. These checks still do not replace actual-app qualification or audible output.

### M0F — Adopt the backend through one native app boundary

**Outcome:** the missing full BH1 service facade connects authored backend behavior to Colosseum's existing owners and pages.

**Files/owners:** introduce the bounded native Vinyl service and adapters under `native/vinyl/`, reusing the Artist QObject bridge where appropriate; wire through `native/main.cpp`, `native/CMakeLists.txt`, `qml/Main.qml` and the existing live Vinyl data/session adapters. Compile shared backend implementations once rather than importing all standalone CMake graphs unchanged.

- [x] Define the host music-reference boundary with local Vault identity and optional verified MBID/edition mappings. Local music must work without network, invented MBIDs or a second identity store.
- [x] Adopt BA1/BA2 queue/session/coordinator behavior behind one native command/event owner. Preserve the current app's repeat-one, previous/manual wrap, shuffle and occurrence semantics; the reference coordinator only supplies Off/Queue repeat.
- [x] Attach BA2-D's concrete playlist-entry and seek request attribution to the existing mpv core. Preserve working host output controls and prevent simultaneous competing QML/native playback controllers.
- [x] Adapt BA3 checkpoints through current Progress storage and migrate existing QML snapshots. Establish Collection save/unsave and BE2 preference ports; complete BA5 playlist operations in M4.
- [x] Integrate BA4 facts through the existing Activity/history owner, preserving privacy, activation thresholds, retention and reset behavior. Never count BA4 and the current tracker as independent listens.
- [x] Expose immutable presentation/query state and explicit commands/events to QML with profile generation, cancellation and shutdown ownership. Use real source/store ports; default fakes and the Artist factory's in-memory bytes port cannot become production dependencies.
- [x] Align the standalone artwork nil-UUID validator with BA0 while receiving the facade; keep canonical-ID checks at every exposed entry. The composed adapter already rejects nil IDs.
- [x] Move one complete existing local play/queue/replace/checkpoint/restart journey onto the adopted path, with before/after compatibility tests and actual-app proof.

**Status (2026-10-20): M0F accepted on the development branch.** Backend 48/48, app native targets 3/3 and native-source QML bridge 13/13 pass. The normal full app completed three phases with exit 0: local import/save/play/replacement/repeat/seek, paused cold restore/resume/privacy/clear, and another restart retaining the cleared queue and earlier history. Decoder/output reported PCM through WASAPI. The dedicated pause notification repair restores Activity accumulation without duplicate history ownership. See [M0F receiving](../reviews/2026-10-20-vinyl-m0f-receiving.md) for exact-source/runtime evidence and limits. Full Artist/catalogue, acquisition, BA5 playlists and tagged-file usability remain their named milestones.

**Exit gate:** one app session/player/state authority, preserved local and existing behavior, profile-safe asynchronous work, and a demonstrable full-app journey through the adopted backend. M1 then extends ingestion/usability on this foundation rather than growing a competing implementation.

### M1 — Make the local listening journey genuinely usable

**Status:** implementation and automated actual-app receiving accepted; the physical Windows picker interaction in the exit gate remains unverified. See [receiving report](../reviews/2026-10-20-vinyl-m1-receiving.md).

**Outcome:** an ordinary folder/selection of tagged music becomes a meaningful library and plays through the app without a hand-authored manifest.

**Files/owners:** `VinylLocalSource.{h,cpp}`, `VinylLiveWorld.qml`, `VinylLiveSession.qml`, `VinylLiveWorldData.qml`, `VinylLiveArtistData.qml`, existing VaultIdentity, Collection, Session, Progress, activity/history and mpv owners. Reuse BA0–BA3 contracts where they fill a demonstrated gap.

**Interfaces:** keep `importFiles(QStringList)` and `decideImportIdentity(relationship, choice)` at the import boundary; preserve `request/cancel/completed` lifecycle and current persisted occurrence identities.

- [x] Add actual title/album/album-artist/track-artist/disc/track/duration/artwork extraction behind the local source, reusing existing media facilities where suitable. Keep extraction asynchronous and tagless files usable; do not guess missing identities from filenames. Embedded FLAC art and conventional sidecars are covered; not all container artwork is qualified.
- [x] Group tagged tracks into local albums/discs, preserving file identity and existing queue occurrences. Repeat/partial import and prior automatic-record migration are covered; canonical online editions remain M2.
- [x] Wire file/folder dialogs, repeat import, changed-file Cancel/Same Media/New Media, missing/unreadable media and mixed-success feedback. Cancellable folder traversal avoids cycles; these contracts are covered by focused checks.
- [ ] Qualify physical file/folder selection through the default Windows picker. Current actual-app receiving invokes the production import command programmatically; native UI automation was unavailable.
- [x] Exercise Library → Album → Song → Artist → Back inside the full app while playing; receive existing queue/repeat/seek/EOF behavior again on this build.
- [x] Fix the observed album hero artwork failure in the actual route using the existing rounded image renderer. Broader cross-page visual refinement remains M5.
- [x] Verify profile isolation, final pending writes, paused cold restore and normal shutdown on this exact build.

**Checks to extend/reuse:** `tests/vinyl_local_source_harness.cpp`, `tests/vinyl-native-session-bridge/`, `tests/vinyl-local-session/`, personal/session harnesses, plus tagged/tagless/multi-disc/duplicate/missing-file fixtures.

**Exit gate:** import real tagged files through the default Windows picker, browse correct artist/album records, play and edit the queue, restart paused at the saved position, and exit normally. Keep screenshots, logs and actual output evidence. This is a useful local milestone, not the complete Vinyl end state.

### M2 — Connect online catalogue, identity and the real Artist service

**Status:** Windows development-app catalogue/Artist receiving accepted. YouTube Music remains primary; MusicBrainz is optional enrichment. See [implementation and decisions](2026-10-20-vinyl-m2-implementation.md) and [receiving report](../reviews/2026-10-21-vinyl-m2-receiving.md).

**Outcome:** provider-neutral search and artist browsing expose real artists, albums, editions, singles, videos and live releases in the existing pages.

**Files/owners:** `scripts/vinyl/provider.py`, `native/vinyl/VinylProvider.h`, `native/main.cpp`, `VinylLiveSession.qml`, `VinylLiveWorldData.qml`, `VinylLiveArtistData.qml`, `VinylArtistData.qml`; promote the needed BG1/BG2 and composed Artist interfaces into the native build with bounded adapters.

**Interfaces:** preserve independent browse/play cancellation; adapt `ICatalogueClient` typed IDs/pages and `ArtistServiceBridge` requests/results to the existing QObject/QML boundary. The wrapper must accept the implemented artist operations and their required payloads rather than dropping them at its current search/album/resolve allowlist.

- [x] Review the exact composed-service repair and dependencies; adapt the accepted bridge through current Collection/session ownership rather than the factory's independent personal bytes/player ports.
- [x] Connect YouTube Music and existing MusicBrainz identity/group/version enrichment, retaining distinct provider/Vault/canonical identities. Provider-version grouping is not an attested concrete MusicBrainz recording/edition occurrence.
- [x] Add runtime search, album, Artist, complete release/video continuations and identity requests, preserving latest-request/profile fences and independent cancellation. Reuse helper error envelopes and MusicBrainz rate limiting/backoff; expose deliberate failure states through the existing QObject callback.
- [x] Bind artist saves to Collection with a stable provider key and external identity metadata. Bind Play/Shuffle to real release admission through the existing session; local playback continued while all canonical group pages loaded. Artist-wide sequencing remains later work.
- [x] Receive loading/empty/error/cached/offline states and artwork fallbacks. Global search and inline video shelves without continuations remain explicitly limited/partial.
- [x] Connect actual artist photographs through the existing image manager and artwork resolver; receive four-photo rotation and cached restart. Remove the exposed image-selection option and prototype description, and redesign artist search with cover rows and keyboard navigation. See [artwork and UI receiving](../reviews/2026-10-21-vinyl-m2-artwork-receiving.md).
- [x] Add an app-owned, hash-pinned Windows runtime preparation/staging contract and test ordinary app-relative launch. Clean-machine installer/distribution qualification remains M8; no PATH Python fallback is the production contract.

**Checks:** provider tests in `tests/vinyl-live/test_provider.py`, artist model-replacement/page/live-artist tests, promoted typed client/bridge tests, then a real catalogue browse in the full app with provider errors and pagination exercised.

**Exit gate:** search → Artist → Albums/Singles/Videos/Live → Album/edition works with real data inside Colosseum. A narrow/unknown artist and an offline restart remain usable. Local listening still works with all online helpers unavailable.

### M3 — Finish the acquisition and playback source stack

**Outcome:** selecting a remote recording reaches playable audio through the intended source policy and the one existing player.

**Files/owners:** `native/vinyl/VinylTorrentProvider.{h,cpp}`, `native/torrent/MusicTorrentMatcher.*`, `MusicTorrentQueryPlanner.*`, `native/player/streamserver.h` and its implementation, `VinylProvider.h`, `native/main.cpp`, `VinylLiveSession.qml`; adapt BB1–BB5 rather than inventing another resolver/player.

**Interfaces/blocker:** the donor adapter expects `requestManifest`, `requestFile`, `cancelRequest` and request-scoped manifest/stream/error signals. Current StreamServer does not supply that contract. First establish a compatible adapter or scoped extension over its actual transport; do not uncomment CMake entries against nonexistent methods or replace the shared server wholesale.

- [x] Run an early bounded compatibility investigation once M0 is stable, so the largest integration risk is known while M1/M2 progress. Pin the exact transport contract and shared-media regressions before coding it.
- [x] Support cancellable request ownership, manifest/file admission and cleanup without disrupting Theatre, readers or other streams.
- [x] Apply source policy: available local media directly; otherwise a suitable torrent; YouTube fallback after explicit source failure/no-match according to the existing candidate contracts. Bound attempts and preserve useful failure reasons.
- [ ] Match the intended recording/edition and selected torrent file. Support authored whole-album/cue semantics, correct spans and next-track behavior where applicable; reject ambiguous or wrong-version candidates.
- [ ] Resolve URLs just in time, preserve queue occurrences, and cancel stale work on skip, route/profile change and shutdown. Expired fallback URLs must be re-resolved rather than persisted as canonical media.
- [ ] Exercise buffering, seek, queue transitions, timeout, retry, source failure, fallback and cache cleanup through the real app player.

**Checks:** `tests/vinyl-live/torrent_provider_contracts.cpp`, `stream_contracts.cpp`, `TorrentSessionContracts.qml`, music matcher harness, adapted BB/W4 cue journeys, and full-app source failure/fallback/output witnesses.

**Exit gate:** demonstrated torrent playback and a separately demonstrated YouTube fallback in the normal app, with cancellation, seek/EOF and clean shutdown. Passing a local-file test cannot close the online transport gate.

**2026-10-21 receiving:** this online exit gate passed for individual audio files: Nas N.Y. State of Mind through Tankorent, and Eminem Stan (feat. Dido) through real YouTube after no exact torrent match. Both retained canonical identity, decoded to WASAPI, sought, paused/resumed, advanced at EOF and shut down their owned runtimes. All six focused targets passed. The app is left open on Illmatic. See [M3 receiving](../reviews/2026-10-21-vinyl-m3-receiving.md). The unchecked extended/CUE requirements above remain work; this does not declare the entire source stack or release complete.

### M4 — Complete personal library, playlists and listening state

**Outcome:** Vinyl behaves as a durable personal music library across sessions and profiles.

**Files/owners:** current Collection/Session/Progress stores, profile preferences and activity/history bridge; `VinylLiveArtistData.qml`, `VinylLiveSession.qml`, `VinylLiveWorldData.qml`, `VinylQueuePanel.qml`. Adapt useful BA3–BA5 and BE2 behavior through those owners.

- [ ] Verify and complete save/unsave for artist, album and track records, plus playlist create/rename/delete, occurrence ordering and removal within the already planned library UI.
- [ ] Keep playlist occurrence identity separate from recording identity; preserve explicit unavailable entries when a source disappears.
- [ ] Complete queue persistence, repeat/shuffle state, paused restore, history/progress recording and search/taste persistence without duplicate databases.
- [ ] Check write failures, retention/privacy controls and profile switching with pending writes/resolutions.
- [ ] Migrate durable QML logic into existing/native owners only where correctness or maintainability requires it, preserving serialized state and behavior.

**Checks:** personal-state/session/taste harnesses; playlist-occurrence tests; local-session lifecycle suites; real save/edit/restart/profile journeys with exact cold readback.

**Exit gate:** personal actions survive restart and stay within the correct profile; unavailable media and write failures are visible without destroying saved intent.

### M5 — Bring every covered page to product quality in the app

**Outcome:** the covered frontend works as one coherent music experience with the recovered artist visual language.

**Files:** `VinylHomePage.qml`, `VinylDiscoverPage.qml`, `VinylWorld.qml`, `VinylAlbumPage.qml`, `VinylAlbumSections.qml`, `VinylArtistPage.qml`, `VinylArtistHero.qml`, `VinylArtistCard.qml`, `VinylArtistShelf.qml`, `VinylSongPage.qml`, `VinylPlayerBar.qml`, `VinylQueuePanel.qml`, `VinylTastePicker.qml` and their actual live data adapters.

- [ ] Carry the approved artist composition into live routes: hero/backdrop synchronization, media tabs, release filters, artist search and return-state preservation. Fix layout, fonts, spacing, image ratios, subtle controls and focus using the running app.
- [ ] Complete Album track/disc/edition selection, credits and source-aware play/queue actions. Complete Song back/album/artist navigation with consistent playback state.
- [ ] Finish Home/Discover/Library responsive layouts, search/lenses, list expansion, taste picker, empty states, player docking and queue layout.
- [ ] Audit every visible control against an actual operation. Implement planned actions in the owning milestone or show an honest unavailable state; remove fixture-derived display assumptions from the live path.
- [ ] Verify at 1280×720, 1600×900 and a larger desktop size, including Windows scaling, long titles, thin artist data, missing artwork, keyboard-only navigation, wheel scrolling and reduced motion.

**Checks:** existing per-page QML suites and world journeys plus comparative screenshots from the actual app with real data. Include repeated artist/model replacement under ordinary Qt garbage collection; no diagnostic-only GC mode as acceptance.

**Exit gate:** Library → Album → Artist → Song → Queue → Back remains coherent while audio plays, across the covered window sizes. Product polish happens during M1–M4 as well; this milestone closes the remaining cross-page consistency gaps.

### M6 — Connect the Song listening features and real video/context

**Outcome:** lyrics, translations, annotations, signal information and related videos use authored backend behavior and actual supported content.

**Files/owners:** `VinylSongPage.qml`, `VinylLyricsColumn.qml`, `VinylNotesColumn.qml`, `VinylNotePicker.js`, `VinylCreditsPopup.qml`, current mpv/session integration; promote BC1–BC3, BD1–BD2 and BF1–BF3 through bounded native presentation adapters.

- [ ] Connect timed/plain lyrics and actual content provenance. Seeking to a cue and following playback must use the real playback clock and selected recording/version.
- [ ] Connect the authored translation alignment and annotation models to a supported runtime content/translation adapter. The prototypes do not by themselves establish a content source or translation engine; select and verify that adapter before accepting the feature.
- [ ] Reconcile covered screen fields with actual data contracts: edition label/catalogue number, annotation votes and artist-verified flags are absent from the reference shapes. Extend those shapes only with genuine source semantics/provenance; otherwise present an intentional absent-data state.
- [ ] Complete notes/credits/context navigation, preserving language, selected cue and route state. Thin-data tracks remain readable and playable.
- [ ] Map real mpv/device observations into signal information; show unknown values honestly and distinguish source format from actual output.
- [ ] Connect related video selection and playback through the existing player/audio ownership. Restore a coherent audio session after leaving video; replace the current no-op video action only when the path actually works.

**Checks:** promoted BC/BD/BF tests; seek/alignment/version mismatch checks; Song QML and native clock integration; actual lyrics/notes/signal/video journeys, including missing data and failed providers.

**Exit gate:** each supported feature performs a real operation in Colosseum and has an intentional absent-data state. A demo-only translation, annotation or video does not qualify production integration.

### M7 — Make Home and Discover reflect the listener

**Outcome:** the covered Home/Discover surfaces offer useful real music and explainable recommendations rather than fixture shelves.

**Files/owners:** `VinylLiveWorldData.qml`, `VinylHomePage.qml`, `VinylDiscoverPage.qml`, `VinylTastePicker.qml`, `VinylWorldLists.js`; adapt BE derivation/BE1–BE3, BG2 and relevant BF context through current profile preferences and history.

- [ ] Supply cold-start artist choices from the real catalogue; save feedback in the active profile and make later edits work.
- [ ] Derive candidates from actual listening/library/taste evidence and real catalogue/context relationships, using the authored deterministic ranker and explanation evidence.
- [ ] Make Home resume/recent/library/recommendation rows and Discover filters/lists/search useful without manufacturing popularity or filling gaps with unrelated items.
- [ ] Apply deduplication, missing-data, repeat exposure and offline/cached behavior; respect privacy/retention choices when deriving recommendations.
- [ ] Define live generator contribution deduplication and eligibility filters, artist-wide versus exact-entity suppression, and passive-versus-deliberate completion policy before exposing recommendation claims. Persist exposure through the current profile owner and count actual shown items, not every computed ranking.
- [ ] Verify a fresh profile, sparse library, established listener, changed taste and offline profile in the running app.

**Checks:** promoted BE/BG tests, world-list/personal-state/privacy tests, deterministic ranking fixtures and real Home/Discover journeys.

**Exit gate:** profile changes and real listening/taste choices affect relevant rows predictably; explanations reflect actual evidence and offline operation retains useful local music.

### M7R — Implement radio/autoplay when promoting that planned capability

**Outcome:** BE4 generates bounded, cancellable sequences from qualified recommendation candidates and feeds the existing queue/session. BE4 is explicitly deferred in the authored backend; no existing sequencer was found to adopt.

- [ ] Implement sequence ownership, duplicate/fatigue/admission policy and refill behavior over accepted M7 candidates, preserving explicit User versus Radio enqueue origin.
- [ ] Make stop/manual queue changes/profile changes cancel pending refill; a late result must not restart or repopulate a stopped session.
- [ ] Test actual in-app radio/autoplay with sparse candidates, exhaustion, failed source resolution and restart policy; keep passive listening distinct in history/taste derivation.

This capability is explicit remaining development. It is not supplied by a candidate enum, ranker or a test fixture tagged Radio.

### M8 — Harden the whole app and ship desktop Vinyl

**Outcome:** the complete intended desktop mode is integrated, reproducibly packaged and qualified as part of Colosseum.

**Files/owners:** touched Vinyl and shared owners, existing release scripts, deployed resource manifest and tests. Do not add a second packaging pipeline.

- [ ] Resolve surviving races, model lifetime/GC failures, slow large libraries, cancellation leaks, corrupt/unavailable records and write-failure recovery using reproduced cases.
- [ ] Verify Vinyl ↔ Theatre ↔ Audiobook audio arbitration, Vault identity/collection consistency, history/progress, profile boundaries and ordinary app shutdown.
- [ ] Verify the packaged provider runtime/dependencies, real user assets, source resolution and update behavior from an installed build on a clean user environment, without developer PATH or preview assets.
- [ ] Complete the final default-picker import, identity decisions, online browse, torrent/fallback playback, queue/EOF, restart, personal-state, enrichment and keyboard/resize matrix on the exact final source/build.
- [ ] Save screenshots, relevant raw logs and audio-output evidence with build/revision/profile identity. Inspect and commit only intended paths, integrate current master changes, then rerun affected receiving checks on the merged build.
- [ ] Release only after the desktop completion matrix below is accepted. Track optional provider/device checks honestly; do not transfer evidence from a donor or earlier binary.

**Exit gate:** ordinary installed Colosseum exposes and runs the intended desktop Vinyl feature set, with no development flags or synthetic fixtures, and the integrated receiving report has no unresolved release-blocking failure.

### M9 — Qualify additional platforms as their own product slices

**Outcome:** platform support is intentional and independently proved.

- [ ] Keep Android unavailable until its actual audio backend, local import/permissions, background lifecycle, streaming transport, catalogue runtime and responsive controls are supported.
- [ ] Reuse portable contracts and QML, adapt platform services to the same canonical owners, and test the installed package on the target device.
- [ ] Qualify desktop platforms separately where shipped; Windows proof does not qualify Linux or Android output, packaging or device behavior.

This is the follow-on platform milestone. It does not block a clearly scoped Windows release, and a Windows release must not claim unsupported platforms.

## Dependencies and execution policy

Primary sequence: **M0 → M0R → M0F → M1 → M2 → M3 → M4 → M5 → M6 → M7 → M8**. M7R follows M7 when radio/autoplay is promoted into the release scope; M9 follows per target platform.

- Perform the bounded M3 transport compatibility investigation after M0 so its risk is understood early; do not wait for finished recommendation or visual polish to discover a transport redesign requirement.
- Refine each page while its live data/operation is being connected. M5 is a final cross-page quality gate, not a license to defer frontend work until all backend milestones finish.
- M4 reuses existing personal state throughout M1–M3; its exit gate closes the complete library feature set.
- Once core identities/playback are stable, M6 content work and M7 ranking work can progress independently if execution is explicitly delegated later. Their final acceptance remains inside the same app.
- Keep an implementation slice small enough to review independently: precise source revision, current owner, actual UI journey, focused checks and a compact receipt.
- Treat earlier passes as evidence to reuse only when exact source/dependency/environment equivalence is established. New code, new wiring or a new platform needs its affected checks.
- Preserve named backlog distinctions: gapless/crossfade, multi-FILE CUE, specific-edition saves, fingerprint escalation, optional sync/scrobbling/ListenBrainz, persistent audio/video preference and learned ranking are not implemented merely because the baseline domain exists. Scope each deliberately rather than silently claiming it complete or making every optional extension a desktop-release prerequisite.
- Dates should be assigned after M1 and the M3 compatibility investigation establish throughput and the transport risk; this roadmap does not invent calendar certainty.

## Desktop completion matrix

| Area | Required proof on the final app |
|---|---|
| Entry and local use | Normal navigation, no account/developer flags, offline usable library and native picker import |
| Local metadata and identity | Tagged/tagless/multi-disc import, correct artwork/order, duplicate and changed-file decisions, missing-file behavior |
| Catalogue and artists | Real search, identity/editions, complete pagination, Artist tabs and thin/offline/error states |
| Playback | Local + torrent + YouTube fallback, exact selected recording, buffering/seek, queue/shuffle/repeat/EOF and output evidence |
| Personal state | Save/playlist occurrence behavior, history/progress/taste/search persistence, paused cold restore and profile isolation |
| Covered pages | Approved visual direction, functional controls, keyboard/back/focus, responsive layout and complete real-data journeys |
| Listening features | Real timed/plain lyrics, supported translations/annotations, factual credits/context, actual signal observations and video lifecycle |
| Discovery | Real catalogue candidates, explainable profile-scoped ranking, useful cold/sparse/offline states |
| Shared app | Theatre/Audiobook arbitration, Vault/Collection consistency, pending writes, repeated routes and normal shutdown |
| Distribution | Installed build with shipped dependencies, correct resources, no fixtures, qualified updates and merged-source receiving report |

## First execution slice

M0, M0R, M0F and M1 implementation/automated receiving are accepted on the development branch; M1's physical Windows picker interaction remains unverified. M2 catalogue/Artist receiving is accepted with real and cached data in the ordinary app. M3's individual-file torrent-preferred/YouTube-fallback online exit gate is now accepted; authored whole-album CUE integration and extended source lifecycle qualification remain unchecked. Main-branch landing and release qualification remain explicit receiving steps.
