# W02 — Tankoban 2 native-stream regression corpus

This file turns the failed Tankoban 2 C++/libtorrent stream path into mandatory Aqueduct gates. A future C++ implementation does not get credit for matching endpoint shapes while reintroducing these failures.

Historical sources: Brotherhood `TB2-project.md`, the 2026-04-23 Stremio-tuning A/B audit, the 2026-04-19 stream-stall work, and the 2026-04-24 stream-server pivot evidence.

## R01 — Invincible catastrophic cold-open

**Historical failure:** Invincible S01E01 Torrentio 1080p, info hash `ae017c71ae078a5ff68f9e545523cfb12922372b`, left the native engine at `HOLY_GRAIL=0` after roughly 4.5 minutes. The same source through Stremio gave ffprobe a usable stream in 0.26 s and mpv decoder-ready around T+0.1 s.

**Future test:** cold-cache open against the same hash if it is still live; otherwise run the exact test shape against the fixed legal Sintel/BBB torrents and retain the historical receipt as a non-reproducible named case.

**GREEN:** never regress into minutes-long pre-first-byte waiting while the oracle opens the same swarm promptly.

## R02 — bandwidth without head-piece progress

**Historical failure:** one cold-open reached roughly 193 peers and 15 MB/s over 118.9 s while the requested startup gate remained `0/1048576`; no first head piece completed.

**Future test:** record total download rate and per-requested-piece completion concurrently during cold open.

**GREEN:** bulk torrent throughput cannot starve the byte range currently blocking the HTTP consumer.

## R03 — reconnect loop / premature stream end

**Historical failure:** the final native-engine smoke before the pivot spent about 4.5 minutes black and produced 12 ffmpeg reconnect loops around byte 5.79 MiB. The Stremio pivot sustained playback with zero reconnect loops on the same source.

**Future test:** read a progressive range continuously while requested pieces arrive slowly; the HTTP connection must wait for data rather than ending the stream early.

**GREEN:** zero premature EOF/reconnect churn caused by temporarily unavailable torrent pieces.

## R04 — scheduler tuning sensitivity

**Historical result:** the 3x3 A/B of Stremio-derived session tuning moved stall rate 9.28→3.25 per 10 min, cold-open 28.7→3.0 s, and p99 wait 38.5→5.3 s. The tuned parameters included strict end-game mode, partial-piece preference, shorter piece/request/peer timeouts, more upload slots and faster connection attempts.

**Future test:** the native server's session profile is snapshot-tested and its cold-open/stall metrics are compared against the Stremio oracle under the same swarm.

**GREEN:** Aqueduct may use libtorrent-native equivalents, but it must preserve the observed streaming behavior rather than silently falling back to generic download defaults.

## R05 — seek-class and tail-metadata preservation

**Historical lesson:** the rebuilt native path needed separate InitialPlayback, UserScrub, ContainerMetadata and Sequential behavior. User scrubs clear and rebuild urgent work; container-metadata reads preserve the startup/head work instead of destroying it.

**Future test:** deep seek, repeated seeks, and tail metadata probe while the head window is active; inspect priority/deadline state before and after each transition.

**GREEN:** seek reprioritization is bounded, stale urgent work is removed when appropriate, and container metadata does not accidentally erase still-needed head work.

## R06 — time-critical queue overload

**Historical lesson:** the old reconstruction could create far more urgent deadlines than libtorrent's effective time-critical queue could service. The later code explicitly capped reasserted deadlines so dozens of nominally urgent pieces did not compete with the actual read head.

**Future test:** instrument the number of active urgent/deadline pieces during cold open and sequential playback.

**GREEN:** the currently blocking FileStream window remains the scheduler's dominant work; urgency does not diffuse across a giant range.

## R07 — metadata/readiness chicken-and-egg

**Historical failure:** the first Stremio integration waited for downloaded bytes before exposing `readyToStart`, but Stremio does not download stream pieces until an HTTP consumer opens the stream. No consumer meant no bytes; no bytes meant no consumer.

**Future test:** create by info hash with no metadata cache, then issue the media request immediately.

**GREEN:** metadata resolution gates file lookup, but byte-count readiness never prevents the first HTTP consumer from creating demand.

## R08 — competing HTTP readers / audio starvation

**Historical failure:** separate audio and video HTTP sessions let video demand dominate; first audio output arrived around 122 s. A 16 MiB audio prefetch ring reduced the same case to about 4.4 s.

**Future test:** two simultaneous ranges against different regions of the same media file, followed by real player A/V open.

**GREEN:** one active stream consumer cannot starve another indefinitely; real playback reaches both audio and video promptly.

## R09 — low-connected-peer tail

**Oracle stress evidence:** the 2026-08-07 Stremio battery forced a five-connection cap. Known peers remained healthy while cold open worsened to 35.92 s, versus a 4.19 s Sintel median under production settings.

**Future test:** deterministic peer-cap test plus a real sparse-swarm test when available.

**GREEN:** Aqueduct reports the discovered→connected funnel and does not mistake a healthy catalog/seeder count for usable streaming capacity.

## R10 — lifecycle cleanup

**Historical failure:** the Stremio subprocess pivot initially leaked `stremio-runtime.exe` on force-stop paths. Aqueduct's final architecture removes that child process, but the resource-lifetime bug class still matters.

**Future test:** repeatedly open, close, cancel during metadata, cancel during range wait, and shut down the application.

**GREEN:** no listener, torrent session, pending reader, worker thread, cache lock or socket survives its owning lifecycle.

## Reproduction status at Wave 0

| Case | Wave-0 reproduction status | Future owner |
|---|---|---|
| R01 | historical exact hash retained; live availability not assumed | W40 |
| R02 | historical telemetry retained; legal-swarm shape reproducible | W16-W20, W40 |
| R03 | historical exact reconnect count retained; progressive wait shape reproducible | W20-W22 |
| R04 | exact A/B parameters and metrics retained | W15-W19, W40 |
| R05 | deterministic scheduler-state testable | W16-W20 |
| R06 | deterministic scheduler-state testable | W17-W19 |
| R07 | deterministic metadata-race testable plus live oracle capture | W07, W20-W22 |
| R08 | deterministic two-reader shape plus real-player smoke | W20, W40 |
| R09 | deterministic peer-cap shape plus live sparse-swarm evidence | W14-W19, W40 |
| R10 | deterministic lifecycle stress | W03, W07-W08, W37-W40 |
