# Vinyl M0 / M0R receiving — 2026-10-20

## Status

- M0 availability accepted and committed as `008aeb812` on `codex/vinyl-production-flow`.
- M0R backend baseline accepted: clean build, 47/47 CTest harnesses and 40 repeated real-engine runs passed; exact source hashes frozen.
- Main-branch integration, live native facade adoption and release qualification remain separate.

## Promoted implementation

The source set comes from the Arc 62 backend inventory in
`2026-10-20-vinyl-backend-coverage.json`: **142 C++ implementation/header/test and
JSON fixture files**. Original raw SHA256 values were checked before copying and
again when preparing the repair diff. Original Arc files were preserved.

The 35 independent prototype CMake definitions and one machine-specific runner
were replaced by `native/vinyl/backend/CMakeLists.txt` and the standalone
`tests/vinyl-backend` receiving build. Existing component directory names remain
for provenance. Shared implementations compile once into
`colosseum_vinyl_backend`; fake catalogue and Artist sources are in a separate
test-support archive. This target is not linked into the live app yet.

The promoted source and test-control files use UTF-8/LF with scoped Git
attributes. The accompanying M0R source manifest pins the exact repaired bytes.

## Repairs

| Review | Implemented repair |
|---|---|
| R1 | Artist composition now uses BG1's integer duration and constructs required typed source batches correctly. |
| R2 | BB5 restore requires the expected profile and returns a typed mismatch; resolution sessions reject reassigned foreign-profile memory at operation boundaries. |
| R3 | Session capture/restore represents continuing resumable playback and pending replacement intent together. |
| R4 | Missing-transform/out-of-window seeks reject before pending state changes. Exact failed/cancelled requests, monotonic sequence ordering, observed target evidence, caller-driven expiry, quarantined generations and EOF cleanup have defined behavior. |
| R5 | Artist transport tickets are mapped across catalogue/media lanes; stale cancelled callbacks are fenced. BB5 coalesces expiry refresh and binds playback failures to the current work token. |
| R6 | Exact integer persistence/version validation, coordinate range checks, duplicate-row rejection, bounded display/container snapshots and rejected-write atomicity are enforced. |
| R7 | Annotation/graph/ranker evidence identities use unambiguous versioned serialization; cue anchors are checked against actual cue text; stored graph payloads are normalized; lyric offset inversion clamps at the final playback boundary. |
| R8 | Calibrated scores are bounded before integer conversion; score arithmetic saturates; unsupported/unevidenced graph relations do not create affinity; duplicate generator support is bounded. |
| R9 | Loaded selections remain usable during continuation, including revision checks for empty continuation. Unavailable media responses retain their typed state. Per-edition track coordinates and provenance preserve repeated recordings; ambiguous recording-only playback rejects. |

Recommendation policy is explicit: one support award and the strongest
calibrated contribution per generator; source contributions/provenance remain
available. `historyComplete` defaults false. Absence of history does not assert
that a recording has never been played. Artist-wide suppression, eligibility
filters and passive-listening policy remain named adoption decisions.

Snapshot limits bound individual codec payloads; they are not product-wide
library or history retention limits. Host adapters must use the existing stores
and suitable transactions/chunks rather than silently truncating user data.

## Verification record

Target: Windows x64, Qt **6.11.1**, MSVC compiler **19.44.35227** / tools
**14.44.35207**, Windows SDK **10.0.26100.0**, installed libmpv and MpvQt headers.
Builds/logs are outside the source checkout.

- Core: 19 behavioral failures captured before their respective fixes, plus a
  recovery API compile failure. Six default harnesses passed, including real
  seek and queued CUE journeys; three additional final CUE repetitions passed.
- Enrichment: 16 defect regressions failed before fixes, one malformed-number
  control already passed; all 17 focused cases and 13 default harnesses passed.
- Sources/Artist: real MSVC composition failures and the targeted behavior
  regressions were captured. Initial repaired sweep passed 4/4. A later 13/14
  sweep exposed transient mixed enum ABI objects during concurrent header edits;
  test expectations were preserved. Clean final receiving supersedes that run.
- Inventory guard: six unit cases pass; missing, disabled, duplicate and
  unexpected tests reject. A negative CMake configuration confirmed that missing
  real CUE media fails rather than reducing the qualifying set.
- Combined receiving: **47/47 passed**, none disabled/skipped, 24.25 seconds,
  after a clean rebuild of all 147 build steps. The inventory guard also passed.
- Repeated real-engine receiving: **40/40 passed** — CUE journey, playlist-entry
  identity, seek completion and bounded playback each repeated ten times,
  79.39 seconds total. No failing repetition was discarded.
- M0 availability closeout follow-up: **2/2 passed**, 0.30 seconds. Android harness expectations
  now agree with the intentionally disabled Android policy; no Android build or
  runtime qualification is claimed.

The historical W4 36/37 CUE failure was not reproduced in four fresh runs of
the original seek core. Separate deterministic regressions did reproduce
unobserved startup restart acknowledgement and older sequence replacement.
Those gaps were repaired. The historical incident's exact cause is not claimed
proven. The original real-journey **300ms** assertion was retained.

Real libmpv probes use null audio/video outputs. They qualify engine behavior,
not audible output, remote providers, installed app behavior or the live Vinyl
journey. No live app player/session/store wiring changed in M0R.

## Independent review and remaining boundaries

Independent read-only review found **no Critical or Important findings**. Its
repair package SHA256 is
`00af5def6472f6e3af53ae57431d3f5e099f0b0938142e7b2fba8a48ee2e67b2`.
Review did not rerun tests; parent execution established the receiving results
above. All 151 source/control hashes remained unchanged throughout receiving.

One pre-existing Minor is deferred: standalone `ArtistArtworkResolver` accepts
the nil UUID as a canonical Artist MBID. The composed adapter first uses BA0,
which rejects nil IDs, so its current entry is protected. Align the standalone
validator while receiving the native facade in M0F.

M0F must establish the local Vault / optional verified MBID boundary, one native
session/player authority, existing Progress/Collection/Activity/profile store
adapters, and owning-thread/profile/shutdown lifecycle. Concrete live provider
ports remain subsequent milestones. The Artist factory's optional memory store
is not a production persistence owner.

Seek deadlines require a caller-owned monotonic clock and regular `expire`
calls. Recovery after quarantine needs a fresh BA2 work generation. Delayed
live event handling can outgrow the settled-position observation window;
the host driver must provide appropriate evidence or a bounded failure, never
fabricate completion. Actual precise occurrence command selection remains to be
exposed by the native facade; the current recording-only port rejects ambiguity.

## Reproduce

Use `tests/vinyl-backend/README.md` for the portable configure/build/test and
inventory commands. The final source manifest, fresh combined results and
receiving receipt must stay together when handing this baseline to M0F.
