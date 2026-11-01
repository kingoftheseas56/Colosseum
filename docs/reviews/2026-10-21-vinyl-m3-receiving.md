# M3 individual-file remote playback receiving

Date: 2026-10-21. Base: `f3674c0f3`, branch `codex/vinyl-production-flow`.

## Accepted scope

The ordinary Colosseum Main.qml app now uses Tankorent's music sources, the current shared StreamServer and the existing native VinylBackendSession/MpvItem. Local files remain direct; remote recordings prefer an admitted torrent file and use the existing YouTube resolver on no-match, acquisition failure or bounded runtime failure. No custom search API, replacement player or parallel canonical store was added.

The existing bundled Stremio server.js supplies torrent metadata and HTTP file transport. Music identity and candidate admission remain Vinyl responsibilities. Scoped cancellation retires request metadata/network work, and does not remove shared engines or persistent runtime cache that other media or an adopted service may own.

## Build and focused tests

- Windows Qt 6.11.1/MSVC Release build, qualification and test-fixture flags OFF.
- EXE SHA256: `653f1055963e2f58f60f8b859fb3b7107f3439951a0603d47f568387b28a213a`.
- QML tree SHA256: `e82cb68b281f7c832adf4718762571d6f0b6ed26941a966891034e1856b0858e`.
- All six focused CTest targets passed: music matcher/query, scoped StreamServer, torrent provider, catalogue source, native session bridge and world UI. Native session: 21 cases; world: 44 cases.
- The new real native decoder cases cover dead torrent transport, pause/occurrence preservation, no fallback loop, matching tags and contradictory title/artist/album tags. These deterministic source replies are not presented as public YouTube evidence.
- Scoped contracts additionally check invalid hash/manifest identity/path/file selection, cancellation/token reuse, preserved magnet trackers and absence of legacy global playback/fetch signal leaks.

## Actual app journeys

Evidence root: `D:\Colosseum-Vinyl-M3-20261021`.
Run: `vinyl-remote-8da537fa40314bf288b67be263affff6`.

`tests/vinyl-live/RemotePlaybackFlow.qml` inherits the ordinary Main.qml and installs no fixture catalogue, transport or player. The PowerShell runner pins the executable/QML manifest, uses one disposable profile, checks actual audio decoder/device properties and keeps signed URLs out of its selection receipts.

### Tankorent

- Catalogue: Nas, Illmatic, N.Y. State of Mind; canonical track `1af8a186-a488-5b2e-954d-0b8dfaee54c0`.
- Public discovery selected hash `d9ac58802d1f431d88a79884a20f7e20fd51580b`, file index 1, `Nas - Illmatic (1994) [MP3 320] 88/02. N.Y. State Of Mind.mp3`.
- Actual audio tags: title N.Y. State Of Mind, album Illmatic, artist Nas. MP3 stereo decoded to `wasapi`; playback clock advanced.
- Seek to 12 seconds, pause/resume and source/occurrence continuity passed. A further seek near the actual duration reached natural EOF and advanced to canonical track 3.
- Profile retirement cleared player/queue/provenance. App PID 12076 exited 0; its owned runtime PID 35360 was confirmed gone.

### YouTube fallback

- Catalogue: Eminem, STANS (The Official Soundtrack), Stan (feat. Dido); canonical track `cd87bdcb-108b-5bc5-86e5-13daa63513ce`.
- Tankorent returned `No exact music torrent file was found.` The same occurrence used the selected catalogue video `gOMhN-hfMtY` through the real helper and Google video transport.
- Opus stereo decoded to `wasapi`; playback clock advanced. Seek, pause/resume and identity continuity passed. Natural EOF advanced to canonical track 2.
- Profile retirement passed. App PID 39380 exited 0; its owned runtime PID 45924 was confirmed gone.

The torrent journey asserts torrent source continuity after seeking, so an unexpected YouTube recovery cannot satisfy its success condition. Both playback screenshots were visually inspected and show the ordinary Album UI, actual artwork and existing playback bar.

### Open review app

The third phase repeated the Nas torrent decode/seek/pause/resume checks, then stopped its driver and left the normal app responsive, paused on Illmatic near 13 seconds. PID 43232, executable `C:\b\vinyl-production-build\colosseum.exe`, start ticks `639281331541514513`. Its process receipt and visually inspected `review-playback.png` are under the run directory. Another unrelated Colosseum process was preserved.

## Receiving fixes

Fresh read-only source review identified search summaries erasing queued release context, missing fallback on failed seeks, ineffective metadata access, the queue artist field shape, stale provenance and an overly permissive journey success check. These were corrected and covered by focused checks. Review found no further concrete source issues after the fixes; actual runtime acceptance is the root-owned evidence above.

Runtime tests also exposed Qt 6 generic invocation argument mixing, a remaining read-only QML assignment, and remote browse context accidentally entering local pagination options. Their failed receipts were retained; corrected native/local and remote cases passed before this app run.

## Limits

- This closes the requested individual-file torrent and separately demonstrated real YouTube playback gate. Whole-album images requiring CUE offsets remain unsupported and safely fall back; authored CUE/span integration is still outstanding in the roadmap.
- Public expiry/retry stress, exhaustive provider availability, installed packaging, main-branch landing and release are not established by these two representative journeys.
- The decoder and Windows output device were witnessed; this is not a human-ear audibility claim. A pre-existing `universeHallLayer` startup warning remains outside this slice.
- Runtime payloads, profiles, downloaded cache, logs and screenshots remain local. Exact raw source/evidence hashes are in the adjacent M3 source manifest.
