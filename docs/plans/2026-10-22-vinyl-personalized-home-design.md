# Vinyl artist onboarding, recommendations and Home

Date: 2026-10-22. Proposed design for implementation; this document is not runtime or deployment evidence.

## Goal and scope

A profile's first meaningful visit to Vinyl Home offers searchable artist taste selection. Home then supplies useful music from those selections and evolves through explicit feedback and qualified listening. Finish catalogue enrichment needed for accurate cards, relationships and recommendations before resuming Tankorent playback work.

Use the existing QML direction, primary YouTube Music catalogue, native stores, session/player and adopted recommendation backend. Preserve the completed navigation, library, playlist and queue work. This work does not replace the primary catalogue with a full MusicBrainz mirror, require external accounts, introduce a second player, or implement radio/autoplay, sonic embeddings or learned ranking.

## Source of requirements

- Hemanth's current instruction and preceding confirmation that YouTube Music is primary.
- Arc 62 `research/home-discover/HEMANTH-RECOMMENDATION.md`: first-Home artist selection, per profile, optional, Done/Skip, no minimum and later editing.
- Arc 62 `recommendations/MUSIC-RECOMMENDATION-BRAIN.md`: independent candidate generators, inspectable ranking, exploration, feedback, privacy and deterministic first proof.
- Arc Q3/Q4 findings: regional coverage is uneven; absence of population evidence is not dislike or musical insignificance. Historical measurements are not fresh provider qualification.
- [Troi/Music Assistant study](../reviews/2026-10-22-vinyl-recommendation-reference-study.md).

## Architecture decision

| Approach | Trade-off | Decision |
| --- | --- | --- |
| Embed Troi as the recommendation runtime | Strong music pipeline; additional runtime/dependencies, MBID resolution and packaging/license work remain. | Reference and optional comparison oracle only. |
| Rank anonymous YouTube discovery shelves | Fast, but the public feed supplies weak evidence of the selected profile's tastes and would let a provider define the brain. | Keep as clearly non-personal fallback, not the main algorithm. |
| Extend adopted BE1/BE2/BE3 with native orchestration and qualified evidence adapters | Requires identity and history host work; keeps the existing owner boundaries and explainable policy. | Recommended. |

## Ownership and proposed file boundaries

- Existing `native/account/ProfilePreferencesStore.*`: onboarding answer/typed seed selections, explicit BE2 feedback and bounded recommendation-exposure preferences. No new personal settings store.
- Existing `native/account/{ActivityStore,ActivityProjector,ConsumptionHistoryBridge,HistoryStore}.*`: retained listening facts and history projections. Extend this seam only where factual music actions are missing; preserve other media and synchronization behavior.
- Existing `native/vinyl/{VinylProvider.h,VinylCatalogueSource.*,VinylBackendSession.*}`, `scripts/vinyl/provider.py`: transport, catalogue/identity enrichment and player facts. Existing provider cache remains public metadata only.
- Proposed `native/vinyl/VinylRecommendationService.{h,cpp}`: native profile-scoped orchestrator and read model. Owns transient requests and derived state, not another durable database.
- Proposed `native/vinyl/VinylRecommendationIdentity.{h,cpp}`: pure validation/conversion between provider refs, verified canonical refs and existing catalogue destinations; no independent identity store.
- Proposed `native/vinyl/VinylRecommendationHistory.{h,cpp}`: read-only bounded Activity/History projection into BE inputs; no event writer or second ledger.
- Existing `native/vinyl/backend/{be1-recommendation-candidate-prototype,be2-feedback-taste-state-prototype,be-derivation-prototype,be3-deterministic-ranker-prototype,bf1-context-graph-prototype,bg2-discovery-enrichment-prototype}`: retain algorithms and contracts; extend rather than implement a QML ranker.
- Existing `qml/vinyl/{VinylTastePicker,VinylHomePage,VinylLiveWorldData,VinylLiveWorld,VinylShelf,VinylItemCard}.qml`: presentation and actions. Existing `native/main.cpp` registers the service; the world supplies its active session/stores.

## 1. Artist onboarding

State: `unanswered -> draft -> answered`; answered may enter `editing -> answered`. Startup never marks an answer automatically. Only a successful atomic Done/Skip commit marks it answered.

- Show the picker in Home on the first meaningful visit after the profile is ready. Discover, Library and playback remain reachable; this is not an application modal.
- Suggested artists come from available public artist data plus the profile's library/saves, deduplicated by typed identity. Search reaches the full supported YouTube artist search, not only local imports. Missing network never disables Done/Skip.
- No minimum artist count. Existing 512-selection storage cap is a technical validation limit, not a requested quota. Selected cards persist in the draft across result pages and query changes.
- Use the existing top search icon/field, scoped to artists while the picker is open; retain the draft and return to it. Do not add a persistent second Home search bar. Retain ordinary search/Back routes elsewhere.
- Done commits selected artist references and answer state together. Skip commits an empty answered state. Failed storage leaves the draft intact and visibly retryable.
- Later Edit opens the saved selection as a draft. Cancel/Back restores the saved selection; merely entering edit does not erase it. Reset is an explicit action; it does not reopen automatically after a history clear.
- Migrate existing `vinylTasteAnswered` / IDs without re-prompting answered profiles. Unresolved legacy IDs remain preserved for resolution; do not silently discard them.
- Icons carry accessible names for Done/Skip/Edit/Cancel and feedback. Artist names, headings and navigation labels remain readable. No tooltips, source-selection UI, automatic Back highlight or design-explanation paragraphs.

## 2. Identity and catalogue enrichment

The recommendation key is a typed union: verified `CanonicalEntityRef` or namespaced `ProviderItemRef`, with an explicit entity kind. A generated application UUID is never a MusicBrainz ID. Keep existing MBID APIs compatible; version persistence and migrate prior canonical snapshots losslessly.

Keep public mapping evidence in the existing catalogue cache and metadata payloads: provider, entity kind, provider ID, optional verified MBID, evidence/method, fetched time, status (`verified`, `unresolved`, `ambiguous`, `unavailable`). Do not create synthetic equivalence from title alone.

Reuse the current artist mapping/artwork paths but separate bounded identity lookup from full-discography enrichment. Preserve collaborators as independent IDs. Enrichment adds available biography/artwork, release group/version/date/type and structured artist/recording credits/relationships without changing the original playable destination. Unknown genre, language, mood and roles remain unknown. No album cover becomes an artist photograph.

For recordings, prefer verified provider/external identifiers and exact artist/version/duration corroboration. A same-title cover, live performance, remaster or unrelated collaborator must not silently become the requested recording. Unresolved canonical candidates may open a verified artist/album destination; direct Play is withheld until an actual playable catalogue match exists.

Do not require MusicBrainz/ListenBrainz coverage for YouTube-only artists. Their own releases remain eligible for seed-based shelves. Provider-supplied relatedness retains provider provenance and is never represented as a canonical MusicBrainz relationship.

## 3. Candidates and initial ranking policy

Independent lanes: (a) releases/tracks from explicit seed/saved artists; (b) related artists/recordings from a qualified public source; (c) releases with verified dates; (d) personal library rediscovery; (e) qualified history/session candidates; (f) public editorial/chart discovery, kept identifiable as non-personal.

The first working proof uses seed and library lanes without external accounts. ListenBrainz population evidence is additive after a live capability/coverage gate; its failure cannot empty otherwise valid seed shelves. User-specific ListenBrainz recommendations and history upload are outside this plan.

Use existing BE1 merging and BE3 scoring. Initial configuration `vinyl-home-v1` inherits existing BE3 defaults for established features; add an explicit seed-affinity contribution of +300, at most once per distinct seed-to-item relation. It is a calibration starting point, not a measured optimum. Onboarding choice is distinct from an explicit like and is removed only from its own evidence lane on editing. Seed weight multiplier is 1.0 through 30 retained qualified manual play sessions, 0.75 through 100, then 0.5; use retained session evidence only and retain 1.0 when history is disabled/unknown. A history reset resets that derived count.

Bound each refresh to 300 unique candidates, 60 per generator and 12 displayed items per shelf. The selected-artist shelf rotates deterministically through all selections, requests at most six uncached artists per refresh, and advances its cursor only on explicit refresh/new seed revision. Cached remaining artists still contribute. These are configurable resource limits; a selection outside the current request batch is not dropped from preferences.

Stable ordering uses config version, input snapshot, explicit evaluation time and stable item key. Rerendering does not randomize results or issue more requests. Different provider scores enter only through explicit calibration; missing scores get source support, not invented popularity. At most three cards per primary artist and two recordings per confirmed release group in a 12-item mixed discovery shelf; exact-artist shelves use the release-group cap without an artist cap. If the input is thin, return fewer valid items.

BE2 suppression is applied before final selection, including its scoped artist context. Deduplicate confirmed equivalents while retaining every contributor's evidence. Queue/playlist duplicates are untouched. Do not rank down a Telugu/Indian artist because ListenBrainz has no match. General discovery reserves up to three of twelve slots for evidenced adjacent candidates when available; the remaining pool stays taste-related. No quota is filled with unrelated global hits.

## 4. Behaviour, privacy and explanations

Collection remains the authority for saves; Preferences for explicit choices/feedback; Activity for durable listening facts; History for derived chronology; Progress for resume. A saved artist is a separate positive source and must survive removal of an onboarding seed.

Current BA4 events are transient. Do not persist a parallel BA4 log to manufacture history. Use qualified Activity sessions and extend the existing Activity seam for missing music start/intent/outcome facts. Store manual skip only when the user deliberately advances a loaded recording; failure, buffering, seek, shutdown and replacement are separate outcomes. Repeated early skips may influence ranking; one skip is not a dislike. Rendering/hover does not become positive taste, and provider failures never become negative taste.

History-derived features and exposure obey `keepActivityHistory`, reset generation and profile lifetime. With history disabled, use explicit tastes/saves and public evidence, retain no behavioural/exposure trail, and never claim lifetime unseen music. Clearing history invalidates derived rows and cached personal snapshots without clearing explicit selections/likes.

An external seed lookup reveals at least the selected artist/recording identifier to that provider. Do not promise that no identifiers leave the device. Do not send profile identity, raw history, private playlists or batches of personal events. Keep aggregate public metadata cache separate from profile-derived Home state.

Every ranked card retains machine-readable reasons (generator, seed, evidence, config version) and a concise optional shelf-level reason. Use existing detail/context affordances for explanation access; no hover descriptors or permanent explanation paragraphs. A reason must agree with its evidence: seed-only means selected artist, not listened/replayed. Exposure is recorded only for a visible recommendation, at most once per item/shelf snapshot per visit, never from a hidden model.

## 5. Home composition and navigation

Order: Continue listening; From your artists; Related discoveries; New from your artists; saved music/playlists; Rediscover your library. Hide empty sections. Deduplicate within a shelf and avoid repeating an item across adjacent recommendation shelves where alternatives exist. Factual saved/resume rows remain independent.

"New" requires a trustworthy date; unknown dates remain ordinary releases. "Rediscover" requires retained prior playback; low-count library rows use "Less played" rather than lifetime claims. If the user skips and has no library/history, Home offers clearly non-personal public discovery and existing Discover/Library destinations.

Reuse existing typed card actions: artist -> Artist; release -> Album; recording -> browsed Song; playlist -> Playlist. Play/Next/Queue use the existing session only after playable identity is established. Back restores shelf/scroll/selection, including after an artist/search detour. Recommendation refresh never replaces the playing queue.

Each remote shelf exposes `loading`, `ready`, `empty`, `error` or `stale`, fetched time, reason and retry capability. Keep successful shelves during partial failure. Profile switch cancels requests, clears drafts/derived state immediately and rejects late responses.

## 6. Request and cache policy

Initial proposed limits: 300 ms artist-search debounce; minimum two query characters for remote search (explicit submit can search one); 20 artist results/page; two concurrent remote candidate requests; background shelf refresh deadline eight seconds. Cache-first presentation never waits for remote completion. Existing transport/process ceilings remain hard upper bounds, not the target Home latency.

Reuse the existing metadata cache namespace with versioned envelopes, entity/provider/request keys and bounded eviction: public cache total 128 MiB; individual entries remain at most the existing 4 MiB limit. Suggested freshness: artist metadata seven days; similar/population candidates 24 hours; dated release candidates six hours; confirmed no-match one hour. Transport failures are retryable, not negative identity cache entries. Honor provider Retry-After/rate limits; no immediate retries inside an eight-second refresh after rate-limit exhaustion. Stale public evidence can remain browseable with stale status; raw audio URLs are never cached as recommendations. Derived personal output is memory-only initially.

## Acceptance / completion

The implementation plan must demonstrate fresh/Skip/Done/edit/restart, disjoint artist selections producing appropriately different seed-related content, a YouTube-only seed, mixed Telugu/Indian/Western selections, ambiguous recording identity, unavailable providers, history disabled/cleared, rapid query/profile changes and unchanged playback. Automated determinism/identity tests and semantic Main journeys are separate from physical input or subjective recommendation-quality acceptance. Source inspection alone closes none of those runtime gates.
