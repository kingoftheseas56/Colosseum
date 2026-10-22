# Vinyl Personalized Home Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Deliver searchable per-profile artist onboarding, catalogue enrichment, explainable recommendations and a useful personalized Home in the actual Colosseum app.

**Architecture:** Extend the adopted BE candidate/taste/derivation/ranker modules behind a profile-scoped native service. Existing Catalogue/Collection/Preferences/Activity/History/Progress owners retain their authority. YouTube Music remains primary; external canonical/population evidence enriches it when verified.

**Tech Stack:** C++17, Qt 6.11.1/QML, existing Python provider process, QSettings/profile stores, current native player and test bridge. No new recommendation runtime or database.

**Spec:** [Personalized Home design](2026-10-22-vinyl-personalized-home-design.md). [Reference study and existing-code map](../reviews/2026-10-22-vinyl-recommendation-reference-study.md).

## Global Constraints

- Use the existing QML direction, primary YouTube Music catalogue, native stores, session/player and adopted recommendation backend.
- No minimum artist count. Existing 512-selection storage cap is a technical validation limit, not a requested quota.
- A generated application UUID is never a MusicBrainz ID.
- Use the existing top search icon/field, scoped to artists while the picker is open; retain the draft and return to it.
- History-derived features and exposure obey `keepActivityHistory`, reset generation and profile lifetime.
- No private history upload is required. No Tankorent playback changes belong in these tasks.
- Initial limits, labels, seed decay, diversity and cache policy are specified in the design; version every change to them.
- User authorized Luna xhigh for simpler work. Root owns identity-contract evolution, history semantics, orchestration and receiving review. Luna can handle bounded QML/fixture/documentation tasks after interfaces are fixed; no GLM.
- Commit and push explicit task-owned paths after focused verification. Main merge, release and live-provider qualification remain distinct.

## Review Focus

1. A fresh profile with no files or network must be able to finish/skip onboarding, navigate and play existing content; no repeated prompt after a committed answer.
2. Same-name artists, collaborators, remasters and YouTube-only identities must not collapse into a fabricated canonical match.
3. History disabled/cleared, profile switch and late network replies must not resurrect inferred taste, expose another profile's drafts or change its queue.
4. Thin regional evidence must still produce seed-relevant catalogue content without unrelated popularity padding or invented unseen/listened claims.
5. Repeated rendering, pagination, retries and duplicate evidence must not multiply score/exposure, reorder stable snapshots or replace playback.

## Execution and verification convention

Work on the existing Vinyl implementation branch or an owned continuation with an exact recorded base; study baseline was `d1086e74a`. Inspect applicable AGENTS and current dirt before implementation. Preserve unrelated work and do not relink an executable while the user is running that binary. Do not assume the current open inspection app already includes these planned changes.

Commands below are **planned verification**, not results. `$BuildDir` is the executor's existing configured out-of-tree native build for that exact checkout, validated against `CMakeCache.txt`; use the repository Windows setup if it needs configuration. Each new native target/test below is created in `tests/CMakeLists.txt` by its owning task. Run focused tests, not a full suite by default. Test fixture providers/clock injection must be explicit; a mock pass never qualifies public endpoints.

## Shared interface decisions

- New QML-creatable `VinylRecommendationService` in `Colosseum.Vinyl 1.0`; wire one instance in `VinylLiveWorld.qml`, with `source`, `preferences`, `collection`, `activity`, `history` and `session` QObject dependencies. C++ reads typed native owners where available and fails closed on absent owners. Add `profileScope:QString`, `profileGeneration:quint64` and `profileActive:bool` inputs, using the same active scope as `VinylBackendSession` and generation/activity as `VinylLiveSession`. Every request token includes scope, generation, seed revision and query/snapshot ID; changing owners/scope cancels prior work before accepting new results.
- Properties/signals: `onboardingState` (`unanswered|draft|answered|editing`), `artistResults` (items plus cursor/status/error), `draftArtists`, `homeSections`, `revision`, and change notifications. Results are bounded plain maps, not live backend C++ containers crossing into QML.
- Methods: `beginHomeVisit()`, `searchArtists(QString query, QVariantMap cursor = {})`, `toggleDraftArtist(QVariantMap artist)`, `commitTaste(bool skip) -> bool`, `beginTasteEdit()`, `cancelTasteEdit()`, `refreshHome(bool force = false)`, `setFeedback(QVariantMap itemRef, QString action) -> bool`, `recordVisibleItems(QString snapshotId, QString shelfId, QStringList itemKeys)`.
- Artist selection map: `{key, kind:"artist", provider:"youtube-music"|"local"|"musicbrainz", providerId, name, image, canonicalMbid?, mappingStatus}`. `key` is an encoded structured tuple, never a delimiter-concatenated name. Canonical ID is optional. Local IDs remain valid choices.
- Section map: `{id, kind, title, status, items, reason, fetchedAt, snapshotId, error, retryable}`. Card map: `{key, kind, title, subtitle, cover, destination, playable, reasons, seedKeys, provenance}`. `destination` uses existing artist/album/song/playlist route shapes. Missing playback match means `playable:false`, not a guessed first search result.

### Task 1: Working first-Home artist selection

**Files:** Modify `native/account/ProfilePreferencesStore.{h,cpp}`, `native/main.cpp`, `native/CMakeLists.txt`, `native/vinyl/VinylProvider.h`, `qml/vinyl/{VinylTastePicker,VinylLiveWorldData,VinylLiveWorld,VinylHomePage}.qml`, `tests/CMakeLists.txt`. Create `native/vinyl/VinylRecommendationService.{h,cpp}`, `tests/vinyl-local-session/tst_artist_onboarding.qml`, `tests/auto/account/tst_vinyl_taste_selections.cpp`.

**Interfaces:** Implement the service's onboarding/search methods and properties. Extend Preferences with `vinylTasteArtists: QVariantList` and `setVinylTasteSelections(bool answered, QVariantList artists) -> bool`; continue exposing compatible legacy IDs. Permit/forward the dedicated `taste-artists` lane in the existing transport/facade and use its existing categorized artist operation, preserving the ordinary Discover query and cursor. No full artist-page open is required.

- [x] Add failing native tests `emptyDoneAndSkipPersistAnswered`, `migrationPreservesLegacyAnsweredIds`, `failedWriteKeepsPriorSelections`, `selectionRoundTripPreservesProviderIdentity`. Verify answered=false with picks remains invalid and 512 remains the existing cap.
- [x] Add failing QML tests `firstHomeOnly`, `leaveAndReturnKeepsDraft`, `searchPagesKeepSelection`, `editCancelDoesNotEraseTaste`, `noNetworkStillAllowsSkip`, `lateSearchCannotChangeAnotherProfile`. Assert an unanswered profile opens the picker only in Home and the ordinary top-search field can search artists without adding a second field.
- [x] Implement atomic selection/answer persistence and migration; do not call the current destructive `resetPicker()` merely to edit. Support remote artist results plus local/saved suggestions. Missing names/images use honest fallbacks.
- [x] Wire first meaningful Home visit, scoped top search, Done/Skip/Edit/Cancel icons and existing neutral focus/card components. A committed answer shows the selected artists as a small real artist shelf immediately; activating one opens its actual Artist page. Other Home rows continue to work.
- [x] Register/build `vinyl_taste_selections_test` and native-bridge `colosseum.qml.vinyl_artist_onboarding_native`; run `ctest --test-dir $BuildDir -R 'colosseum\.(qttest\.vinyl_taste_selections|qml\.vinyl_artist_onboarding_native)$' --output-on-failure`. Both must pass; run a fresh Main profile through remote search -> choose -> Done -> restart and separately Skip -> restart.
- [x] Review explicit diff; commit `Add profile artist onboarding to Vinyl Home`. Root accepts native state/storage; Luna may implement QML after method names are fixed.

**Received 2026-10-22:** [First-Home onboarding record](../reviews/2026-10-22-vinyl-artist-onboarding.md). 13 focused suites and actual Main search/Done/restart/Edit-Cancel/Skip-restart passed. The existing generic catalogue facade already forwards the categorized artist operation; no extra facade implementation was needed. Tasks 2 onward remain open.

### Task 2: Provider-aware recommendation identities without false MBIDs

**Files:** Create `native/vinyl/VinylRecommendationIdentity.{h,cpp}` and `native/vinyl/backend/be1-recommendation-candidate-prototype/RecommendationEntityRef.{h,cpp}`. Modify BE1 candidate contract, BE2 feedback/persistence, BE derivation and BE3 ranker headers/implementations, associated existing tests, `native/vinyl/backend/CMakeLists.txt`, `tests/CMakeLists.txt`. Create `tests/vinyl_recommendation_contracts.cpp`.

**Interfaces:** `RecommendationEntityRef` stores entity kind plus `std::variant<CanonicalEntityRef,ProviderItemRef>`; provides typed factories, stable serialized key and equality. Existing MBID constructors/factories remain supported. `VinylRecommendationIdentity::fromCatalogue(QVariantMap item) -> QVariantMap` returns `{ok, ref, destination, mappingStatus, error}`; a provider ref cannot be relabeled verified by parsing a UUID. Canonical feature relationships continue to require canonical IDs. Provider artist/release context is carried explicitly for diversity/seed affinity, not inserted into BF1 as a canonical edge.

- [x] Add failing contract tests `providerAndCanonicalNeverAlias`, `generatedUuidIsNotCanonical`, `collaboratorsRemainSeparate`, `ambiguousVersionCannotPlay`, `canonicalSnapshotV1MigratesLosslessly`, `unknownSnapshotVersionPreservesStoredBytes`, `duplicateContributionsDoNotMultiplySupport`.
- [x] Extend existing recommendation contracts to the typed union and feedback snapshot version 3 (the existing writer was already version 2). Preserve current canonical semantics and migration; active-state errors never overwrite a valid saved snapshot. Convert verified equivalents to a single key only when mapping evidence warrants it.
- [x] Update BE3 artist/release diversity-key extraction for provider-native context; keep all existing ranker/default canonical tests. No identity changes to Collection, playback queues or saved playlist occurrence IDs.
- [x] Build `vinyl_recommendation_contracts_test` linked to the existing backend; register/run `ctest --test-dir $BuildDir -R 'colosseum\.vinyl_recommendation_contracts$' --output-on-failure`. Register the affected existing BE contract tests with this target or separate focused tests so canonical regressions are actually executed, not merely compiled.
- [x] Commit `Support typed provider identities in Vinyl recommendations`. Root owns this task; it is not a simple mass-replacement job.

**Received 2026-10-22:** [Identity contracts receiving record](../reviews/2026-10-22-vinyl-recommendation-identities.md). 20 focused suites passed, including all five existing BE contract mains and native feedback-port validation. Feedback v1/v2 canonical readers remain; typed recording playability is explicit, and album candidate/exposure eligibility agrees. Live enrichment/mapping qualification remains Task 3.

### Task 3: Bounded catalogue enrichment and candidate transports

**Files:** Modify `scripts/vinyl/provider.py`, `native/vinyl/{VinylProvider.h,VinylCatalogueSource.h,VinylCatalogueSource.cpp}`, `tests/vinyl-live/test_provider.py`, `tests/vinyl-catalogue/tst_catalogue_source.cpp`. Create `tests/vinyl_recommendation_provider_test.py`. Extend Task 2 contract coverage for mapping results.

**Interfaces:** Reuse Task 1 artist search; add provider operations `artistCandidates`, `recommendationIdentity`, `recordingMatch`, `populationCandidates` through `requestPayload`; isolated lanes `home-seed-0`, `home-seed-1`, `home-population`, `home-identity`. Candidate payload `{version:1, seed, items, provenance, fetchedAt, complete, status}`; each item carries the shared typed identity/destination fields. Existing artist search/mapping code is factored into bounded helpers, not duplicated or coupled to the currently open artist.

- [x] Add failing cases `youtubeOnlyArtistStillHasOwnReleases`, `boundedIdentityDoesNotEnumerateDiscography`, `sameNameArtistStaysAmbiguous`, `recordingMatchRejectsCoverAndWrongVersion`, `partialSearchIsNotNegativeCache`, `rateLimitDefersWithoutLoop`, `cacheEvictsWithinBudget`, `malformedOrOversizedPayloadFailsClosed`.
- [x] Reuse YouTube artist releases/top recordings/related artists only where the current upstream response exposes them. Missing capability yields unavailable for that lane; never scrape a new site or substitute charts for relatedness. Enrich date/type/artwork/credits from qualified existing paths. Apply the spec's request/concurrency/cache policies through the existing provider cache with versioned envelopes and bounded eviction.
- [x] Implement an optional ListenBrainz artist-evidence adapter for verified artist MBIDs. The route observed in Troi is `GET /1/lb-radio/artist/{mbid}` with `mode`, `max_similar_artists`, `max_recordings_per_artist`, `pop_begin`, `pop_end`; current auth/response support must be checked against official docs and real responses before enabling. Do not require user recommendations, tokens, private history or Last.fm.
- [x] Run `python -m unittest discover -s tests -p 'vinyl_recommendation_provider_test.py'` with injected transports/clock. Build `vinyl_catalogue_source_test`; run `ctest --test-dir $BuildDir -R 'colosseum\.vinyl_catalogue_source$' --output-on-failure`.
- [x] Record a bounded live capability matrix for Nas, Ilaiyaraaja and S. P. Balasubrahmanyam, plus two same-name artist candidates and one provider-only fixture: exact returned IDs, source status, seed-related count, latency and mapping outcome. Obtain canonical IDs from verified mappings, not guesses. Public failure keeps the optional lane disabled and must be reported; own-artist YouTube shelves must still work. This gate qualifies transport/cross-market coverage, not subjective ranking quality.
- [x] Commit `Add bounded Vinyl recommendation evidence transports`. Root owns identity/transport decisions; Luna may add bounded fixture coverage.

**Received 2026-10-22:** [Candidate transport receiving record](../reviews/2026-10-22-vinyl-recommendation-candidates.md). Real YouTube candidates and candidate-to-album tracklists passed for Nas/Ilaiyaraaja/SPB; same-name identities remain separate. Optional canonical mappings remain unverified for these selected seeds; ListenBrainz's independent canonical route passed. 14 new Python checks, 50 existing Python checks and 20 focused CTest suites passed (optional live skips recorded). Automatic composition/ranking/Home presentation remains Task 4 onward.

### Task 4: Native candidate orchestration and deterministic first-day ranking

**Files:** Extend `native/vinyl/VinylRecommendationService.*`, BE3 config/evidence declarations and implementation, `native/CMakeLists.txt`, `tests/vinyl_recommendation_contracts.cpp`. Create `tests/vinyl_recommendation_service_test.cpp`; register its native target in `tests/CMakeLists.txt`.

**Interfaces:** Implement `refreshHome(bool force)`, `homeSections`, snapshot/revision/error notifications and transient per-lane fencing. Inputs are committed selections, Collection membership, Task 3 evidence and Task 2 identities. A successful response never rewrites the selected profile's library or queue.

- [ ] Add failing tests `disjointSeedsYieldDifferentRelevantPools`, `allUnmappedSeedsStillProduceProviderNativeCandidates`, `oneFailedLaneKeepsOtherSections`, `stableInputClockAndConfigProduceSameOrder`, `retryAndPaginationCannotBoostScore`, `diversityKeepsDistinctVersions`, `lateReplyCannotReplaceNewSeeds`, `refreshDoesNotMutateQueue`.
- [ ] Build independent source contributions, validate/merge via BE1 and run BE3 using `vinyl-home-v1`. Implement the design's +300 seed-affinity evidence, budgets, fairness cursor and diversity policy. Seed selections remain distinct from explicit Positive feedback. Use no population-score calibration unless a scale and bounded multiplier are documented and fixture-tested.
- [ ] Emit real `From your artists`, `Related discoveries`, `New from your artists` sections, hiding unsupported/empty lanes. All reasons reference actual seeds/evidence; undated releases cannot enter the New row. Cache-first sections arrive without waiting for the eight-second background budget.
- [ ] Build `vinyl_recommendation_service_test`; run `ctest --test-dir $BuildDir -R 'colosseum\.vinyl_recommendation_(contracts|service)$' --output-on-failure`. Record source/config/fixture hashes and explicit evaluation time for reproducibility.
- [ ] Commit `Rank Vinyl Home candidates from explicit artist tastes`. Root owns orchestration/ranking acceptance.

### Task 5: Qualified history, feedback, suppression and exposure

**Files:** Create `native/vinyl/VinylRecommendationHistory.{h,cpp}` and `tests/vinyl_recommendation_history_test.cpp`. Modify `native/vinyl/{VinylRecommendationService.*,VinylBackendSession.*}`, `native/account/{ActivityStore.*,ActivityProjector.*,ActivityPlaybackTracker.*,ConsumptionHistoryBridge.*,ProfilePreferencesStore.*}`, existing BE derivation/exposure persistence, and focused Activity/History tests. Extend `tests/vinyl_recommendation_service_test.cpp`, `tests/auto/account/tst_vinyl_feedback_snapshot.cpp` and `tests/CMakeLists.txt`.

**Interfaces:** `VinylRecommendationHistory::project(ActivityStore*, HistoryStore*, QVariantList catalogueRows, qint64 nowMs) -> QVariantMap` returns qualified session aggregates, available semantic outcomes and `historyComplete:false` unless completeness is actually established. Add `ActivityStore::recordMusicAction(QVariantMap fact) -> bool` through the existing validation/writer, not a second log. Fact type `music_action`, world `vinyl`, kind `music`: existing identity/session/time fields plus action `start|skip|natural-eof|stop|failure`, origin `manual|album-continuation|autoplay`, position/duration and typed identity evidence. Extend serialization and history-read filtering for this device-local fact without turning it into listening-time/completion evidence itself. Add `vinylRecommendationExposureSnapshot` and validated setter to Preferences; max 4 MiB, same profile, bounded to the latest 2,000 records in the ranker's 14-day exposure window. Explicit BE2 feedback continues to use its existing snapshot owner.

- [ ] Add failing tests `seekDoesNotCountAsPlay`, `failureIsNotSkip`, `stopIsNotNaturalCompletion`, `oneSkipIsNotDislike`, `repeatSessionsDedupeBySessionNotTimer`, `savedAffinitySurvivesSeedRemoval`, `suppressedArtistExcludesItsKnownItems`, `historyOffRetainsOnlyExplicitTaste`, `clearHistoryInvalidatesDerivedRowsAndExposure`, `profileSwitchRejectsPendingFeedback`, `hiddenShelfCreatesNoExposure`.
- [ ] Read existing qualified Activity deltas/History for chronological shelves. Add music actions only where needed to retain actual intent/outcome; preserve the existing ten-second counted-listening gate and retention/reset semantics. A start/early-skip action is behavioural evidence, not counted listening time. No arbitrary inference from a generic stopped session. Keep other worlds' facts and sync shapes unchanged; new music actions remain device-local (`syncable:false`).
- [ ] Reuse BE derivation for semantic observations; add a read adapter for current normalized facts rather than fabricating a BA4 replay. Feed manual qualified sessions to seed decay at the specified 30/100 boundaries. With missing outcomes omit those features. Use existing BE2 hidden/snooze/do-not-recommend state; `setFeedback` accepts `like|dislike|neutral|hide|unhide|snooze-7d|unsnooze|exclude-artist|allow-artist` and stores atomically.
- [ ] Record visible exposure once per item/shelf snapshot per visit; do not write at model construction. Disabling history stops exposure writes; history reset clears it. Derived history state is rebuildable and must not survive a privacy reset in a separate cache.
- [ ] Run focused recommendation tests plus existing feedback snapshot and affected Activity/History tests. Register `colosseum.vinyl_recommendation_history` for the new projection; build its target and run `ctest --test-dir $BuildDir -R 'colosseum\.(vinyl_recommendation_(contracts|service|history)|qttest\.vinyl_feedback_snapshot)$' --output-on-failure`. Record additional exact Activity tests selected by the schema diff.
- [ ] Commit `Connect retained listening evidence to Vinyl recommendations`. Root owns this task; Activity format/privacy changes require direct receiving review.

### Task 6: Finish Home presentation, routes and cold/offline states

**Files:** Modify `qml/vinyl/{VinylHomePage,VinylLiveWorldData,VinylLiveWorld,VinylShelf,VinylItemCard}.qml` and shared card context actions only where required. Extend `tests/vinyl-local-session/tst_home_flow.qml`; create `tests/vinyl-local-session/tst_personalized_home.qml`; register native bridge test.

**Interfaces:** Bind service `homeSections` after existing Continue listening; retain factual Collection shelves. Feed card destinations to existing `activateItem`/route methods. Existing top search remains the only search entry. Feedback uses the shared contextual action affordance and service `setFeedback`; visible card delegates call `recordVisibleItems` only after viewport visibility is established.

- [ ] Add failing QML cases `freshHomePickerAndSkip`, `seedShelfCardRoutesAndBack`, `missingDatesDoNotShowAsNew`, `unknownHistoryDoesNotSayNeverPlayed`, `partialFailureRetainsFactualRows`, `profileClearRemovesOldRowsImmediately`, `feedbackRefreshPreservesPlayback`, `noNewHoverDescriptorsOrAutoFocusFrame`.
- [ ] Render the spec's Home order and per-shelf loading/stale/error/retry state with existing cards/tabs/cold ripple. No empty filler rows, explanatory boilerplate, permanent second search field or source picker. Retain the selected artist shelf even when related-source data is thin. Adapt service section `id` to the existing shelf `key`, `kind` and `items` fields; preserve See all as a stable snapshot route rather than falling through to the old Library-only list mapping.
- [ ] Add rediscovery only from retained prior playback and Less played from known library counts. Reuse existing saved music/playlists; do not rebuild their stores. Verify narrow/desktop geometry, keyboard navigation, active playback bar clearance and focus restoration.
- [ ] Build `vinyl_native_session_bridge` and runtime-ready target; run `ctest --test-dir $BuildDir -R 'colosseum\.qml\.vinyl_(home|artist_onboarding|personalized_home|playback_queue|player_seek)_native$' --output-on-failure`. Inspect representative rendered captures; semantic model tests do not prove layout.
- [ ] Commit `Finish personalized Vinyl Home with existing QML`. Luna xhigh can own the bounded QML work against the frozen service interface; root reviews actions/privacy and visual result.

### Task 7: Actual-app receiving journey and roadmap closure

**Files:** Create `tests/vinyl-live/PersonalizedHomeFlow.qml` and its scoped runner using the existing live-flow runner conventions. Update `docs/plans/2026-10-21-vinyl-functionality-checklist.md`; add a dated receiving record under `docs/reviews/`.

**Interfaces:** Real production Main, service, provider and native stores/player; separate disposable profiles for two disjoint taste sets, one Skip case and one history-disabled case. Fixture transport runs are separately labeled. No synthetic catalogue response may qualify a live source.

- [ ] Implement and run: fresh Home -> top-icon artist search -> select -> Done -> personalized rows -> Artist/Album/Song -> play/Next/Queue -> Back -> restart. Check exact provider IDs and reason evidence, not only nonempty rows.
- [ ] Run distinct Nas and Ilaiyaraaja/SPB selections: own-artist content must differ and retain both taste regions when mixed; thin public similarity must not erase own-artist results. Include a YouTube-only seed and same-name identity case from Task 3's recorded matrix.
- [ ] Run Skip/restart, Edit/Cancel/Done, unavailable source with cache and without cache, rapid query/profile changes, history disabled, history clear, suppression and reallow. Keep playback unchanged through browse/rank refresh. No external account should be needed.
- [ ] Measure foreground Home render, cache display and background completion separately. Target local/cached rows within one second after world readiness on the qualification machine and terminate remote refresh at the eight-second budget. Record cold/warm measurements, failure samples and device context; do not turn one run into a service guarantee.
- [ ] Inspect actual desktop and narrow layouts and physical pointer/keyboard flow where tool support permits. Label semantic-handler, pointer-test, rendered-capture and human acceptance evidence distinctly. Do not require Hemanth to perform a check that the agent can do.
- [ ] Close only proven H3/H4/H7/H8 and relevant catalogue gates; leave unresolved source/data-quality/subjective acceptance items open with concrete evidence. Keep Tankorent success and intermittent remote seek gates from slice 6 unchanged.
- [ ] Commit/push accepted files, verify remote branch SHA and leave a responsive app open on the completed Home. Report branch versus main integration accurately. This is the point to resume Tankorent work after user acceptance of the recommendation/Home result.

## Plan self-review

The five Review Focus cases are assigned to Tasks 1/2/3/4/5/6/7, not deferred to a generic final test. Every added service/store property has an owner and a migration/privacy rule. Provider-native identity and missing semantic listening events are prerequisite work, not claimed as existing live features. Proposed constants are explicit, versioned first-pass policy, not research conclusions. All acceptance boxes remain unchecked: this task produced the study/design/plan only.
