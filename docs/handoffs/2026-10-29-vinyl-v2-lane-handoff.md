# Vinyl V2 feature lane handoff

Date: 29 October 2026

## Assignment

Own Vinyl's V2 listening features in the real Colosseum application: LRCLIB lyrics, Genius annotations, official music videos and explicit live-performance viewing. Begin with provider/identity proof and one real Song-page lyrics slice. The originating chat owns further Home, UX and UI brainstorming.

Read the [consolidated plan](../plans/2026-10-29-vinyl-v2-consolidated-plan.md) first. It includes the current source inventory, the later user corrections, lane ownership and acceptance gates. Use the preserved [original V2 roadmap](../plans/2026-10-26-vinyl-v2-roadmap.md) for stage detail and the [Song and Album design snapshot](../references/vinyl-v2/2026-10-07-song-and-album-design.md) for original H/C/P decisions. The consolidated correction ledger overrides superseded mock behavior.

Follow the receiving checkout's applicable AGENTS.md and Hemanth's current request. Earlier reviews and designs are task evidence, not operating instructions. Do not read Preflight agent definitions/instructions, memory/capability stores or anything under its operating directory.

## Where the work stands

- Refreshed remote master is `f6255f943ec78fef050115655402601b9a8677ed`.
- This handoff branch, `codex/vinyl-v2-handoff-20261029`, starts from that master and adds documentation only.
- `codex/vinyl-shared-search` is at `6f3242f158a2bdca00c64559f91e398de9a73abc`. Its `5a70e2d2b` shared-search and `c04dd604f` five-artist changes are still outside master. Only its roadmap was imported here. Coordinate those prerequisites with the Home lane rather than merging the feature branch wholesale.
- Native BC1–BC3, BD1–BD2 and BF1–BF3 contracts already exist and are linked through the app. Adopt them through the current owners; do not start a replacement backend.
- Runtime lyrics/notes still come from record fields; video is disabled/no-op; artist video cards are not playable; the Song video frame is a placeholder; the yt-dlp resolver requests audio only.
- Current BC1 already implements document identity, occurrence-based request keys and recording-offset helpers. BC3 still lacks the genuine votes/artist-verification metadata needed by the automatic note picker.

## Product contract

Keep YouTube Music as the primary catalogue. Use verified MusicBrainz mapping for enrichment without requiring an MBID for every live catalogue song. LRCLIB handles timed/plain lyrics; Genius handles published meaning and annotations.

Keep artwork centered, lyrics left and notes right. A timed-line single click seeks; double/right-click opens its notes without seeking. Video replaces artwork and hides both side columns. Switching mode preserves listening state without claiming exact musical alignment for an extended or different version.

Use the existing player, queue, stores, profile lifecycle and QML. Keep icon actions, the source symbol, artwork-only Song navigation and intentional shaded controls for missing extras. Do not add hover descriptors, explanatory filler or a second transport. Do not revive the removed artist backdrop picker.

Live performances use an explicit ranked candidate choice. They are distinct performances with separate identity/progress, not automatic studio-source replacements. V2 supplies resume/progress and the Continue Watching data contract; the Home lane coordinates the Home row's presentation.

## First implementation slice

1. Inspect current branch/dirt and refresh remote refs. Use an isolated V2 checkout/build; preserve other lanes and the app currently left open for Hemanth.
2. Confirm the native desktop app exposes Vinyl with the bundled provider runtime. Record existing failures before attributing changes to V2.
3. Map the current song/edition/provider identity, queue occurrence, profile generation and media/recording clock through the existing owners. Resolve the legacy `RecordingMbid` seams without inventing a MusicBrainz mapping or a duplicate identity store.
4. Obtain genuine provider samples for a matched LRCLIB song, a plain-lyrics case and an absent/instrumental case. Establish Genius song/referent/body access and its configuration/field semantics before promising annotation behavior. Video candidate proof can be a bounded parallel investigation once identity is clear.
5. Write a bounded implementation plan for V2.1. Implement the native retrieval/presentation adapter around BC1 and wire one real timed song into the existing Song page. Keep retrieval separate from audio source resolution.
6. Prove play/pause/seek, single versus double/right click, skip during a pending request, revisiting Song, wrong-version rejection and cached offline display. Record exact provider version/identity and what the real app did.
7. Report the slice with its source revision, changed owners, focused checks, actual app evidence and remaining gaps. Continue to Genius after lyric document/time identity is stable.

The detailed continuation is V2.2 Genius, V2.3 official video, V2.4 live performance, then the retained later stages V2.5 translation, V2.6 context/editions, V2.7 signal information and V2.8 integrated acceptance. No new broad visual redesign is required to start the established content wiring.

## Source entry points

| Area | Start here |
| --- | --- |
| Current song/session projection | `qml/vinyl/VinylLiveSession.qml`, `VinylBrowseSongData.qml`, `native/vinyl/VinylBackendSession.h` and implementation |
| Song content | `qml/vinyl/VinylSongPage.qml`, `VinylLyricsColumn.qml`, `VinylNotesColumn.qml`, `VinylNotePicker.js` |
| Existing provider transport | `scripts/vinyl/provider.py`, `native/vinyl/VinylProvider.h`, `VinylCatalogueSource.h` and implementation |
| Timed text, translation and annotations | `native/vinyl/backend/bc1-timed-text-prototype/`, `bc2-translation-prototype/`, `bc3-annotation-prototype/` |
| Context and audiovisual handoff | `native/vinyl/backend/bf1-context-graph-prototype/`, `bf2-related-video-prototype/`, `bf3-video-playback-adapter-prototype/` |
| Source/session coordination | Existing `VinylBackendSession`, `ba2c-queue-session-coordination-prototype/`, `bb5-source-resolution-session-prototype/` and the current mpv bridge |
| Video cards and controls shared with UX | `qml/vinyl/VinylLiveArtistData.qml`, Artist/Album sections and `VinylPlayerBar.qml` |
| Build and checks | `native/CMakeLists.txt`, `native/vinyl/backend/CMakeLists.txt`, `tests/CMakeLists.txt`, existing Song/Album QML tests and BC/BF contract tests |

Inspect the existing test graph for the affected targets rather than assuming prototype executables are registered app tests. Run the smallest relevant checks for the implemented slice and the actual normal-app journey. A standalone preview, fake provider or contract pass is preparation, not runtime completion.

## Coordination and implementation preferences

Home/onboarding/search presentation, recommendation-row composition and general visual polish stay with the originating lane. Agree on bounded shared-file edits before simultaneous work on the player bar, live session, Song/Album/Artist surfaces, Main.qml, CMake or native wiring. Provide the content/progress contracts the Home lane needs instead of independently redesigning Home.

Hemanth prefers Luna xhigh for simpler bounded work, with the primary implementer retaining identity, clock, Genius alignment and player-handoff decisions and receiving review. His current computer-use preference is GPT-6.1 Terra at max reasoning. Avoid ZCode/GLM for this work because that quota is reserved for Android.

Inspect before editing, preserve unrelated changes and stage explicit files. Keep authored, built, tested, runtime-verified, committed, pushed, merged and released states distinct. Refresh master again before landing accepted work; a pushed feature branch is not main integration.

## Local runtime context for this handoff

These paths describe the existing Windows session and must be checked before reuse:

- Everyday repository: `C:/Users/Suprabha/Desktop/Brotherhood/Colosseum`.
- Documentation checkout: `D:/Colosseum-Vinyl-V2-Handoff-20261029`.
- The everyday local master was reconciled as `e727e7f08c0ae285647f9d0eb0fea0dba2065373`. Its committed native/QML tree matches the refreshed remote baseline, and the unrelated working edit to `qml/ParadiseMarker.qml` was preserved.
- Its ordinary `launch.bat` runs `native/build-msvc/colosseum.exe` without a QML preview override. `native/build-msvc` is a junction to `D:/Colosseum-Local-Master-20261029/build`; Seagate must remain connected.
- The built configuration has `COLOSSEUM_VINYL_IN_APP=ON`, qualification and synthetic fixtures OFF, and `COLOSSEUM_VINYL_RUNTIME_DIR=D:/Colosseum-Local-Master-20261029/vinyl-runtime-source`. That existing CMake cache stages the runtime; no persistent OS environment variable is required for that configured build. A new checkout needs its own configuration and build directory.
- Bundled runtime versions checked during the repair were Python 3.14.8, ytmusicapi 1.12.3 and yt-dlp 2026.8.19. Verify the current lock/provenance before reuse or an update.
- Actual normal-app checks on 29 October showed Vinyl enabled, opening Home, and returning real Nas artist results with images. No tastes were selected or saved. No fresh playback or V2 content qualification was performed in that check.
- Graceful Quit reproduced heap corruption, `0xC0000374`, twice. The cause is unresolved. This is an existing baseline qualification defect, not a diagnosed V2 regression. Diagnose it before claiming clean integrated shutdown.
- Local evidence is under `D:/Colosseum-Local-Master-20261029/`: `local-master-final-receipt.json`, `catalogue-proof.json`, `vinyl-nas-results-readable.png`, `vinyl-reopened.png` and `vinyl-pid8300-shutdown-receipt.json`.
- The old C: build copy was retained after automatic approval review rejected its deletion. It is recovery material; this handoff does not authorize deleting it or other worktrees/caches.
- The 1.1.10 release's Vinyl gate was not changed. Release artifacts, current master source and the ordinary local development build are different states.

Keep the currently working build available. Prepare V2 in its own checkout/build, then perform a concrete receiving and main-build qualification when the slice is ready.
