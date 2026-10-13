# Vinyl live listening slice

This implements anonymous YouTube Music album search, exact-release browsing,
audio playback, seek, album queue, saved albums, and paused restoration after
restart. Claude's existing album, song and transport components are reused.
The search/library wrapper is a development entry surface, not a replacement
design for the approved Home/Discover experience.

## Ownership and reference choices

- YouTube Music is the primary catalogue, not the only catalogue. Search copy
  stays provider-neutral; other catalogue adapters remain part of the product.
  MusicBrainz enrichment is optional and is not needed to open or play an album.
- Catalogue priority and playback priority are separate. Torrent streaming is
  the preferred playback source; YouTube is the secondary, inexpensive fallback.
  This first slice proves the YouTube fallback path only. Its current direct
  resolver does not implement the intended torrent-first source selection.
- `VinylProvider` is a replaceable, asynchronous process transport. Its two
  independently cancellable lanes are catalogue browsing and stream resolution.
- `scripts/vinyl/provider.py` uses pinned `ytmusicapi` and `yt-dlp` dependencies.
  No account, cookies, subscription, catalogue dump or cloud database is needed
  for the verified local-development path.
- IDs are UUIDv5 in namespace `9b7255c4-9aec-51d6-8905-e55de3fa8209`, named
  `youtube-music:<entity-kind>:<source-key>`. Release browse IDs and recording
  video IDs remain explicit. Future MBID reconciliation must retain these IDs
  as aliases rather than invalidate saved entries.
- Existing profile `ProgressStore` and `CollectionStore` own resume and saved
  albums. No new profile database or disk music cache is introduced. Resume
  stores versioned metadata, source IDs, queue order and position. Signed media
  URLs/headers are transient and are resolved again when playback starts.
- The existing `MpvItem` performs playback. A replaced player instance and
  request generation checks prevent old callbacks from advancing a new queue.
- Profile departure snapshots and clears playback; the incoming profile starts
  paused. Final checkpoint registration precedes the progress writer's shutdown.

References reviewed: [ReVanced](https://revanced.app/),
[InnerTune](https://github.com/z-huang/InnerTune),
[Metrolist](https://github.com/MetrolistGroup/Metrolist),
[ytmusicapi](https://ytmusicapi.readthedocs.io/en/stable/usage.html),
[yt-dlp](https://github.com/yt-dlp/yt-dlp).
The useful standalone-client pattern is to separate browse identities from
ephemeral playback resolution. No application code was copied from these clients.

## Run from a source checkout

Use Python 3.10+ and install the pinned requirements into an isolated environment:

```powershell
python -m venv <runtime-directory>
& <runtime-directory>/Scripts/python.exe -m pip install -r scripts/vinyl/requirements.txt
$env:COLOSSEUM_VINYL_PYTHON = '<runtime-directory>/Scripts/python.exe'
$env:COLOSSEUM_VINYL_LIVE = '1'
# For qualification, use a fresh tag and a distinct Lanista pipe.
$env:COLOSSEUM_APPDATA_TAG = 'vinyl-live-test'
$env:COLOSSEUM_LANISTA_PIPE = 'ColosseumVinylLiveTest'
```

Build the normal `colosseum_runtime_ready` target using the repository's native
build instructions. Launch from the source root with its matching QML manifest.
The shared world selector exposes Vinyl only with the opt-in environment flag.
The helper file must remain at `scripts/vinyl/provider.py` under the resource
root. Explicit test QML overrides use the launch working directory as that root.
This is not a packaged-runtime installation contract.

## Bounded verification

`python tests/vinyl-live/test_provider.py` checks typed/stable identities,
unavailable track preservation, incomplete album rejection and invalid resolver
input rejection without network access.

Configure `tests/vinyl-live` as a standalone CMake project against Qt Quick,
Qt Quick Test, MpvQt and libmpv. It builds:

- `vinyl_live_harness <isolated-state-dir> contracts`: deterministic fake
  transport checks latest-request wins, failures, cancelled late replies,
  profile separation, restore validation, saved albums, shuffle permutation and
  queue boundaries. It uses the real profile stores and live session adapter.
- `vinyl_live_harness <isolated-state-dir> play`: live search for
  Idhayathai Thirudathe, selection of `MPREb_IsOnA4Gotve`, seven-track identity
  check, playback of `InEXiAJCOuA`, seek, and quit without an explicit test save.
- `vinyl_live_harness <same-state-dir> resume`: starts a new process, checks the
  saved source identity and paused position, resolves a fresh stream and verifies
  that playback advances beyond that point.
- `vinyl_live_harness <isolated-state-dir> interactive`: opens the development UI.
- `vinyl_qml_regression -input tests/qml/tst_vinyl_album_page.qml` (also song and
  page-data suites): runs the existing prototype regression tests with a bounded
  runner, without loading unrelated native tracker infrastructure.

`tests/vinyl-live/ProductionSmoke.qml` is an explicit QML override inheriting the
real `Main.qml`. Run it with a **fresh app-data tag**, from the source root, using
the newly built Colosseum executable. It verifies native registration, world
loader wiring, live catalogue and actual mpv decoding, and exits automatically.
Always check the process exit code, not only the success marker.

## Verified on 2026-10-14

- Full native Colosseum build and bounded native harness build passed.
- Anonymous search returned the selected release; album browsing preserved all
  seven track IDs. The native player decoded stereo Opus (~138 kbps reported by
  the resolver), sought past 12 seconds, and exited cleanly.
- Fresh-process resume restored `InEXiAJCOuA` at 13.4756 seconds and advanced to
  14.5004 seconds, with clean exit. The final checkpoint used normal shutdown.
- The real Main.qml smoke run logged `VINYL_PRODUCTION_OK`, reported stereo Opus
  at 2.2054 seconds, and exited with code 0. This is source-build runtime evidence,
  not an installer or release claim.
- Deterministic contract checks passed. The four Python provider tests passed.
- Existing album, song and page-data QML suites passed: 29 checks, zero failures,
  three optional capture cases skipped. The pre-existing shared runner stalled
  before producing results; the bounded runner compiled here completed all three.

## Remaining boundaries

No hosted resolver, cross-network URL validity, expiry recovery during playback,
relay, mobile/background service, installer dependency bundle, Google sync,
MusicBrainz/Wikidata enrichment, listening-history/recommendation ingestion,
lossless lane, lyrics, annotations or video mode is implemented by this slice.
Provider unavailability stops with an explicit retry action; it does not substitute
a similarly named upload or loop through failing tracks. Album queues stop at
the end. The provider is unofficial and can require adapter updates.

The infrastructure chat owns D1/API delivery and optional MusicBrainz enrichment.
This local helper is a development adapter, not a decision to bypass the eventual
service architecture. The opt-in remains until packaging, shared service
contracts, and the complete approved world UI are qualified. Render captures
from this execution environment were not usable for visual sign-off; no new
pixel-level fidelity claim is made.
