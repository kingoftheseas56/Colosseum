# Vinyl M1 receiving — tagged local library

**Status:** implementation and automated actual-app receiving accepted on `codex/vinyl-production-flow`. Physical Windows picker selection remains unverified. Main integration and release remain separate.

## Delivered

- Read actual music tags and duration through the existing shipped libmpv. Import inspection runs on a cancellable worker, with bounded folder traversal, progress and profile/request fences. No production ffmpeg or new helper executable is required.
- Group local tracks by tagged release information and album folder, order by disc/track, and normalize matching tagged CD/Disc suffixes. Preserve raw album tags and Vault file identities. Tagless files retain the filename fallback without fabricated external identifiers.
- Merge repeat and partial imports without losing other tracks. Migrate prior automatic single-track records through existing Collection ownership; explicit valid manifests take precedence. Changed-file identity decisions remain explicit.
- Reuse existing Collection, Session, Progress, activity and mpv owners. No parallel player, library database or history store was introduced.
- Add folder import, progress, cancellation and track/album feedback to the normal Vinyl world. Replace the album hero's failing cover mask with the existing RoundedPosterImage renderer.
- Extract bounded embedded FLAC front artwork into the existing artwork cache; support conventional local cover sidecars. Embedded artwork in other containers is not qualified by this slice.

## Build and focused checks

Base: `1f0be042ac04293c80f359c194edcaa90f0cfc58` (M0F).

Actual app: `C:\b\vinyl-production-build\colosseum.exe`, built from this worktree with both Vinyl qualification and fixture flags **OFF**, no Vinyl enable environment override.

- EXE SHA256: `fdab4c343d873d8156c55e1f571bc0e7997af182a2b412984d4017c4e909131f`
- Final QML tree SHA256: `ba2e65a718fe2bf5bae98f6d70a99a958862b594f6915937db70612a7a1ccbd4`
- Native affected consumers and final link succeeded. Evidence: `app-configure.log`, `snapshot-final-build.log`, `snapshot-final-link.log` under the evidence root below. Build concurrency was bounded to two jobs.
- **7/7 CTest targets passed**, zero failures: local source, tagged import, native session bridge, full native world QML, native facade, actual mpv events and feedback snapshot. Tagged import has 16 QtTest cases including setup/cleanup; full world has 36. See `qualified-ctest.log` and `qualified-ctest.xml`.
- Native bridge repeated **3/3** successfully (`bridge-repeat-final.log`).
- Both PowerShell runner files parsed successfully; final diff whitespace check passed.

Fixtures cover tagged/tagless and multi-disc ordering, compilation grouping, artwork, partial/repeat import, cancellation/profile boundaries, manifest precedence/migration and changed-file handling. Synthetic test audio uses a test-only configurable ffmpeg path. Earlier failed test receipts remain retained; `qualified-ctest` supersedes them. The earlier M0R backend suite was not rerun wholesale in M1.

## Actual Colosseum app evidence

Evidence root: `D:\Colosseum-Vinyl-M1-20261020` (local artifacts, not committed).

### Hemanth's Illmatic folder

Run: `vinyl-m1-e7054b6d26fb49f1899a90c9adb8f71c`.

`TaggedLibraryFlow.qml` inherits the actual Main.qml and uses the production Vinyl world, native source, player and stores. It invokes the production world import command programmatically; it does not substitute a standalone preview or synthetic album.

- Folder has **10 FLAC files from CD1**, despite the parent folder's two-disc name. Tags declare disc 1/2. The imported library correctly has one present disc, ten tracks, and retains the declared total without fabricating CD2.
- Raw album tag `Illmatic XX CD 1` becomes **Illmatic XX**, artist **Nas**, year **1994**, ten ordered tracks and **39:48** duration. Artwork renders in both the album hero and player bar; final `album.png` was visually inspected.
- Partial one-track reimport preserves all ten tracks and existing Vault IDs without duplicates.
- Library, Album, Song, Artist and Back routes, search and empty-state filtering exercised while playing.
- Actual FLAC decode and WASAPI output observed. Ten queue occurrences; **14 seconds of qualified listening history** persisted.
- Cold restore retained the library, queue, saved position and history while paused with no active player. Explicit resume advanced playback.
- A separate disposable profile had an empty library/queue/player.
- Record, restore and isolated phases all exited normally with code **0**; markers and hashes are in `result.json`, with per-phase logs/process identities and screenshots.

### Existing native playback regression

Run: `vinyl-m0f-98493cd6d4da4e2b8ce38c8486d7ae29`, same final app/QML.

- Existing production-native runner passed collection save/unsave, same-occurrence replacement, queue duplicate/move/shuffle behavior, repeat-one at real EOF, serialized seeks and qualified history bounds.
- Cold restore/resume, history privacy disable, clear and empty restart passed.
- PCM decoding/WASAPI evidence and all success markers recorded; record, restore and empty phases all exited **0**.

The real-app runners now expose their windows and wait for visibility before driving playback. History assertions wait for actual persisted qualified listening rather than assuming media position alone proves it. Prior intermediate history/window failures remain in the evidence root and are superseded by the final passing runs.

## Limits and remaining gate

- **Windows picker interaction is unverified.** FileDialog/FolderDialog are wired to the production import path, but selecting files/folders in the OS dialog was not automated: computer-use initialization failed with missing kernel assets; BDC enumeration timed out, and the bounded fallback does not support folder selection. No picker success is claimed.
- Decoder/output observations do not establish human ear audibility. This is Windows source/build receiving, not installed-package or cross-platform qualification.
- All supported extensions were not individually exercised. FLAC embedded artwork is implemented; other containers use sidecars for artwork in this slice.
- Grouping is local and deterministic; online canonical edition matching and catalogue/Artist adapters remain M2. Torrent and YouTube transport remain later milestones.
- Source files, receiving report and manifest are committed separately from local audio, screenshots, profiles, logs, caches and build output. Commit/push status is recorded in Git, not inferred from test success. No main merge or release occurred as part of M1.
