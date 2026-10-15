# Vinyl Backend BE3 - Deterministic Ranker Contract

Status: W4-G reference ranker implemented and locally verified
Date: 2026-10-06
Owner: Arc 62 Vinyl W4-G
Depends on: BE1, BE2, BE-DERIVATION, BF1, BG1, BG2

## Purpose

BE3 owns Vinyl's first deterministic recommendation policy.

It consumes already-normalized evidence and personal state. It does not generate radio/autoplay sessions, fetch network data, resolve playback sources, or mutate BA4/BE2/BG stores.

## Inputs

The reference function consumes:

- normalized BE1 RecommendationCandidateSet;
- BE2 TasteProfileState;
- BE-DERIVATION RecommendationDerivationOutput;
- BE-DERIVATION RecommendationExposureLog;
- optional BF1 ContextGraph;
- available BG1 Artist / ReleaseGroup / Recording features;
- available BG2 DiscoveryCollection evidence;
- injected DeterministicRankerConfig including clock, version, weights, bounds, and raw-score calibrations.

Taste, derived history, and exposure must share one ProfileScopeId.

## Configuration law

The ranking clock is injected as `config.nowMs`. BE3 never calls the wall clock.

All policy weights and bounds are explicit configuration fields. The config carries a required version string.

Equal complete inputs plus the same config produce the same output.

## Raw/provider scores

Raw BE1 scores are incomparable by default.

A raw score contributes to Vinyl rank only when DeterministicRankerConfig contains an explicit calibration for that exact RecommendationGeneratorId.

A calibration specifies:

- generator identity;
- multiplier;
- maximum absolute contribution.

Missing scores and uncalibrated scores contribute no calibrated-score component and produce no fake neutral/zero evidence.

## Suppression law

BE2 durable suppression is applied before scoring.

An entity is excluded while any of these is active at the injected ranking time:

- hidden;
- snoozed;
- doNotRecommend.

Suppressed canonical refs are returned separately in canonical deterministic order for auditability.

## Explicit taste

Explicit positive and explicit negative are distinct configured score components.

They remain stronger/different from inferred behavioral history under the default reference weights.

Neutral means no current explicit polarity component.

## History components

When a BE-DERIVATION aggregate exists, BE3 may use bounded configured components for:

- play count;
- recent completion count;
- early skip count.

Missing aggregate evidence contributes nothing.

BE3 does not infer missing plays/skips/completions.

## Exposure/fatigue

Recent exposure is counted only from actual RecommendationExposureRecord rows inside the configured exposure window.

Each retained recent exposure applies a bounded configured fatigue penalty.

Exposure is not positive taste and is not listening.

## BF1 relationship evidence

When BF1 contains a real canonical relationship between the candidate and another entity with explicit positive BE2 feedback, BE3 may apply a bounded relationship component.

The explanation source points to BF1 relationship/evidence identity. Relationship absence means unknown and adds nothing.

## BG1 catalogue features

BG1 Artist credit and BE1 canonical context are used only for bounded deterministic diversity keys.

- Artist candidates identify their own artist key.
- ReleaseGroup/Recording candidates use supplied canonical artist credit when available.
- Recording ReleaseGroup context remains optional and missing means unknown.

BE3 does not choose a representative Release or pretend a ReleaseGroup is directly playable.

## BG2 discovery evidence

A candidate's actual presence in a BG2 DiscoveryCollection may apply a bounded discovery-presence component.

BG2 sourceRank/sourceScore is not treated as a Vinyl score. The structured explanation retains provider, discovery kind, and evidence ID.

## Exploration

The reference exploration policy is intentionally small and deterministic.

An entity with no factual prior play aggregate receives one bounded exploration bonus.

There is no randomness, bandit model, learned model, or autoplay sequence.

## Diversity

Final top-N selection is greedy and deterministic.

After base scoring, a bounded penalty may apply for already-selected matching artist or ReleaseGroup keys.

The penalty is recomputed for each selection step from exact selected state and configured caps.

Diversity never changes canonical identity or BE2 state.

## Tie-breaking

Provider arrival order, candidate arrival order, contribution arrival order, and raw score magnitude without calibration are not tie-breakers.

Final equal-score ties resolve by:

1. CanonicalEntityKind numeric order;
2. canonical MBID string.

## Structured explanation contract

Each ranked result exposes typed RankExplanationEvidence entries.

Current evidence kinds include:

- candidate generator support;
- calibrated generator score;
- explicit positive;
- explicit negative;
- play count;
- recent completion;
- early skip;
- canonical relationship;
- discovery presence;
- exploration bonus;
- exposure fatigue;
- artist diversity adjustment;
- ReleaseGroup diversity adjustment.

Each item includes a traceable sourceId and exact integer score delta. Optional related canonical entity is preserved where useful.

BE3 emits evidence, not user-facing prose. Presentation may later render "Why this?" copy without fabricating claims.

## Candidate kind law

BE3 inherits BE1 exact CanonicalEntityRef candidates.

- Recording may be directly playable.
- ReleaseGroup and Artist are navigation/discovery entities.
- BE3 never resolves them into tracks or sources.

## Reference prototype

Path: `backend/be3-deterministic-ranker-prototype/`

Files:

- DeterministicRanker.h/.cpp
- test_deterministic_ranker.cpp
- CMakeLists.txt

## Native acceptance

The Qt 6.11.1/MSVC harness proves:

- explicit positive versus explicit negative;
- hide, snooze, and do-not-recommend suppression;
- missing raw score and uncalibrated incompatible raw scores remain non-comparable;
- calibrated score is explicit and bounded;
- early-skip penalty;
- exposure/fatigue;
- canonical deterministic ties;
- provider/candidate-order permutation invariance;
- bounded artist diversity;
- bounded exploration;
- BF1 relationship and BG2 discovery evidence;
- structured explanations remain stable;
- two complete runs produce identical structured output.

BE3 does not implement BE4 radio/autoplay, networking, source resolution, personal-store mutation, or QML.
