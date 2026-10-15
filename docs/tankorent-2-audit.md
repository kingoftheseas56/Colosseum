# Tankorent 2.0: first music streaming slice

Source inspection and bounded runtime verification on 2026-10-14. Evidence below
distinguishes injected discovery, real peer transfer and public-provider playback.

## Current owners

| Concern | Existing owner and boundary |
| --- | --- |
| Music catalogue and metadata | `VinylProvider` / `scripts/vinyl/provider.py`; YouTube Music is the current primary catalogue adapter. Catalogue identity does not require YouTube to own playback. |
| Music library and resume | Existing profile `Collection` and `Progress` stores through `VinylLiveSession.qml`. Persist stable identity, queue and position; never resolved URLs. |
| Torrent search | `TankorentSearchService`, concurrent search handles and source settings; indexers are replaceable. Music has its own category/allowlist extension. |
| Exact music file admission | `MusicTorrentQueryPlanner` and `MusicTorrentMatcher`; no fuzzy title substitution. Unsupported CUE manifests and ambiguous matches fail closed. |
| Music acquisition coordination | `VinylTorrentProvider`; one cancellable lane, bounded discovery/metadata work, no database or new torrent engine. |
| Streaming transport | `StreamServer`; adopts an existing Stremio service or starts the packaged runtime. The HTTP service owns torrent playback bytes and serves the selected original manifest index. |
| Audio rendering | Existing `MpvItem` in `VinylLiveSession.qml`, with per-player generation checks. |
| Books/comics/manga downloads | Shared native libtorrent `TorrentEngine` and medium-specific downloaders/selectors. Music does not alter their priorities, remove their hashes or reuse their completion gates. |
| Video admission | `MediaAdmissionProbe` intentionally requires decoded video frames. It must not gate audio-only music. |

There is no live `StreamEngine` or `TorrentClient` implementation in this checkout;
references to them in `TorrentEngine` comments are historical, not integration APIs.

## Exact music path

`VinylTorrentProvider::resolve(QVariantMap track)` returns an integer request ID
before any completion. Track keys are `artist`, `album`, `title`, `trackNumber`,
`discNumber`, and `durationMs`. `completed(id, data, error)` returns a selected
`url`, `infoHash`, `fileIdx`, `filePath`, `provider`, and `qualityLabel`, or an error.
`cancel()` suppresses outstanding completion. A new resolve retires the previous
request. Constructors allow injected search and stream services for bounded tests.

The coordinator uses the music query planner, `startSearch("music", "all", query, 5)`,
and the exact matcher. It retains at most five search candidates, checks at most
three manifests, and has a default total deadline of 12 seconds. Search phases
receive up to three seconds; a candidate's metadata/URL phase receives up to six
seconds within that total. Cold DHT startup may exceed this budget.
The total timer always wins: phase budgets are upper limits, not additive waits.
The caller receives fallback eligibility at the acquisition deadline; QML's
separate 15-second torrent audio startup watchdog begins after URL delivery.
Therefore acquisition plus initial-audio failure can take about 27 seconds
before YouTube resolution begins; the 12-second acquisition bound is not a
12-second bound on all playback and fallback work.

`StreamServer` exposes request-scoped methods:

- `requestManifest(requestId, infoHash)` -> `torrentManifestReady(requestId, infoHash, files)`.
- Manifest rows preserve `fileIdx`, `name`, `path`, and `length`.
- `requestFile(requestId, infoHash, fileIdx)` -> `torrentStreamReady(requestId, url, infoHash, fileIdx)`.
- `torrentRequestError(requestId, message)` identifies failure; `cancelRequest(requestId)` retires only that caller's work.

Scoped music requests must not emit legacy `streamReady`, `fetchReady`, or global
playback errors: the existing video page consumes legacy `streamReady` without
request filtering. Cancellation must not remove shared torrent payloads or kill
an adopted service. Manifest registration and playable URL availability are
transport evidence only; they do not prove downloaded data or audio output.

`VinylLiveSession.qml` prefers torrent acquisition and owns explicit YouTube
fallback after acquisition or playback startup failure. It correlates request IDs
and mpv generations, cancels on profile retirement, and keeps browse selection
independent of the playing queue. The provider coordinator owns no YouTube logic.

## Verification boundaries and remaining gaps

`tests/vinyl-live/torrent_provider_contracts.cpp` uses injected search and stream
services to cover music routing, exact index selection, ambiguity, bounded
attempts, deadline failure, stale callbacks and cancellation. Matcher tests cover
deterministic identity rules. Such tests do not establish live indexer coverage,
runtime availability, swarm metadata latency, payload delivery or audio output.

The standalone `tests/vinyl-live` CMake project builds the integration support
library and focused harnesses with Qt Core/Network/Test; the live mpv harness also
requires Qt Quick and MpvQt. The production target includes the same coordinator,
matcher and query planner through `native/CMakeLists.txt`.

Runtime qualification must separately show real catalogue metadata, real torrent
manifest selection, the exact selected file's HTTP payload, advancing audio
playback, startup-failure fallback, rapid track replacement and profile isolation.
Installed-package runtime discovery and clean application/process exit remain
separate checks. Conservative path identity may reject valid releases whose
folders do not follow the supported Artist/Album/Track layout. Joined multi-artist
catalogue labels and unavailable manifest tags further limit initial matching.

## Verified first slice

- Full native application and the standalone live harness compiled with MSVC/Qt.
- Matcher/query harness passed. Provider contracts passed 8 checks, including
  unchanged books/audiobooks/comics routes and the existing videos/unknown route
  behavior, cancellation, ambiguous matches and bounded stalled acquisition.
- Stream contracts passed 3 checks: scoped manifest/index selection and cancellation,
  no music emission on legacy video/download callbacks, and working legacy
  `play`/`prefetch` callbacks. These are controlled HTTP contracts, not media playback.
- Existing StreamServer lifecycle regressions passed 5 checks, with 1 Linux-only
  skip: startup failure/retry, hung-child timeout/retry and dead-listener recovery
  preserving 12 queued video requests. Root test process exited 0. Real book/comic
  downloads and video decoding were not rerun; their engine implementations were
  unchanged, and routing plus shared transport contracts were the regression scope.
- QML session contracts passed preferred-source selection, fallback position/pause,
  queue identity, stale reply rejection and profile retirement. The existing
  search/library/profile/queue contracts passed again.
- Existing album/song/page-data QML regressions: 29 passed, 0 failed; 3 optional
  screenshot cases skipped. No new visual sign-off is claimed.
- A real libtorrent loopback seed served two generated 20-second stereo PCM WAVs.
  Only search results were injected. The real runtime received `.torrent` metainfo,
  discovered its local tracker peer and delivered approximately 7,077,888 bytes.
  The production coordinator selected original manifest indexes **0 and 2** (index
  1 is a padding file), and mpv decoded both tracks as signed 16-bit PCM.
- After advancing to the second track and seeking, shutdown saved 13.4244 seconds.
  A fresh process restored that track paused, then resolved it through the torrent
  path and advanced to 14.6984 seconds. Both processes exited 0. Resume reused the
  runtime's cached payload; the first run independently proved peer transfer.
- The actual `Main.qml` source-build smoke performed anonymous public album search,
  attempted real torrent discovery, found no admitted file and fell back to the
  exact YouTube video. mpv decoded stereo Opus at 2.2255 seconds; root process exit 0.
  This establishes production registration/fallback, not a public torrent match.

The first seed attempt exposed a test fixture failure (incoming handshakes were
rejected). The corrected fixture explicitly activates seeding, avoids advertising
itself to itself and allows multiple loopback peers. A direct handshake and the
subsequent peer-transfer run verified the fixture. Product deadlines were retained.

## Reproducing the bounded checks

Build `tests/vinyl-live` with Qt Quick, Network, Test and MpvQt. Its targets include
`music_torrent_matcher_harness`, `vinyl_torrent_provider_contracts`,
`vinyl_stream_contracts` and `vinyl_live_harness`. Run the live harness with a new
isolated state directory and scenario `contracts` or `torrent-contracts` for
deterministic session checks. Set `COLOSSEUM_VINYL_PYTHON` to a working interpreter.

Build `tests/music-torrent-seed` with matching libtorrent/Boost/OpenSSL dependencies.
Run `music_seed <disposable-payload-directory>`; it prints the magnet and hash and
serves a loopback tracker/peer for ten minutes. Set `VINYL_TEST_TORRENT_HASH` to that
hash, `VINYL_TEST_TORRENT_FILE` to the generated `music.torrent`, and
`COLOSSEUM_STREAM_SERVER` to the existing runtime directory. Run the live harness
with `torrent-play`, then `torrent-resume` using the same isolated state directory.
The harness refuses an unrelated service already listening on port 11470 and
starts an owned runtime with a separate application cache.

For this controlled seed, registration uses `/create` with metainfo bytes because
the runtime's peer-search setup does not obtain custom tracker sources from a
magnet string's `tr` parameter. Production hash-based discovery uses the runtime's
existing default peer sources. The test does not copy payload files into its cache.

## Still outside this delivery

Broad public music-indexer coverage; alternate naming/tag layouts and reliable
edition reconciliation; CUE offsets; gapless playback and next-track prefetch;
midstream stall/expiry recovery; explicit offline downloads; cross-platform and
installed-package qualification remain open. Cancellation retires requests and the
player, but does not delete shared engine hashes or payloads. A future shared
stream lease policy must govern retained torrents without disrupting other media.

This branch remains opt-in, unmerged and unreleased. It does not add Vinyl to the
1.1.9 release scope or change Claude's approved presentation direction.
