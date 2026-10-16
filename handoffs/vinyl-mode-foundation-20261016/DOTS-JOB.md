# Build Vinyl as a functioning Colosseum mode

**One Dots-owned implementation project.** Deliver the working mode end to end. These are completion checkpoints for one project, not separately dispatched parallel jobs. Choose the practical internal execution method; keep one accountable delivery and one assembled candidate.

## Objective and scope

Finish the provider-independent Vinyl mode inside the native Colosseum app, reusing the substantial frontend, backend prototypes, existing music player and existing profile stores. A user must be able to enter Vinyl, work with real local media, navigate the approved pages, play and manage a queue, save their state, leave, restart and resume. Internet access and third-party accounts must not be prerequisites for this acceptance journey.

The deliberately deferred work is external catalogue access and external streaming discovery/resolution: YouTube/YouTube Music, Tankorent 2, MusicBrainz network access and other services. Implement clean replaceable adapters for those systems; do not make their network availability a dependency of the mode. Preserve existing provider implementations for later connection rather than deleting them.

This is implementation and native-app qualification, not another audit, UI mockup, disconnected prototype, or test-only repair. Do not stop after making individual harnesses pass.

## Exact source basis and source precedence

Repository: https://github.com/kingoftheseas56/Colosseum

1. App integration base: **`3fcdd4be620281d70b59961450d638a77439da91`**, the remote master tip verified for this handoff. Create a normal task branch from this pin. Record the pin before edits. Production master did not contain `qml/vinyl` or `native/vinyl` at receiving inspection.
2. Vinyl implementation donor: **`501bf8a3baa003d70190c00dd6d24810dc2677aa`**, branch `codex/vinyl-artist-header-20261016`. Contains the existing live session/provider machinery and later approved artist presentation, including shared DiscoverPicker and BackAction work. Import the relevant implementation and tests deliberately. Transplant the necessary app entry/build changes into the integration base; do not overwrite current `qml/Main.qml`, `native/main.cpp` or build files wholesale with older versions.
3. Repaired composed A2 donor: **`caecb8f6c97845e5673453ca4465b40f71abac8f`**, branch `dots/vinyl-a2-linux-20261016`, under `prototypes/vinyl-a2-20261016`. Reuse reviewed components and their tests where they fit production ownership. This is a donor subtree, not a branch to merge indiscriminately. Its Linux standalone suites were delivered passing; production integration remains unproven.
4. The defensive lifetime patch is an optional reviewed input, supplied alongside this job as `inputs/VINYL-LIFETIME-CANDIDATE.patch`, SHA256 **`e9e2f86f9b6daf09ad32e7b5bdab0b5eb3ca0d8cc2ea2bbb60d64a38181d180d`**. It qualified 20 full Windows artist prototype runs, but the original also passed negative-control runs. It does not establish the cause or cure of intermittent `0xC0000409`.

The current app base owns unrelated app behavior. The artist donor owns the accepted artist design. The repaired A2 subtree owns its bounded repair. Resolve overlaps by responsibility and record them. Do not merge reading snapshots such as `argon-source*`. Do not require a ZIP or access to Hemanth's laptops to perform this job.

## What already exists, and what still needs joining

- Approved World/Home/Discover/list, artist, album and song QML plus keyboard and visual components exist. Preserve them and their design language.
- Prototype `VinylPageData` imports fixtures, simulates playback state and toggles a session save flag. `VinylWorldData` has fixture-style profile states. `VinylArtistData` has session-only saved names/playlists. These are not durable production owners.
- `VinylLiveSession` already uses the existing `MpvItem`, `Collection` and `Progress` owners. It has real playback/session machinery; disc and edition selection currently have no-op implementations. Several metadata capabilities are empty. Reuse and complete this owner instead of creating a competing player/session.
- The native Python transport currently permits `search`, `album` and `resolve`; Python additionally supports artist operations. Generalizing the core service seam is in scope. Calling the external services is deferred.
- A2 supplies `IArtistSource`, `ArtistReadAdapter`, `ArtistServiceBridge`, `SavedArtistAdapter`, `RecordingPlaybackPort` and a composed factory. Its in-memory defaults and command-recording sink are prototype adapters. Production persistence and playback must connect to the real owners.
- A2 artist/recording interfaces rely heavily on MusicBrainz identifiers. Local content must have stable Colosseum-owned identity without requesting or fabricating a MusicBrainz ID. Preserve external identities as optional mappings. Inspect existing canonical identity ownership before extending it.

## Checkpoint 1 — assemble the native mode and its contracts

- [ ] Establish an isolated task branch, source manifest and reproducible native build/launch commands.
- [ ] Make Vinyl reachable through the app's appropriate mode entry, respecting existing feature gating and unrelated worlds. A standalone `ArtistPreview` window is not the completion artifact.
- [ ] Define one coherent ownership path from QML presentation through the native services/session to player and profile stores. Keep fixtures in test/dev injection, not runtime imports from `tests/fixtures`.
- [ ] Expose capabilities and truthful states for unavailable optional content. A visible action must perform its advertised operation or communicate a concrete unavailable state; silent no-ops do not count as implemented.

## Checkpoint 2 — usable catalogue-independent input and browsing

- [ ] Support real local audio through the app's existing open/import/library route. Extend that route narrowly if required. A hardcoded developer-only fixture list is insufficient as the sole user input.
- [ ] Use supplied/local metadata to populate albums, tracks and artists, including unknown fields and missing artwork. Keep browsed identity separate from playing identity.
- [ ] Make Home, Discover/local browse, Library, lists/collections, Artist, Album and Song routes function against the same owned records. If a page needs online enrichment, retain the page and show its truthful offline/empty state.
- [ ] Preserve selections, sort/filter, scroll and focus through forward/back routes and nested artist/album/song navigation. Cover empty, single-item, long and changing datasets.

## Checkpoint 3 — wire real playback to the existing player

- [ ] Feed local playable sources into the existing native music-player path. Do not simulate progress with a timer or only emit a recorded play command.
- [ ] Implement play/pause, seek, previous/next, end-of-track advancement, queue inspection and the existing queue mutation/shuffle/repeat controls the UI exposes. Specify and test boundary behavior rather than inventing extra controls.
- [ ] Keep playback alive while browsing another album or page, and keep every playback surface synchronized with the same session.
- [ ] Handle unreadable/missing files, unsupported media, decoder failure, cancellation, rapid track switches, retry, end of queue and shutdown without stale callbacks or a stuck spinner.
- [ ] Test local files and, where useful, a controlled loopback source for delayed/failing transport. A real external torrent catalogue or public streaming source is not required. Existing deterministic tone generation in `tests/vinyl-live/music_seed.cpp` can supply test audio without a media download; do not make torrent setup a prerequisite for local playback.

## Checkpoint 4 — durable user state

- [ ] Connect saved albums/artists, library membership, listening history/progress, taste-picker state and existing user-created playlist actions to appropriate existing profile-owned storage.
- [ ] Inspect `CollectionStore`, `ProgressStore`, profile preferences and the relevant personal-state contracts before adding any store. Extend the existing owner where practical; no parallel database just for convenience.
- [ ] Prove persistence after a process restart, profile separation, failed-write behavior and profile departure during a pending request. A successful in-memory toggle is insufficient.
- [ ] Restore the appropriate queue, item and position under established resume policy; do not autoplay unexpectedly on launch.

## Checkpoint 5 — finish the existing presentation and detail tools

- [ ] Connect multi-disc/edition choices and displayed metadata to the actual selected record. Complete current no-op adapter methods.
- [ ] Make supplied/local lyrics, credits, notes and related detail panels work where data is present. Missing material remains missing; external lyric/metadata fetching is not part of this job. Do not invent a new authoring/synchronization product where the existing UI is read-only.
- [ ] Preserve approved artist Albums / Singles / Videos / Live navigation, shared DiscoverPicker/BackAction, Backdrops behavior and Colosseum's shared Theme, focus and scrolling conventions. Biblio's collapsed source rail and its Filter popup are separate controls.
- [ ] Preserve the current artist photo/backdrop design and wallpaper fallback. Do not replace it with an older mock or generic redesign. No visible scrollbars; full keyboard reachability and focus restoration; check 1280x720 plus narrow/resized windows.
- [ ] Represent genuinely streaming-only Videos/Live actions as unavailable until a provider is connected, while making supplied/local playable media functional where the existing page supports it. Do not show a fake playback success.

## Checkpoint 6 — prove the mode as a whole

- [ ] Reconcile the test registry before quoting a coverage fraction. The earlier local snapshot had 18 `tst_vinyl_*.qml` files; the lifetime derivative has 19. Published donor `501bf8a3` has 17 tracked Vinyl suites, including `tst_vinyl_artist_model_replacement.qml` missing in the local snapshot; local capture/model-lifetime diagnostics are extra. Classify tests and preserve meaningful regressions; do not silently omit the missing suite or count duplicate captures as extra product coverage.
- [ ] Run the applicable component/native suites on the assembled candidate; diagnose failures without weakening assertions. Give per-suite commands, runner identity, exit code and pass/fail/skip totals. Baseline-dependent artwork failures and environment blocks need exact classification, not a blanket PASS.
- [ ] Run the real native Colosseum app on Linux and exercise the acceptance journey below. Capture protocol/app state, process exits and representative rendered screens. Distinguish this from offscreen QML component tests.
- [ ] Exercise repeated route changes, rapid open/back/close, pending-load teardown, page resize, profile changes and app shutdown under normal GC. Investigate reproducible native failures with owned-process diagnostics. Do not claim the historical intermittent crash cured solely because a repetition campaign passes.
- [ ] Reuse existing components' regression suites and verify unrelated world entry still works after app/build wiring changes.

### Required assembled-app acceptance journey

With external providers disabled and no third-party sign-in:

1. Launch the exact built app and enter Vinyl.
2. Open/add real local audio, then find it through browse or Library.
3. Navigate Artist -> Album -> Song and back using pointer and keyboard.
4. Start actual playback; observe decoder/player position and successful audio handling. Record audible-output evidence only if the environment actually provides and verifies an audio device; otherwise explicitly retain that limitation.
5. Pause, seek, queue another item, browse elsewhere, then advance and recover from one missing/unreadable item.
6. Save an item and use the existing playlist action; leave/re-enter Vinyl, restart the app and verify durable state and resume behavior.
7. Switch profiles and demonstrate separation, including a pending operation.
8. Remove/unavailable optional metadata and artwork, demonstrate honest fallbacks, then exit cleanly.

The mode is foundation-complete only when these implemented paths are connected in the assembled app, with platform/evidence limits stated. A cloud audio-device limitation must not be presented as verified audible playback. Windows receiving qualification remains a separate Codex/Preflight gate.

## External adapter seams to leave ready

Document and exercise each seam with a local adapter through the same consumer API:

- Catalogue/search/read: stable core IDs, optional provider IDs, pagination, cancellation and loading/empty/error/cached results.
- Playback resolution: core track selection to a playable source and optional request headers, with pending/failure/cancellation and stale-result protection. Source selection must not re-create the player or own durable profile truth.
- Metadata/artwork enrichment: optional inputs with provenance and missing-data behavior. Local playback and saved state must survive their absence.

Reuse `IArtistSource`/bridge/ports where appropriate and adapt their overly provider-specific assumptions deliberately. Avoid a second incompatible catalogue, history, queue or playback system. No speculative provider framework beyond the interfaces required by this functioning mode.

## Evidence, handoff and boundaries

Use `kingoftheseas56/Colosseum` task branches and `kingoftheseas56/Dots-Colosseum` as the delivery/evidence index. Source is Git-first; no source ZIP. Normal in-scope commits, pushes, builds and Actions are permitted. No production-master merge or release is authorized by this job.

Publish one evolving delivery with: exact source pins; checkpoint status; changes; build/launch/test commands; native app identity; core acceptance matrix; logs/screenshots; adapter contracts; unresolved failures and exact next action. Mark implemented, component-tested, native-app-tested and blocked states separately. Progress checkpoints do not transfer overall ownership away from Dots.

Do not download/rehost provider images with uncleared rights or access credentials to work around cloud internet restrictions. Use existing approved assets and honest fallbacks. Keep test seed data clearly separate from real user data. Preserve unrelated code and other workers' changes.

Start by inspecting the pinned sources, assembling the one candidate and completing a local playback vertical slice. Continue through the remaining checkpoints. Report genuine dependency/build blockers with exact evidence; do not replace the requested mode with an unconnected harness and call the project complete.
