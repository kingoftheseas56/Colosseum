# Vinyl M0F receiving — 2026-10-20

## Status

**M0F accepted on the development branch:** native adoption built, focused
checks passed, and the actual app completed all three receiving phases with
exit 0. Main-branch landing and release qualification remain outstanding.

## Scope and ownership

M0F adopts the local listening foundation through `VinylBackendSession` in the
actual desktop app. M0 (`008aeb812`) and M0R (`93103589c`) remain its baseline.
The existing QML pages issue commands and consume native projections; they no
longer maintain a competing playback queue or create a separate player.

| Adopted behavior | Actual product owner / adapter |
|---|---|
| BA1 queue, BA2 playback/session/coordinator | One native `VinylBackendSession`; duplicate occurrences, move, shuffle/unshuffle, manual wrap and repeat-one preserved. |
| BA2-D work attribution | Existing `MpvItem`/mpv core, with actual playlist-entry IDs, load generations and tracked seek replies. Latest pending seeks serialize behind the physical seek. |
| Local music identity | Existing Vault reference, optional verified recording mapping, no invented MBID. Canonical-only consumers reject unmapped local references. |
| BA3 checkpoint | Existing profile Progress store, typed profile-scoped state plus compact presentation metadata; cold restore is paused and allocates no player. Existing QML v1/v2 checkpoints migrate through validation. |
| Personal state | Existing Collection save membership, preserving imported local catalogue records when unsaved. Existing profile preferences persist validated BE2 feedback bytes. Full BA5 playlist commands remain M4. |
| BA4 listening facts | Transient native qualification feeds the existing ActivityPlaybackTracker. The existing Activity store remains the only durable history writer; privacy, retention, reset and profile boundaries remain host-owned. |
| Source lifecycle | Existing local source request/cancel/completed port, generations, deadlines and shutdown fences. No fake catalogue or in-memory persistence adapter is used by the production facade. |

The facade is in a private static app library. Backend types remain behind its
private implementation, avoiding backend include paths leaking through the app.
Desktop qualification and synthetic-profile fixture flags remain OFF. Android
availability is unchanged.

## Repairs found during adoption

- Standalone artwork admission now rejects nil UUIDs consistently with BA0.
- Replacing a still-resolving occurrence clears the coordinator's pending source
  intent before activating the next occurrence. A failing regression preceded
  the fix.
- Local references round-trip through BA3 v3 without fabricated canonical IDs;
  foreign-profile and malformed restores reject without partial mutation.
- A real-app history failure exposed an overly broad `paused` NOTIFY signal.
  Every clock update was resetting QML's Activity baseline. The regression
  observed three false pause notifications; the dedicated `pausedChanged`
  signal emits only for actual pause transitions.
- Full-app heap corruption was independently traced by ASan to stale class
  layouts: one cached consumer allocated a 280-byte ProfilePreferencesStore
  while its current destructor expected 304 bytes. Cached dependency records
  referenced an old `T:` checkout. Compiler caching was disabled for the local
  receiving build, every affected header consumer and shared Vinyl library was
  rebuilt, and the generated app recipe relinked the coherent objects. No
  unrelated AnimeOrder source change or global cache setting was needed.

## Verification

Target: Windows x64, Qt 6.11.1, MSVC 19.44.35227 / tools 14.44.35207,
Windows SDK 10.0.26100.0, installed libmpv/MpvQt. Evidence is retained outside
the repository under `D:/Colosseum-Vinyl-M0F-20261020`.

- Repaired backend: **48/48 CTest harnesses**, none disabled or omitted;
  six inventory-guard cases also pass. M0R's frozen manifest is preserved;
  M0F changes are identified by a separate manifest.
- App native targets: **3/3 CTest targets** pass: facade, existing real
  MpvItem events/seeks, and profile feedback persistence.
- Final native-source QML bridge: **13 passed, 0 failed, 0 skipped**. It covers
  import identity decisions, cancellation, profile fences, native envelopes and
  queue preservation. Its Activity call-order stub is not the history proof.
- Actual app: **record / restore / empty**, all **exit 0**, under disposable
  profile `vinyl-m0f-7ef4193825e9439182409e76f5cf5e8e`. Import/save/unsave,
  same-occurrence replacement, duplicate occurrences, move/shuffle/unshuffle,
  repeat-one across a real EOF/new player, rapid serialized seeks and pause
  passed. The checkpoint restored three occurrences, repeat-one and position
  4 seconds, paused with no player. Explicit resume reached 4.6 seconds.
  Eleven seconds persisted through the existing Activity store; disabling
  history prevented further accumulation. Clearing survived another restart,
  while both Collection records, privacy preference and previous history
  survived. Every phase exited normally.
- Actual decoder/output observation: `PCM signed 16-bit little-endian`, one
  channel, `wasapi`; no null output driver. Human ear audibility is unclaimed.
- Qualified executable SHA256:
  `bb1281801bd4269dae600a7451e07a2ddd37cd0927cc2a43a305ac3b3eb0e5e5`.
  This was a coherent affected-object rebuild/relink of the ordinary Release
  app, not a fresh full-repository clean build. The QML manifest was
  `3333eff6eb87b5d57f54e1949c5df4ca782d593bde8da7ce1646a04337fe1cbc`.
- PowerShell runner parsed successfully. Final source diff was inspected.

Evidence files: `backend-ctest.log` / `.xml`, `app-ctest-final.log` /
`app-ctest.xml`, `bridge-tests-final.txt`, `pause-notify-red.txt`,
`pause-notify-green-build.log`, `bounded-coherent-build.log`,
`coherent-app-link.log`, `pause-notify-app-link.log`, and the actual run's
`result.json` and per-phase logs. Failure diagnostics and ASan binaries stay
outside source control. The accompanying M0F manifest records receiving hashes.

The repeatable driver is `tests/vinyl-live/ProductionNativeFlow.qml`, which
inherits the real `Main.qml` and runs in the normal `colosseum.exe` with the
production registrations, stores and player. Its PowerShell runner creates a
disposable profile tag and two real PCM WAV files, checks the executable hash
across all phases, requires exit 0 and explicit success markers, and restores
its environment. This is programmatic interaction with the actual app;
Windows picker clicks and human listening are separate checks.

## Remaining product work

- M1: tagged metadata, correct album grouping, ordinary picker/folder usability,
  missing-file/import feedback, and route/focus polish on the adopted foundation.
- M2: online catalogue and full composed Artist adapters.
- M3: torrent transport compatibility and YouTube fallback admission. The
  current host accepts whole files; bounded/CUE load options reject explicitly
  until a matching host adapter is implemented.
- M4 onward: complete personal/playlist operations, discovery, lyrics/context,
  output presentation and full visual/product qualification.
- Main-branch integration and release/installed-package qualification remain
  separate. Existing unrelated `universeHallLayer` startup warnings and optional
  image/network failures are outside this slice.

The complex native integration was parent-owned. Luna xhigh supporting identity,
player-boundary and receiving reviews were inspected. The final receiving review
reported no remaining actionable source findings. One optional CLI Luna review
failed in its launcher before dispatch (`Get-FileHash` unavailable in that child
PowerShell); it supplies no review evidence and was not retried. ZCode/GLM was
not used.
