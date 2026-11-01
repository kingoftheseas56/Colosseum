# Vinyl V2.1 real lyrics implementation plan

Status: implemented and receiving-reviewed on `codex/vinyl-v2-lyrics-20261029`; uncommitted. This is the lyrics milestone and shared reading interaction contract. Genuine Genius annotations and the later video milestones remain outstanding.

Goal: retrieve qualified LRCLIB content for the playing catalogue song and present it in the existing Song view, with safe identity/clock handling and the approved reading interactions.

Architecture: extend BC1 around existing provider/canonical entity references; use the existing Vinyl provider transport/cache and a native presentation service. Keep source resolution independent. Reuse the current session, queue, profile and Song components.

Spec: [approved Song content design](2026-10-29-vinyl-v2-song-content-design.md). Hemanth approved it and requested GLM 5.3 High through ZCode, Luna Max for bounded simpler work, and primary ownership of complex work. No worker may commit, push, merge, deploy or trigger CI.

Corrected review baseline: Git HEAD remains `3a80fab7aa1ccf9f60493e17cf71fc0a096b0b84`; the working candidate now also contains all 12 source/test changes from `codex/vinyl-shared-search` at `6f3242f158a2bdca00c64559f91e398de9a73abc`, including shared search and five-artist onboarding. The initial master-based preview wrongly omitted that existing Vinyl work and is superseded. A clean three-way application preserved the lyrics changes and current master changes; the existing shared-search checkout and ordinary checkout were untouched. This is an uncommitted source composition, not a committed branch merge. The corrected native app rebuilt successfully, and its normal UI was checked for one shared search input and absence of the prototype `vinylSearchInput`. Lyrics are open on N.Y. State of Mind, paused, for Hemanth's review.

## Boundaries and review focus

- Preserve lyrics left / artwork centered / annotations right at supported sizes.
- Do not fabricate MBIDs, note metadata, timings, or recording/version equivalence.
- Repeated queue occurrences, profile changes and late replies must not reuse stale content/actions.
- Source restart is distinct from changing the playing occurrence.
- Plain text must remain non-seeking; lyric following and annotation Auto are independent.
- Document replacement invalidates old anchors; variable-height lines must scroll by measured geometry.
- This plan delivers the lyric milestone. Genius transport/real annotations remain V2.2; the wider V2 stages remain in the consolidated plan.

## Task 1: native content identity and parser — primary

Files: `native/vinyl/backend/bc1-timed-text-prototype/TimedTextCore.{h,cpp}`, compatibility call sites in BC2/BC3, new `native/vinyl/VinylLyricsService.{h,cpp}`, and `tests/vinyl_lyrics_contracts.cpp`.

Interfaces: BC1 documents and request keys accept the existing `RecommendationEntityRef` for recordings alongside legacy `RecordingMbid` factory overloads. Optional canonical mappings stay optional. `VinylLyricsService` projects qualified documents as line objects (`text`, optional playback `time`, stable cue identity), document identity, availability, current line, and seek targets. It consumes recording/edition/queue/profile context and explicit media alignment, never stream URLs as identity.

- [x] Add focused cases for provider-only identity, unchanged canonical document identity, revision/recording differences, ambiguous version/duration rejection, malformed/nonmonotonic LRC, plain/instrumental results, and inverse offset mapping.
- [x] Extend BC1 without duplicating its cue/clock machinery; reject incompatible legacy translation/annotation calls safely.
- [x] Implement strict payload qualification, LRC ingestion and projection; malformed timing may only fall back to genuinely matched plain text.
- [x] Build/run `vinyl_lyrics_contracts` and existing BC contract checks through the actual native test graph.

## Task 2: LRCLIB transport — ZCode attempt, Luna Max delivery

The approved GLM 5.3 High launch exited 1 with no output, changed source, or reply. There is no GLM delivery or verified model execution to accept. Luna Max completed the bounded transport task; primary independently reviewed it, requested boundary corrections, and reran the decisive checks. The original ZCode failure receipt is retained separately.

Files: `scripts/vinyl/provider.py`, `tests/test_vinyl_lyrics_provider.py` only. Fresh isolated repository; launch only when the shared ZCode quota slot is free.

Interface: `fetch_lyrics(request, opener=None)` handles operation `lyrics` before constructing a YouTube client. Request fields: `trackName`, `artistName`, optional `albumName`, positive `duration` seconds and existing opaque `value`. Return `{source:'lrclib',status:'found'|'not_found',fetchedAt:<ISO UTC>,record:<bounded LRCLIB object or null>}`. Native code owns identity qualification, document construction and time mapping.

- [x] Add mocked HTTP cases for encoded signature, valid timed/plain/instrumental response, 404, oversized/malformed response, timeout, 429/503 and invalid input.
- [x] Implement bounded first-party HTTPS retrieval with timeout, typed sanitized errors, no dependencies or credentials, and no change to audio extraction.
- [x] Run `python -m unittest discover -s tests -p test_vinyl_lyrics_provider.py -v` and existing provider regression modules.
- [x] Save genuine metadata-only probe receipts for timed/plain/absent cases; provider absence remains evidence rather than invented success.

## Task 3: responsive Song composition — Luna Max

Files: `qml/vinyl/VinylSongPage.qml`, `tests/qml/tst_vinyl_song_page.qml` only. Fresh isolated repository.

Interface: preserve existing `pageData`, column object names and control functions. Replace the compact combined-text fallback with the original three-region arrangement. Adapt cover/gaps/column sizes; do not implement provider, clock, pin or seek logic in this task.

- [x] Establish the focused QML baseline, then add real geometry/visibility cases at 1280x720, 1600x900 and narrow supported sizes, including no poster movement on opening lyrics/notes.
- [x] Make the smallest layout change, retaining poster aspect ratio, text readability, video hiding of side columns and one transport.
- [x] Run `C:/Qt/6.11.1/msvc2022_64/bin/qmltestrunner.exe -input tests/qml/tst_vinyl_song_page.qml -import qml -platform offscreen` with Basic Controls style and the Qt bin directory on PATH. This is the existing project test, not a new preview project.

## Task 4: app integration and reading state — primary

Files: `VinylBackendSession.{h,cpp}`, `VinylProvider.h`, `VinylLiveSession.qml`, `VinylLyricsColumn.qml`, `VinylNotesColumn.qml`, `VinylPageData.qml`, existing controls as needed, `native/main.cpp`, `native/CMakeLists.txt`, `tests/CMakeLists.txt` and affected focused tests.

- [x] Expose the existing native playing occurrence/recording/edition/profile context to content retrieval. Register/link the native service and add the `lyrics` transport lane/operation/cache path.
- [x] Fence requests, source alignment and document replacement; retain valid content during same-occurrence source retries and serve qualified cached content offline.
- [x] Wire live Song lyrics/current line/seeking through native BC1 projection. Preserve existing local content when it is the actual source.
- [x] Implement manual lyric scrolling, measured current-line following, return icon, double/right-click cancellation, independent annotation pin/Auto states, same-song hide/revisit restoration and new-occurrence reset.
- [x] Run focused native/parser/provider and Song/notes/session QML tests against the final integrated source. Independently rerun workers' decisive checks.

## Task 5: isolated normal-app qualification — primary

Build: task-owned `D:/Colosseum-Vinyl-V2-20261029/build`, Visual Studio 2022 x64, Qt 6.11.1, Ninja and the verified existing dependency prefixes. Configure `COLOSSEUM_VINYL_IN_APP=ON`, qualification/synthetic fixtures OFF, and the verified bundled Python runtime source; stage the current provider helper from this checkout. Compiler, runtime, temp and evidence artifacts stay on D:. After the task-owned D: profile junction failed the instance-lock launch, normal-app qualification used a small fresh native C: profile tag; the ordinary profile and lock were untouched.

- [x] Configure the real native graph and build affected tests plus `colosseum_runtime_ready`. Do not alter the ordinary build or app session.
- [x] Obtain actual catalogue versions and first-party provider samples; record exact IDs, durations and matching evidence without printing full lyric bodies or transient resources.
- [x] In the isolated normal Main app/profile, verify genuine timed/plain content, play/pause/line seeking, manual scrolling, return without seeking, hide/reopen/revisit, new-occurrence reset, alignment rejection and cached lyrics with Python-provider external HTTPS blocked. Qt event tests cover double/right-click and gesture cancellation; native tests cover late request/profile/occurrence fences. Responsive geometry is verified in the real QML components at the tested desktop sizes; the normal app was inspected at 1280x720 logical pixels. These evidence scopes remain distinct.
- [x] Review the final combined diff and receipts. Report source/build/tests/runtime and remaining baseline shutdown/provider gaps distinctly. No commit, push, merge, release or CI is implied by this plan.

## Receiving evidence

- Real native MSVC/Qt 6.11.1 app graph and runtime staging built successfully with Vinyl enabled and qualification/synthetic fixtures disabled.
- Native lyric service: 12 passing Qt checks. BC1/BC2/BC3 compatibility and lyric contracts: four passing CTest targets. Native source/session bridge: 31 passing checks, including actual local playback, selected-edition content, reading retention through retry, and profile retirement.
- Python: 15 passing LRCLIB checks, 14 passing recommendation checks, and 13 passing existing provider checks with one optional live smoke test skipped.
- QML: Song 15 passing / two optional capture cases skipped; lyric column 11 passing; notes nine passing; reading state five passing; shared wheel behavior five passing. A failing programmatic-scroll regression was reproduced before the input-specific fix.
- Normal Main app: genuine Nas timed lyrics with 293.981-second audio against a 294-second catalogue signature; genuine Bhupen Hazarika plain-only lyrics, which do not seek. A longer Without Me source retains readable lyrics while timed seeking/following stays disabled. Cached plain lyrics restore the identical document with the Python provider returning an explicit unavailable error behind a refused process-local HTTPS proxy.
- First-party transport also returned the matched Weightless Part 1 instrumental record with both lyric fields null. This is provider proof, not a normal-app instrumental journey.
- Host receipts are retained under `D:/Colosseum-Vinyl-V2-20261029/evidence/`. Both isolated online and provider-blocked app runs exited 0 after their owned windows were closed. The final app also verified opening the current song through cached Browse results without losing its lyric document or manual scroll. This does not resolve or qualify the separately recorded ordinary-checkout startup/shutdown issues.
- Genuine Genius access, attribution/metadata qualification, and normal-app annotation provider journeys remain V2.2 work. No commit, push, merge, CI, release, or deployment occurred.
