# Personalized Vinyl Home — final qualification, 2026-10-23

## Decision and source

The personalized Home implementation is received for the bounded desktop flows
below. This closes Task 7 and Home checklist H3/H7/H8; H4 remains received. It does
not close the complete music-product checklist, remote playback provider matrix,
subjective recommendation quality, main integration or release.

Branch `codex/vinyl-production-flow`, Task 7 base
`b1b6659c2293997d1642699ea89faf2e66d8cf14`. Production app, native bridge and
service test were rebuilt after the two fixes. Qualification executable SHA256:
`30FA229A5531898EB11A24FE53010488028657C10F31A88D254C51F632AB8E83`.
Native Home source SHA256:
`CF90CA954DE491A2DEFC21CC64BD88B979B8986D1B0BC3CA704772B77BB82238`.
Production QML manifest remains
`d0f490e262d356318f63ac1e03420c5f58d3f5884f1ee318f87c7ccc57ae7cc2`.
Receipts record base HEAD, app/driver hashes, profile, PID, times and exit result;
later receipts also record the native Home source hash. The native fixes were
uncommitted during qualification and are included in this record's commit.

## Receiving and fixes

A fresh read-only review of the personalized implementation range found two
Important issues. Root reproduced both in native tests (**30 passed / 2 failed**)
before changing production code, then fixed them in one pass:

1. **Visible exposure reordered the shelf being observed.** Current-visit
   exposure is still persisted/deduplicated, but ranking applies prior visits'
   exposure. A 16-candidate regression checks unchanged cards/scores after capture
   and another input refresh, then verifies fatigue applies on the next visit.
   The policy hash explicitly records `exposureApplication: prior-visits`.
2. **Artist exclusion missed implicit own-release credits.** A provider's own
   release shelf establishes its owner even when explicit credits are absent.
   That identity now joins the merged card's suppression/recovery context across
   contributions. A related artist's seed remains discovery provenance. The
   regression checks exclude/reallow, recovery metadata and an unaffected related
   artist. Production Main also exercised exclude/reallow after this fix.

Final service suite passed with **32 QtTest cases**, including init/cleanup.
Final affected CTest selection: **20/20 passed in 40.90 seconds**. It covers
recommendation service/history/contracts, BE contracts/persistence/derivation/
ranker, feedback/music actions, native Home/onboarding/library/discovery/catalogue,
playback/queue/actions/seek. The 14 recommendation provider Python tests passed;
their HTTP 429 case is injected fixture evidence, not a new live rate-limit result.
Luna xhigh reviewed existing QML coverage, found no missing decisive regression,
and independently ran the personalized Home native-bridge suite successfully.
No full repository suite was run.

## Actual production Main journeys

`PersonalizedHomeFlow.qml` now supports the final combined qualification and
timing/identity records. `Run-PersonalizedHomeQualification.ps1` is the scoped,
parameterized runner; it accepts build/Qt/runtime paths, an evidence directory,
disposable `VinylQual…` profile and journey/phase. It preserves prior receipts,
uses the actual executable, checks PASS/exit status and never changes machine
network adapters. Presentation/privacy flows reuse the existing presentation
driver. No account or synthetic catalogue transport was installed for live runs.

| Case | Actual result |
| --- | --- |
| Fresh Nas | PASS: top-icon artist search, Done, seed-bound public release cards, real album/tracklist, artist/detail/Back, local player/Next/Queue, feedback, Edit/Cancel/Done |
| Fresh Ilaiyaraaja + S. P. Balasubrahmanyam | PASS: both tastes retained, distinct content from Nas, same navigation/playback journey |
| Nas + Ilaiyaraaja + SPB | Follow-up fresh-profile PASS: all three seed identities/reasons represented; first attempt failed complete three-seed availability, retained below as a failure sample |
| Two John Williams artists | PASS: `UCfv8TEQ44ej9uPGF-SNsDBA` and `UCZH8NkgXeliBVbpaYY2PoNQ` remain separate selections and seed evidence; no fabricated canonical merge |
| Nas, regional mix and John Williams restart | PASS: tastes restored, stable See-all/detail/Back and exact queue occurrence IDs/order/index matched the prior runs |
| History disabled before listening | PASS: tastes/rows/playback remain usable; qualified manual sessions/exposure remain zero |
| Retire Vinyl profile during real pending catalogue/search work | PASS after nine seconds: no old drafts, selected/derived rows or queue resurrection. This uses the actual session retirement seam, not physical account-switch UI |
| Retained local history | PASS: actual tagged WAV listening A once/B twice, saved albums/song, Rediscover/Less played, stable routes/queue; restart restored rows; history-off/clear removed private rows/copied See-all |
| Visible exposure, hide/unhide, exclude/reallow and taste editing | PASS in final control and mixed-taste journeys without queue replacement; settled shelf keys stay stable while visible |
| Skip/restart with unavailable helper | PASS: answered empty tastes persist without another onboarding prompt |
| Fresh Skip/public catalogue | PASS: actual anonymous YouTube Music shelf → real album/tracklist → Back, then background local import/playback without losing the public See-all snapshot |
| Provider network unavailable with cache | PASS: actual previously fetched personalized cards remain rendered with cached state/retry |
| Provider network unavailable without cache | PASS: no fabricated cards; central unavailable state, Discover/Library actions and retry remain usable |

The live Nas ID is `UCPoQYATXIYvN5WB0c4f6jfQ`; Ilaiyaraaja is
`UCwzzSiogpjsYBMoyVcwDXwQ`; SPB is `UCl4iPtukwe7m0kIxUMskkgA`. These selections
remain provider-native/unresolved. This proves useful real content without a
verified canonical mapping; it does not prove these people lack MusicBrainz
records. The deliberately provider-only `UC` + 22 `A` fixture remains automated
evidence, clearly separate from the live artist cases.

The first three-taste attempt did not establish complete availability within its
refresh and failed the driver's assertion. Its original log lacked a full failure
snapshot, so no precise upstream/root cause is claimed. The follow-up fresh run
passed and retained all three owner IDs and seed reasons. The driver now dumps
failure state and checks merged seed-affinity reasons as well as the single
representative `seedArtistRef`. Public catalogue completeness remains variable;
this receiving result is not an availability guarantee.

Playback in this matrix used the existing native player with real tagged local
PCM fixtures; public catalogue detail responses were real. It is not new torrent,
YouTube audio-decoding, remote seeking or perceived-audio-quality qualification.
The existing Q1/Q2/Q6 remote-provider gates remain unchanged.

## Timing samples

Machine: Windows 11 Pro 10.0.26200, Intel i5-8365U, approximately 16 GiB RAM;
Release MSVC x64 app, Qt 6.11.1. Timings are instrumented app-driver observations,
including event-loop/render/poll overhead. Some independent profile runs and
focused tests overlapped; these are samples, not isolated benchmarks.

| Sample | Foreground render after observed world readiness | First complete taste coverage after Done | Forced background completion |
| --- | ---: | ---: | ---: |
| Nas | 194 ms | 2793 ms | 5629 ms |
| Ilaiyaraaja/SPB | 103 ms | 4315 ms | 8241 ms |
| John Williams pair | 89 ms | 2717 ms | 7721 ms |
| Three-taste follow-up | 89 ms | 5764 ms | 8284 ms |

Final warm restarts produced cached personal cards in **2 ms** each; their
foreground rendered frames took **623 / 450 / 421 ms** for Nas/mixed/John. Queue
identity comparison was machine checked against the prior final-state logs.
The native refresh timer is configured for 8000 ms; observed completion includes
timer/event-loop/poll delay. The measured 8.24–8.28 second samples must not be
reported as a strict end-to-end eight-second guarantee. Remote acquisition and
audio startup latency are separate.

## Presentation, failures and remaining boundaries

Actual desktop 1920×1080, narrow 1000×800 logical, and retained-history 880×760
logical captures were inspected (physical PNG dimensions reflect Windows DPI).
Cached covers/status, no-cache state, shelf geometry and player-bar clearance
were reviewed. The final responsive app is left open on the Ilaiyaraaja/SPB Home.
Actual app controls were driven through their semantic handlers; QML pointer/
keyboard tests are separate evidence. Physical desktop input and human acceptance
are not claimed.

Network-unavailable cases used the real child provider with HTTP(S)/ALL proxy
set to unreachable loopback port 9. Localhost remained exempt. This tests provider
network failure without disrupting other work; it is not whole-machine airplane
mode or proof that every artwork request was blocked. Missing-helper Skip cases
are separately labeled. Source expiry, all regional catalogues, optional population
coverage and subjective ranking quality remain outside this bounded qualification.
The existing unrelated Main `universeHallLayer` diagnostic was observed, not fixed.

Raw evidence is local in `D:/Colosseum-Vinyl-Home-Qualification-20261023`:
phase receipts/logs/captures, RED/GREEN/final suites, provider tests and the computed
`qualification-summary.json`. Runtime profiles, logs, generated audio, images and
machine-specific helper files are not committed. Only the runner, driver,
regressions, two native fixes and receiving/checklist changes are committed.
