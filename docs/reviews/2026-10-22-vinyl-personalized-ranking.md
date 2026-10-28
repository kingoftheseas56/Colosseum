# Vinyl personalized Home composition and ranking

Date: 2026-10-22. Base: `ed4ff5fd3`; branch `codex/vinyl-production-flow`.
Scope: Task 4 plus the existing-QML binding and destinations requested in this slice.

## Implemented

The existing `VinylRecommendationService` now composes Task 3 catalogue evidence with selected artists, saved artists and matching saved albums. Preferences, Collection, public metadata cache, BE1 candidate contracts and BE3 ranker retain ownership of their existing state. The new service implementation is a transient profile-scoped projection, not another personal store.

- `vinyl-home-v1` awards +300 for each distinct seed relationship. Repeated replies/contributions cannot multiply that relationship; verified aliases of the same canonical artist award it once. Seed affinity is separate from explicit Positive feedback. Existing ranker callers retain their defaults.
- Candidate admission is bounded to 300 entities, 60 per generator and 12 per shelf. Artist contributions enter round-robin. Mixed selections cap a primary artist at three results; an exact single-artist shelf can exceed that cap. At most two recordings from one confirmed release group survive. Album editions retain distinct identities and do not consume the recording cap.
- Real sections are `From your artists`, `Related discoveries` and, only with a full date within the last 90 days, `New from your artists`. Year-only metadata cannot enter New. Adjacent rows prefer alternatives to repeated entities. Unsupported and empty lanes are hidden.
- The existing cache is read before background requests. A refresh admits at most six uncached artists, runs at most two requests concurrently and ends at eight seconds. Explicit refresh advances the fairness cursor. Playback/re-render does not rotate it. Later Home visits reevaluate cache age and the New cutoff. Optional identity failures do not mark fresh YouTube rows stale.
- Profile changes clear the projection and fence old replies. Source replacement preserves the current seeds and rejects old senders. Saved artist membership survives removal from onboarding choices. Ranking/refresh never writes the player or queue.
- Native sections bind after Continue listening and before factual saved shelves. See all retains a snapshot through detail navigation and Back. Album/artist cards use existing destinations. The Qt sequence wrapper is materialized through the session's existing `plainData` boundary. YouTube album navigation uses the provider browse ID; returned stable album UUIDs remain owned by the existing catalogue.

Top-recording candidates without separately qualified playback evidence are not displayed as playable recommendations. ListenBrainz canonical recordings currently lack usable titled provider destinations, so they are not promoted into visible cards. Available own releases and related artists still appear when canonical matching is unavailable. No recording match, date, genre, listening event or population score is invented.

## Verification

- Full Windows `colosseum` application, native session bridge, service, catalogue and ranker targets built. The actual app build exposed a missing public backend include dependency; the app library now exports its existing backend dependency correctly.
- **22/22 focused CTest suites passed in 96.39 seconds** on the final production sources. This includes BE1/BE2/derivation/BE3, preferences, native facade/catalogue, onboarding, Home, saved library, discovery/search and playback/queue coverage. The new service QtTest reports **19 cases**, recommendation contracts **18**, and personalized native/QML bridge **15**, including init/cleanup. Existing opt-in live catalogue coverage remains separate from these deterministic runs.
- RED regressions reproduced stale visit clocks, source replacement losing seeds, canonical aliases doubling affinity, optional identity failure incorrectly degrading fresh shelves, native Qt lists disappearing in QML, unregistered album navigation, and albums consuming the recording cap. Each passed after its fix. Luna xhigh supplied bounded QML and native fixture work; root reviewed the implementation and handled orchestration, ranking, actual app runs and receiving fixes.
- The production Main journey uses actual native stores/provider/player and anonymous YouTube Music search. Separate disposable profiles select Nas and Ilaiyaraaja/SPB, commit Done, render seed-bound rows, open See all, open a returned real album and nonempty tracklist, restore Home/See-all through Back, and visit a related artist. The mixed shelf retains both selected artist IDs. There are no fabricated New rows for the year-only live responses.
- A locally generated silent WAV qualifies **existing-player continuity only**: importing/playing it, then refreshing real recommendations leaves playback progressing and queue occurrence IDs unchanged. This is not a new remote playback or Tankorent qualification.
- Restart restores the two selected artists and cached rows; Skip and Skip/restart preserve an answered empty selection with no fabricated recommendations. Desktop and narrower rendered captures were inspected with real cover art. The driver calls actual semantic actions; physical pointer/keyboard acceptance is not claimed.

## Reproducibility and observations

Deterministic service fixtures inject `1792670400000` milliseconds as the evaluation clock. Each Home snapshot exports evaluation time, source/config/seed hashes, candidate count and `historyComplete:false`. Policy hash `a7f480a585dd7c7f2221ac06e7a27669c9ae83178ce6500d365671961c89ca46` identifies the Home limits; inherited BE3 defaults are pinned by the committed ranker source.

Initial successful actual-app samples, measured from Done or Home entry after world readiness:

| Taste profile | Eligible candidates | Own / related cards | Rows available |
|---|---:|---:|---:|
| Nas | 30 | 12 / 10 | 2292 ms |
| Ilaiyaraaja + SPB | 57 | 6 / 12 | 2120 ms |
| Mixed restart, cached | 57 | 6 / 12 | 9 ms |

These measure model availability, not cold application startup, all artwork loading, or a provider latency guarantee. Optional enrichment can continue after rows appear. Final-build rerun receipts are retained alongside the initial evidence.

SHA256 of source/fixtures before commit (working-tree UTF-8 bytes):

| File | SHA256 |
|---|---|
| `native/vinyl/VinylRecommendationHome.cpp` | `dfeaf2a4f4bcddab77a02df08ee5e751abe67639f581faec4d7f8d7165b08f39` |
| `native/vinyl/backend/be3-deterministic-ranker-prototype/DeterministicRanker.h` | `ec402d97092ef0a8d90da182325b4b241246b248df45203dd2354caa7f10f42c` |
| `tests/vinyl_recommendation_service_test.cpp` | `6f73dcb8d62c5f7ec592c5e6ed1afefa2569cd595799a25aac930f922d383744` |
| `tests/vinyl_recommendation_contracts.cpp` | `026e5ba21520a58e96d647a26fc8f99279ece37f99a31fc411619f7a017e3870` |
| `tests/vinyl-local-session/tst_personalized_home.qml` | `d8c7248056a3a56c96c66b41fb42ffed05585c52343a99b00684c7d0710b1b50` |

Machine-local evidence: `D:/Colosseum-Vinyl-Home-Ranking-20261022/`: receiving RED/GREEN logs, Qt-list and album-route regressions, final 22-suite log, app build log, actual Main journey logs, extracted snapshots and desktop/narrow captures. Public responses, local audio, profiles, caches and screenshots are not committed.

## Remaining scope

Task 4 is complete. This includes the minimum Home binding/routes needed to use its output now; it does not close all later presentation/receiving tasks. Task 5's qualified listening outcomes, decay, suppression and visible exposure remain open. Recommendations in this slice use explicit selections/membership and available existing feedback, with no inferred personal listening history. Full offline/no-cache UI, physical input and subjective acceptance remain separate. Main's unrelated `universeHallLayer` warning and existing Tankorent/remote-seek gaps were not changed. Work is delivered on the feature branch; no main merge or release is claimed.
