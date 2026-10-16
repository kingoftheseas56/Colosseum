# Build Vinyl as a functioning Colosseum mode

**One Dots-owned implementation project, delivered as four sequential reviewable builds: 0.25, 0.5, 0.75 and 1.0.** These labels name acceptance milestones, not measured percentages, production releases or Colosseum app versions. Keep one accountable delivery and one assembled candidate. Deliver each build for Codex receiving review and wait for acceptance before advancing; fix rejected criteria within that build.

## Objective and scope

Finish the provider-independent Vinyl mode inside the native Colosseum app, reusing the substantial frontend, backend prototypes, existing music player and existing profile stores. A user must be able to enter Vinyl, work with real local media, navigate the approved pages, play and manage a queue, save their state, leave, restart and resume. Internet access and third-party accounts must not be prerequisites for this acceptance journey.

The deliberately deferred work is external catalogue access and external streaming discovery/resolution: YouTube/YouTube Music, Tankorent 2, MusicBrainz network access and other services. Implement clean replaceable adapters for those systems; do not make their network availability a dependency of the mode. Preserve existing provider implementations for later connection rather than deleting them.

This is implementation and native-app qualification, not another audit, UI mockup, disconnected prototype, or test-only repair. Do not stop after making individual harnesses pass.

Read `REUSE-MAP.md` alongside this job. It identifies current shared owners from Tankoban, Biblio and Theatre and adds the cross-world acceptance cases. Treat that as an initial implementation checkpoint inside this same project, not a separate audit assignment. Reuse the current app-base implementations; older donor copies must not downgrade shared components.

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

## Delivery gates for every build

For EACH of 0.25, 0.5, 0.75 and 1.0, publish:

- **Dots owns cloud testing before delivery.** Compile the candidate, launch the actual native Colosseum app in Dots' cloud graphical environment, exercise that build's acceptance journey and relevant regressions, and repair failures before presenting it as passing. Do this at every build, not only 1.0. A source commit, successful compilation, component suite or unexercised screenshot alone is not a tested build.
- Exact Git commit and delta from the last accepted build, plus a runnable native Linux app artifact with checksum and launch instructions. If a native build cannot be produced, report BLOCKED rather than silently substituting a QML preview.
- A short acceptance checklist with actual PASS / FAIL / BLOCKED / NOT RUN, reproducible commands, native exit codes, test totals, representative app screenshots and known regressions.
- Include screenshots from the exact running native app showing the build's important states, plus interaction receipts/app-state or protocol evidence and logs that support the claimed behavior. Use a short recording where a sequence is clearer than still images. Screenshots prove presentation; player/protocol state, test logs and actual interactions must prove playback, persistence and other behavior. Record cloud OS, Qt/runtime, artifact identity, fixture/local-media identity and evidence paths. State audio-device/audibility limitations explicitly.
- A build-specific report and machine-readable state in the one Dots delivery index. The next build starts from the last accepted source pin. Do not overwrite prior receipts.
- Deliver each checkpoint as **cloud-tested and review-ready**, or identify it honestly as blocked/incomplete. Codex then receives/reviews Dots' code and proof, requests bounded corrections if needed and explicitly accepts the checkpoint before Dots advances. This receiving review is additional verification, not a transfer of Dots' testing responsibility. Windows/Preflight receiving evidence is recorded separately from Dots Linux evidence. Hemanth may review any build; manual testing by Hemanth is not a prerequisite for agent verification.

Each build must remain a working native mode. Test changed behavior and meaningful regressions at every gate; do not defer all testing to 1.0. No production merge or release at a milestone.

## Build 0.25 — the real playable vertical slice

**Review outcome:** enter Vinyl in the native Colosseum app, open local audio, navigate Library -> Album -> Song, play/pause/seek through the existing player, return/back and exit cleanly.

- [ ] Create the isolated integration branch and source manifest from the exact inputs above. Record shared-owner use/extend/keep-specific decisions from `REUSE-MAP.md`, then implement; no separate audit assignment.
- [ ] Assemble the native mode entry, minimal Library/Album/Song path and music-session owner. Preserve unrelated worlds and existing feature gating. A standalone ArtistPreview is not the build artifact.
- [ ] Support usable local-file input through the app's open/import/library route, extending narrowly if necessary. A hardcoded developer fixture list is insufficient as the sole user input.
- [ ] Connect real local playback, play/pause and seek, with truthful missing-file/decoder error states. Do not simulate time or only record a play command. Keep browsed identity distinct from playing identity.
- [ ] Establish stable core identity and replaceable read/source-resolution contracts. Demonstrate a local adapter through the same consumer API intended for future catalogue/streaming adapters; no external account or catalogue lookup required.
- [ ] Reconcile the source/test registry. The earlier snapshot had 18 Vinyl suites; the lifetime derivative has 19 physical files. Published donor 501bf8a3 has 17 tracked suites, including model-replacement coverage missing in the snapshot. Classify local diagnostics and restore meaningful regressions instead of blindly quoting 18/18.
- [ ] Use isolated test/profile storage. Keep fixtures in tests/dev injection rather than production imports from tests/fixtures. Actions not implemented until a later build must be visibly unavailable, not misleading successful controls.

**Gate evidence:** native-app launch and entry; actual player position/seek/pause state; local input -> Album -> Song -> Back; missing-file recovery; clean process exit; focused component/bridge checks. Clearly state audio-device/audibility limits.

## Build 0.5 — all core pages and the complete queue connected

**Review outcome:** navigate the full existing Vinyl frontend against owned local records while one real music session keeps every playback surface synchronized.

- [ ] Connect Home, Discover/local browse, Library, lists/collections, Artist, Album and Song to the same owned records. Online enrichment is optional and has truthful offline/empty states.
- [ ] Preserve sort/filter, selections, scroll, focus and nested history across routes. Cover empty, single-item, long and refreshed datasets; keep playback alive while browsing elsewhere.
- [ ] Complete previous/next, natural end-of-track advancement, queue inspection/mutation and the shuffle/repeat controls the existing UI exposes. Specify/test boundaries rather than adding speculative controls.
- [ ] Wire multi-disc/edition choices and current metadata/detail tools. Supplied/local lyrics, notes and credits work when present; missing material remains missing. Complete current no-op adapter methods without inventing a new authoring/sync product.
- [ ] Preserve accepted artist Albums / Singles / Videos / Live tabs, shared controls, artist backdrop rotation and wallpaper fallback. Streaming-only actions remain honestly unavailable; supplied/local playable media uses the real player where supported.
- [ ] Apply appropriate shared shell, collection keyboard and scrolling behavior without replacing accepted destination information architecture. No visible scrollbars; full keyboard focus return; check 1280x720 and a narrow/resized window.

**Gate evidence:** native walkthrough of every route; queue and playback-continuity journey; representative normal/empty/error/fallback states; relevant page, keyboard, queue and regression suites. This build is not yet accepted as restart/profile-complete.

## Build 0.75 — durable state and full app lifecycle

**Review outcome:** saved user state survives restart, profiles stay separate, and Vinyl behaves correctly alongside the rest of Colosseum.

- [ ] Wire saved albums/artists, library membership, listening progress/history, taste-picker state and existing user playlist actions to appropriate existing profile-owned stores. Inspect CollectionStore, ProgressStore and personal-state contracts before extending ownership; no competing convenience database.
- [ ] Prove persistence after process restart, queue/item/position restore under the established resume policy and no unexpected autoplay. A successful session-only toggle is insufficient.
- [ ] Handle profile switch or departure during playback/pending work; demonstrate no cross-profile library, preference, queue or activity leakage.
- [ ] Integrate existing search-history/activity preferences. Use the shared activity path for real consumption and correct pause/seek/EOF transitions; no fabricated listening time or separate activity database. An absent/failing optional activity sink must not break playback.
- [ ] Integrate appropriate taskbar/open-session capture/restore. Test Vinyl -> Biblio/audiobook -> Theatre/player -> Vinyl under the existing audio-handoff policy, avoiding unintended competing playback and stale controls. Do not confuse shell session state with the authoritative music queue.
- [ ] Surface failed durable writes and verify shutdown flushing. Apply shared background scheduling/cancellation if needed for import/metadata work rather than adding another global worker pool.

**Gate evidence:** cold process restart; two isolated profiles; failed-write behavior; pending-operation/profile-switch recovery; cross-world audio/session handoff; activity opt-out checks; native clean exits plus focused persistence/lifecycle regressions.

## Build 1.0 — Vinyl foundation acceptance and hardening

**Review outcome:** all previous builds' acceptance criteria remain passing on one assembled candidate, with provider-independent behavior qualified and external adapter contracts ready.

- [ ] Run the applicable reconciled QML/native registry on the final candidate, with exact per-suite commands, runner identity, exits and pass/fail/skip totals. Diagnose real failures without weakening assertions. Classify asset/environment-dependent limits rather than claiming a blanket pass.
- [ ] Exercise missing/unreadable/unsupported media, decoder failure, cancellation, rapid track changes, retry, end-of-queue, offline optional content and resizing. A controlled loopback source may test delayed/failing transport without a public streaming service.
- [ ] Exercise rapid open/back/close, pending-load teardown, profile changes and repeated app shutdown under normal GC. Capture owned-process diagnostics for reproducible native failures. A passing repetition campaign does not prove the historical intermittent 0xC0000409 root cause cured.
- [ ] Run shared-component regressions and verify unrelated worlds still open and function after the app/build wiring changes.
- [ ] Run the complete assembled-app acceptance journey below, retain source/artifact identity and produce the final reuse inventory, adapter contracts, known gaps and Windows receiving matrix.

**Gate evidence:** full foundation acceptance matrix and reproducible native Linux candidate; separate Windows receiving qualification; honest audio-device and original-art limitations. If a required functional criterion is blocked, report an incomplete 1.0 candidate rather than quietly redefine completion.

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

Start with Build 0.25 only: inspect the pinned sources, assemble the candidate and complete the real local playback vertical slice. Publish it for receiving review and stop at that gate. Continue to 0.5, 0.75 and 1.0 only after the preceding build is accepted. Report genuine dependency/build blockers with exact evidence; do not replace the requested mode with an unconnected harness and call the project complete.
