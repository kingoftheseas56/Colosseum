# Vinyl Backend BE-DERIVATION - History, Aggregates, and Exposure Contract

Status: W4-G reference contract implemented and locally verified
Date: 2026-10-06
Owner: Arc 62 Vinyl W4-G
Depends on: BA0, BA1 EnqueueOrigin, BA4, BE2

## Purpose

BE-DERIVATION is the pure recommendation interpretation layer between factual listening history and recommendation/taste inputs.

BA4 remains factual history. BE2 remains private taste state. BE-DERIVATION owns only deterministic, versioned conversion of facts into recommendation observations and aggregates.

## Laws

1. Derivation is pure. It performs no network access and mutates no BA4, BE2, queue, catalogue, source, or playback state.
2. Derivation version is explicit. Reference version: `kRecommendationDerivationVersion = 1`.
3. ProfileScopeId must remain consistent end to end.
4. Missing provenance stays unknown. Absence of an event is never converted into taste.
5. Same-occurrence BA4 continuation does not increment play count or fabricate replay.
6. Recording identity remains the BA0 canonical RecordingMbid represented as CanonicalEntityRef.
7. Wall time is descriptive input. BA4 sequence owns event order; the derivation does not reorder factual history.

## Inputs

The reference function consumes:

- one BA4 ListenEventLedger;
- QueueOccurrence.EnqueueOrigin already frozen by W4-D;
- BA4 PlayInstanceId continuation links;
- an injected RecommendationDerivationConfig with version, ranking-time reference, recent window, and early-skip threshold.

The derivation never queries a provider or guesses missing queue provenance.

## Behavioral observations

Version 1 can derive only evidence actually present in the frozen facts.

- `deliberate_queue`: Start with EnqueueOrigin=user.
- `replay`: a later non-continuation deliberate user start of the same Recording inside the ledger.
- `natural_completion`: Completion with explicit user, album, or playlist origin.
- `passive_autoplay_completion`: Completion with explicit radio origin.
- `early_skip`: Skip at or before the configured early-skip position.

The same Recording with `EnqueueOrigin=user` and `EnqueueOrigin=radio` therefore produces different behavioral meaning.

## Signals intentionally not manufactured

Version 1 emits none of the following unless a future factual contract supplies enough evidence:

- `album_completion`: BA4 plus EnqueueOrigin does not prove that every member of an album was heard.
- `queue_removal`: BA4 has no queue-removal lifecycle fact.
- `recommendation_avoidance`: not listening is not proof that a shown recommendation was actively avoided.
- `library_save` and `playlist_add`: these belong to factual BA5/user actions, not BA4 listen history.

Failing to emit an unsupported signal is correct under the Vinyl unknown-evidence law.

## Deterministic entity history aggregates

For exact Recording CanonicalEntityRef keys, version 1 derives only ranker inputs supported by BA4:

- playCount;
- lastPlayedAtMs;
- recentCompletionCount;
- recentSkipCount;
- naturalCompletionCount;
- earlySkipCount;
- passiveAutoplayCompletionCount.

Output aggregates are canonical-key ordered.

Explicit positive/negative state and hide/snooze/do-not-recommend are not redundantly copied into an aggregate. BE2 already owns those authoritative facts and BE3 consumes BE2 directly.

## Exposure record

Recommendation exposure is a separate factual plane.

`RecommendationExposureRecord` schema version 1 contains:

- ProfileScopeId;
- RecommendationExposureId;
- exact candidate CanonicalEntityRef;
- recommendedAtMs;
- one-based rankPosition;
- rankingConfigVersion;
- structured ExposureEvidenceRef[].

Allowed exposure entities are the same BE1 kinds: Recording, ReleaseGroup, Artist.

An exposure means Vinyl showed/recommended the entity. It is never converted into positive taste or listening history.

## Exposure provenance

ExposureEvidenceRef carries:

- typed evidence kind;
- bounded sourceId;
- optional related CanonicalEntityRef.

Current evidence kinds cover candidate generator, explicit taste, behavioral history, history aggregate, canonical relationship, catalogue context, discovery evidence, diversity, exploration, and fatigue policy.

This supports later "Why this?" reconstruction without storing presentation prose.

## Exposure log semantics

RecommendationExposureLog is profile scoped.

- first exposure ID + payload appends;
- exact replay is an idempotent no-op;
- same exposure ID + different payload fails closed;
- another profile fails closed.

The prototype freezes record semantics, not a production database location or sync transport.

## Reference prototype

Path: `backend/be-derivation-prototype/`

Files:

- RecommendationDerivation.h/.cpp
- test_recommendation_derivation.cpp
- CMakeLists.txt

## Native acceptance

The Qt 6.11.1/MSVC harness proves:

- deliberate user queue and radio/passive completion differ for the same Recording;
- replay is derived only from explicit repeated deliberate starts;
- early skip remains distinct negative evidence;
- play/recent completion/recent skip aggregates are deterministic;
- unsupported album-completion, queue-removal, and recommendation-avoidance signals remain absent;
- exposure is profile scoped, versioned, idempotent, conflict checked, and does not create taste observations.

Production Colosseum adoption is not implied by this reference prototype.
