# Vinyl torrent startup and seek reliability follow-up

Date: 2026-10-24. Branch: `codex/vinyl-production-flow`. Starting commit: `a646ce582dc9954d65b423a804b942d2da7f56ec`.

## Result

The reproduced discovery, listener recovery and corrupt-peer failures are diagnosed and repaired. The final production app passed exact torrent playback and repeated seeking on two recordings, and real YouTube fallback on a third. This supersedes the unexplained intermittency recorded in [the earlier receiving record](2026-10-24-vinyl-remote-playback.md), within the scenarios below. Public swarm availability and a universal startup SLA are not established by these samples.

## Causes and repairs

1. **Discovery discarded late results and serialized slow candidates.** A fresh baseline canceled collection at 1,018 ms, waited until 3,523 ms for its first dead manifest, and reached the four-second deadline before finding the healthy FLAC release. Collection now remains open when useful work exists. At most two manifests race, with a one-second head start for the first candidate; stronger results may race immediately. Remaining planned queries can run while a manifest is pending. The existing four-second total deadline, three-attempt bound, exact-file matching and cancellation fences remain authoritative.
2. **Scoped requests did not recover a dead listener.** A refused `/create` now uses the existing shared server recovery path. A validated scoped manifest resets the consecutive recovery budget, including when best-effort `/settings` fails. RED controls reproduced both faults before their fixes; canceled tokens cannot be resurrected by recovery.
3. **A corrupt supplier was repeatedly poisoning pieces.** In `race-genesis-01`, HTTP 206 headers were returned but the audio response delivered zero bytes for 14,987 ms: piece 0 stayed unavailable despite active peers and about 19.9 MB received. `peer-genesis-03` decoded the correct FLAC but later stalled on missing seek bytes, with 68 hash failures. A complete failed piece was supplied entirely by one peer, while another peer supplied a valid piece. Disconnecting the proven supplier in a diagnostic control reduced failures to three and completed the same strict torrent journey.

`StreamRuntimeIntegrity.h` applies a narrow patch to the recognized engine in the **owned cached child script**. It records the actual suppliers of accepted blocks on each PieceBuffer. Only a failed whole verification group with one supplier permits disconnection and reconnect refusal. Mixed suppliers and multi-subpiece verification groups cannot identify a culprit and are left unattributed. Existing hashing is preserved. Attribution retains at most two suppliers; the per-engine refusal map retains at most 256 endpoints. Beyond that map capacity, a proven corrupt supplier is still disconnected but its endpoint is not retained.

The patch also deduplicates repeated whole-file prewarm selections during reopened HTTP ranges. All required source anchors are checked before any rewrite; an unknown bundle is left untouched with a native warning. External service installations are never edited. An already running adopted service does not receive this owned-child patch.

## Verification

Final app SHA256: `8E7548E251ABD4B3137B963E6412176E95429CB5A9C0BC871BC64428E87838DD`.

QML tree identifier: `d0f490e262d356318f63ac1e03420c5f58d3f5884f1ee318f87c7ccc57ae7cc2`.

Windows Qt 6.11.1/MSVC app build passed. Six focused CTest suites passed in 56.35 seconds: scoped stream, provider, runtime integrity, native session bridge, native player seek and playback queue. The integrity suite contains five Node behavior tests. Coverage includes delayed candidates, later-query racing, two-token supersession, loser callbacks, attempt limits, listener recovery, independent subsequent outages, atomic/idempotent patching, supplier attribution, duplicate callbacks, mixed/virtual groups, reconnect refusal and memory bounds. Luna xhigh supplied bounded test work and receiving review; the parent built and qualified the app. No full product suite was run.

Evidence root: `D:/Colosseum-Vinyl-Reliability-20261024`. `final-focused.log`, `final-LastTest.log`, `qualification.json`, per-run process/hash receipts, screenshots and retained failure traces are external to Git.

| Actual Main app journey | Result |
| --- | --- |
| `production-genesis-05` | Nas / Illmatic / **The Genesis**, hash `fec9baa6f4920edf4c25d47cff112d57a6253f08`, file 0. Matching decoded title/artist/album, stereo FLAC and WASAPI. Loaded 5,195 ms; advancing 7,323 ms. Seek 12, pause/resume, **12 paused tail/back cycles**, natural EOF, queue advance and profile retirement passed while remaining on torrent. |
| `production-ny-06` | **N.Y. State of Mind**, same release, file 1. Matching decoded tags, stereo FLAC/WASAPI. Loaded 4,418 ms; advancing 6,567 ms. Seek 12, pause/resume, **12 playing tail/back cycles**, natural EOF, queue advance and retirement passed on torrent. |
| `production-youtube-07` | **Stan (feat. Dido)**. Actual torrent discovery timeout led to exact catalogue video `gOMhN-hfMtY`, stereo Opus/WASAPI. Loaded 4,536 ms; advancing 6,734 ms. Same canonical recording/queue occurrence, seek 12, pause/resume, **12 playing tail/back cycles**, natural EOF, queue advance and retirement passed. |
| `production-guard-observed-08` | Same final app and production integrity helper, with an external observation-only server copy. A `production-corrupt-peer-rejected` event was captured; four invalid pieces were observed, followed by strict Genesis seek/pause/resume/tail/EOF/queue/retirement success without fallback. The diagnostic quarantine used in the earlier experiment was removed. |
| `final-review-09` | Fresh production runtime/profile: **N.Y. State of Mind** loaded 4,791 ms; advancing 6,942 ms. Matching tags and output, seek 12/pause/resume passed. Left open and paused for review, PID 18660, pinned by `receipt.json` and `live-verification.json`. |

The first three runs and the final review used the shipped bundle through the normal native patch path, without diagnostic server/player logging. The observation-only fourth run is labeled separately. Completed runs had no remaining owned runtime process when checked. Actual-app drivers invoke production semantic actions; these results do not claim physical mouse automation or human listening acceptance. Timing includes real provider work and is sample evidence, not a guarantee.

The initial `baseline-01`, using a test-profile junction, exited with single-instance rejection; its underlying cause was not investigated. It did not reach playback and is excluded. `baseline-02`, `race-genesis-01`, `piece-genesis-02` and `peer-genesis-03` are retained failed controls, not passed journeys. Test profiles were archived intact to the evidence drive after process exit; normal app data was not moved.

Authored, built, focused-tested and actual-app-qualified states are distinct from commit, branch push, main integration and release. This follow-up does not merge into main or publish a release.
