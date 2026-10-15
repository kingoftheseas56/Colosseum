# Vinyl Backend BE2 - Feedback + Taste State Contract

Status: W4-E hardened and verified, including revision ordering and durable schema v2; production Colosseum adoption pending  
Date: 2026-10-06  
Owner: Arc 62 Vinyl  
Depends on: frozen BA0 Domain Contract and BE1 ownership boundary  
Reads BA5 only for profile-scoped personal-state precedent

## Purpose

BE2 defines Vinyl-owned, presentation-independent personal taste state for one host profile.

It records what the user explicitly said, what behavioral evidence the host observed, and three independent recommendation-suppression instructions. It does not decide a final recommendation score, order, diversity policy, radio sequence, or autoplay queue.

BE2 is the private policy-input plane consumed later by BE3.

## Basis inspected

Arc 62 authority:

- `backend/README.md`;
- `backend/BA0-DOMAIN-CONTRACT.md`;
- `backend/BE1-RECOMMENDATION-CANDIDATE-CONTRACT.md`;
- `backend/BA5-LIBRARY-PLAYLISTS-CONTRACT.md`;
- `recommendations/MUSIC-RECOMMENDATION-BRAIN.md`;
- `donor-reviews/listenbrainz/DONOR-ASSESSMENT.md`;
- `donor-reviews/music-assistant/DONOR-ASSESSMENT.md`.

Relevant prior decisions:

- MusicBrainz owns canonical music identity.
- Colosseum owns personal state and final policy.
- BE1 supplies candidate evidence, not local feedback or final ranking.
- BA5 establishes the precedent that personal state is explicitly profile-scoped.
- ListenBrainz is useful prior art for love/hate, recommendation feedback, and do-not-recommend, but Vinyl-private state remains authoritative.
- Behavioral actions are evidence, not automatic declarations of taste.

## BE2 laws

1. Every taste record is keyed by a BA0 `CanonicalEntityRef`.
2. Provider IDs and provider feedback IDs are provenance only and can never replace canonical identity.
3. Personal taste state belongs to one explicit host profile scope.
4. Explicit user feedback and behavioral observations are different state planes.
5. Behavioral evidence never silently becomes explicit positive or negative feedback.
6. Explicit positive, negative, and neutral states are deterministic versioned mutations.
7. Neutral is a retained tombstone, not deletion of mutation history.
8. Hide, snooze, and do-not-recommend are three independent state dimensions.
9. Changing one suppression dimension never implicitly changes another.
10. Suppression state never implicitly changes explicit feedback.
11. Snooze is time-bounded and requires an explicit expiry.
12. Do-not-recommend is durable until explicitly cleared.
13. Behavioral observations are retained as typed evidence without BE2 weights or scores.
14. Replaying one behavioral observation is idempotent only when its full payload agrees.
15. Reusing a behavioral observation ID for different evidence fails closed.
16. Versioned explicit/suppression mutations use host-supplied positive per-dimension revisions for conflict order; descriptive wall time is retained but is never ordering authority.
17. BE2 does not call wall-clock APIs. The host supplies mutation revisions and descriptive mutation/observation time.
18. BE2 does not upload private taste state or require an external account.
19. BE2 does not implement final scoring, ranking, diversity, fatigue, exploration, radio, or autoplay.
20. QML is a consumer only. No current mock or presentation shape defines BE2 state.

## Profile scope

### ProfileScopeId

BE2 consumes the shared BA0 `ProfileScopeId`; it no longer defines a lane-local taste-profile identity.

Rules:

- non-empty;
- maximum 128 characters;
- no surrounding whitespace;
- no control characters;
- case preserved;
- not a provider identity;
- not a MusicBrainz identity;
- not converted into a filesystem path by BE2.

The shared type remains host profile scope only, never provider, MusicBrainz, account, or filesystem identity.

One `TasteProfileState` instance is bound to exactly one `ProfileScopeId`.

Two profiles can therefore hold different feedback and suppression state for the same canonical entity without conflict.

## Canonical entity key

The key for all entity taste state is:

```text
CanonicalEntityRef
  kind
  MBID
```

BE2 accepts BA0 canonical entity kinds without inventing a second music identity system.

A provider feedback record, provider user ID, title string, artist label, recommendation-generator ID, or queue occurrence cannot become the key.

Two canonical entities remain distinct even when they carry the same provider feedback provenance.

## Explicit taste feedback

### ExplicitTasteFeedback

BE2 freezes three explicit values:

- `positive`;
- `negative`;
- `neutral`.

The first two are direct user polarity.

`neutral` means the explicit polarity was intentionally cleared. It remains a versioned tombstone so an older positive or negative mutation cannot later resurrect by arriving out of order.

### ExplicitTasteFeedbackState

Per canonical entity:

```text
ExplicitTasteFeedbackState
  feedback
  revision
  changedAtMs
  providerFeedbackRef?
```

`providerFeedbackRef` is optional interoperability provenance.

For example, a future opted-in ListenBrainz connector may retain an upstream feedback record reference. That reference does not participate in canonical lookup, deduplication, or identity equality.

A newer explicit mutation may replace or clear older provider provenance because provenance describes that explicit mutation, not the canonical entity.

BE2 does not perform the external write.

## Behavioral evidence

### BehaviorObservationId

A behavioral observation has a host-owned opaque deduplication ID.

Rules mirror profile-scope safety:

- non-empty;
- maximum 128 characters;
- trimmed;
- no control characters;
- profile-local;
- not a provider item ID.

The ID is ingestion identity for one observation. It is not music identity and does not become a recommendation candidate key.

W4-H reference integration now proves the BA4 factual ledger -> versioned recommendation derivation -> BE2 behavioral-signal ingestion path (`verifyRecommendationComposition`) without mutating BA4. What remains is production wiring from Colosseum's host event/history owner into that proven derivation seam; BE2 still does not own BA4.

### BehavioralSignalObservation

One observation contains:

```text
BehaviorObservationId
CanonicalEntityRef
BehavioralSignalKind
occurredAtMs
```

`occurredAtMs` must be positive.

BE2 freezes these evidence kinds:

- `replay`;
- `deliberate_queue`;
- `natural_completion`;
- `album_completion`;
- `library_save`;
- `playlist_add`;
- `early_skip`;
- `queue_removal`;
- `recommendation_avoidance`;
- `passive_autoplay_completion`.

These are observations, not permanent weights.

A replay and an early skip may both exist for the same canonical entity. BE2 preserves both. BE3 later decides how, whether, and under what context to interpret them.

This directly preserves the recommendation-brain warning: do not treat every click or one ambiguous action as taste.

### Behavioral deduplication

Inside one profile:

- first observation ID + payload inserts;
- exact replay of the same ID + same payload is a successful no-op;
- same observation ID with any different canonical entity, signal kind, or timestamp fails as `conflicting_behavior_observation`.

No arrival-order guess breaks that tie.

## Recommendation suppression

BE2 stores three independent dimensions.

### Hidden

`hidden` is a direct user-controlled marker that an entity is hidden from recommendation consumption until the marker is cleared.

State:

```text
hidden
hiddenRevision
hiddenChangedAtMs
```

BE2 records the instruction. It does not implement the later candidate-filtering or ranking policy.

### Snoozed until

`snoozedUntilMs` is optional.

State:

```text
snoozedUntilMs?
snoozeRevision
snoozeChangedAtMs
```

A present expiry must be strictly later than the mutation timestamp.

The pure query:

```text
isSnoozedAt(atMs)
```

returns true only while `atMs < snoozedUntilMs`.

No background timer mutates BE2 state when the deadline passes. Clearing the snooze is an explicit later mutation.

### Do not recommend

`doNotRecommend` is a durable explicit instruction.

State:

```text
doNotRecommend
doNotRecommendRevision
doNotRecommendChangedAtMs
```

It has no implicit expiry in BE2. Clearing it requires an explicit later mutation.

This is intentionally distinct from snooze. ListenBrainz's optional-expiry do-not-recommend concept is represented in Vinyl as two clearer local states: durable do-not-recommend and time-bounded snooze.

### Independence

The three suppression dimensions may coexist.

For one entity it is legal to have:

```text
hidden = true
snoozedUntilMs = <future time>
doNotRecommend = true
```

Clearing one leaves the other two untouched.

Suppression mutations also leave explicit feedback and behavioral observations untouched.

## Deterministic mutation ordering

Every versioned explicit/suppression dimension has its own positive host revision plus descriptive changedAtMs.

For one dimension:

1. no prior mutation: accept a positive revision;
2. higher revision: replace current value even when descriptive wall time moved backward;
3. lower revision: reject with `stale_mutation`;
4. same revision + identical value/provenance and identical descriptive timestamp: successful no-op;
5. same revision + any disagreement: reject with `conflicting_mutation`.

The rule is fail-closed because arrival order and wall-clock adjustments must not decide conflict order.

Explicit feedback compares polarity, optional provider provenance, and descriptive timestamp for equal-revision idempotence. Snooze compares optional expiry and descriptive timestamp. Hidden and do-not-recommend compare their booleans and descriptive timestamp.

Revisions are local host sequencing facts, not a distributed clock or cross-device merge protocol.

## Privacy and ownership

BE2 state is local/private host-owned personal state.

The core contract has:

- no network dependency;
- no authentication dependency;
- no ListenBrainz login requirement;
- no upstream feedback submission;
- no provider-user identity;
- no raw-history upload;
- no QML dependency.

A future connector may mirror explicit feedback only under separate opt-in and connector policy. External success or failure cannot redefine the local canonical entity or local taste owner.

## Provider boundary

Provider feedback identity is optional provenance:

```text
ProviderId + ProviderItemId
```

It never becomes:

- a `CanonicalEntityRef`;
- a `RecordingMbid`;
- a profile scope;
- a behavioral observation ID;
- a recommendation score;
- a deduplication key for entity taste state.

The same provider feedback reference can therefore appear on two test entities without merging them. Canonical MBID identity remains authoritative.

## Downstream ownership

### BE1 - candidate evidence

BE1 owns provider-neutral recommendation candidates and contribution evidence.

BE2 does not mutate BE1 candidates or reinterpret upstream scores.

### BE3 - deterministic Vinyl ranker

BE3 may consume BE2 state.

BE3, not BE2, owns:

- signal weighting;
- score calibration;
- affinity derivation;
- final score;
- final ordering;
- suppression application policy;
- diversity;
- fatigue;
- exploration;
- explanation synthesis.

BE2 deliberately exposes facts/state without implementing those decisions.

### BE4 - radio/autoplay

BE4 owns recommendation sequence/session construction after BE3.

BE2 never creates a radio sequence or BA1 queue.

### BA4 - listen event ledger

BA4 owns playback/listening-history facts.

BE2 may later receive selected normalized behavioral observations from host-owned history processing, but it does not replace the ledger and does not infer event semantics from raw playback callbacks.

### BA5 - Library + Playlists

BA5 owns saved membership and ordered playlist state.

BE2 may record `library_save` or `playlist_add` as behavioral evidence supplied by the host without taking ownership of BA5 membership.

## Typed BE2 failures

Stable snake_case failures:

- `none`;
- `invalid_behavior_observation_id`;
- `invalid_timestamp`;
- `invalid_revision`;
- `invalid_snooze_until`;
- `stale_mutation`;
- `conflicting_mutation`;
- `conflicting_behavior_observation`.

Malformed canonical MBIDs and provider IDs remain BA0 errors before BE2 state construction.

## Explicit non-goals

BE2 does not implement:

- a recommendation candidate generator;
- final Vinyl score;
- final recommendation rank/order;
- signal weights;
- affinity-vector math;
- diversity;
- fatigue;
- exploration;
- recommendation explanation synthesis;
- radio sequencing;
- autoplay;
- BA1 queue mutation;
- playback;
- source resolution;
- production file/database placement;
- cloud sync or distributed conflict resolution;
- provider login;
- upstream feedback writes;
- QML;
- network requests;
- current mock surfaces.

W4-E freezes a local versioned persistence format for BE2 state. Production storage placement and sync transport remain adoption concerns.

## Durable taste-state format

W4-E freezes `FeedbackTastePersistence` as the versioned BE2 reference format.

- schema: `colosseum.vinyl.feedback-taste-state`;
- writer version: 2;
- reader accepts supported versions 1 through 2 and upgrades supported v1 state in memory;
- malformed JSON, profile mismatch, invalid state, unknown fields, and unsupported future versions return typed failures;
- entity records serialize canonically by CanonicalEntityRef;
- behavioral observations serialize canonically by BehaviorObservationId;
- explicit feedback, per-dimension revisions, descriptive timestamps, suppression state, optional provider provenance, and behavioral observations round-trip;
- equivalent state with different non-semantic insertion order emits identical compact JSON bytes.

The durable format keeps provider provenance non-authoritative and does not embed scoring/ranking policy.

## Prototype

Reference implementation:

`backend/be2-feedback-taste-state-prototype/`

Files:

- `FeedbackTasteState.h`;
- `FeedbackTasteState.cpp`;
- `FeedbackTastePersistence.h/.cpp`;
- `test_feedback_taste_state.cpp`;
- `test_feedback_taste_persistence.cpp`;
- `CMakeLists.txt`.

The prototype is C++17 and Qt Core only.

It compiles BA0 `VinylDomainTypes` as the canonical identity dependency. It does not link BE1, BA5, QML, networking, playback, or provider runtime code.

## Acceptance contract

BE2 is accepted when the native harness proves:

1. positive/negative/neutral explicit feedback remains keyed by canonical entity;
2. provider feedback provenance cannot merge two canonical entities;
3. higher-revision explicit feedback replaces lower-revision state even when descriptive wall time moves backward;
4. neutral remains a tombstone that blocks stale resurrection;
5. lower-revision explicit feedback fails closed;
6. equal-revision explicit disagreement fails closed;
7. exact replay of the same revision/value/timestamp is idempotent;
8. hide, snooze, and do-not-recommend can coexist;
9. each suppression dimension clears independently;
10. suppression never mutates explicit feedback;
11. snooze requires an expiry later than its mutation;
12. snooze expiry queries are deterministic at the boundary;
13. hide revision/conflict/idempotence rules are deterministic;
14. snooze revision/conflict/idempotence rules are deterministic;
15. do-not-recommend revision/conflict/idempotence rules are deterministic;
16. behavioral observations remain separate from explicit feedback, including an entity with no explicit user mutation;
17. positive and negative behavioral evidence can coexist without BE2 scoring;
18. exact duplicate behavior observation is idempotent;
19. reused observation ID with conflicting payload fails closed;
20. the same canonical entity can hold different state in two profiles;
21. strong types prevent provider identity from becoming profile or observation identity;
22. no final-score/rank/radio interface exists;
23. versioned BE2 persistence round-trips, rejects corruption/unknown fields, restores supported v1, and rewrites the current schema;
24. canonical persistence bytes do not depend on entity/behavior insertion order;
25. the prototype compiles and executes against exactly Qt 6.11.1;
26. core source contains no QML/network/mpv/queue/candidate/ranker dependency.

## Verification

Verified locally on 2026-10-06 with the exact Qt 6.11.1 MSVC kit at:

`C:/Qt/6.11.1/msvc2022_64`

Observed verification:

- CMake configured successfully with Visual Studio 2022 x64.
- MSVC compiler identified as 19.44.35223.0.
- Release build completed for BA0 dependency, BE2 core, BE2 persistence, and both harnesses.
- CTest passed `2/2`: `vinyl_feedback_taste_state_harness` and `vinyl_feedback_taste_persistence_harness`.
- Direct harness execution returned:
  `VINYL_BE2_FEEDBACK_TASTE_STATE_OK checks=72 qt=6.11.1`.
- Core forbidden-boundary scan returned zero matches for final score/rank, diversity, radio/autoplay, QML, network, mpv, queue, or recommendation-candidate dependencies.
- Production Colosseum remained read-only.
- W4-E consumes the frozen W4-D shared BA0 ProfileScopeId and does not modify the W4-D write-set; BE1 and BA5 remain read-only authority/context.

Production Colosseum adoption is a separate future state and must not be inferred from this prototype.
