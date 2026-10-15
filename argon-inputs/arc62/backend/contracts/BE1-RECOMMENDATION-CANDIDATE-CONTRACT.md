# Vinyl Backend BE1 - Recommendation Candidate Contract

Status: BE1 candidate-evidence contract
Date: 2026-10-06
Owner: Arc 62 Vinyl

## Purpose

BE1 defines the presentation-independent boundary by which independent recommendation generators propose canonical music entities to Vinyl.

BE1 is a candidate evidence plane. It is not Vinyl's final ranker.

A recommendation generator may propose a canonical Recording, Release Group, or Artist with evidenced reasons, provenance, and an upstream score if one exists. It may not decide the final Vinyl score, final order, feedback state, diversity policy, or autoplay sequence. Only a Recording candidate is directly playable; Release Group and Artist candidates remain discovery/navigation entities.

## Basis inspected

BE1 was derived from:

- `backend/README.md`;
- `backend/BA0-DOMAIN-CONTRACT.md`;
- `recommendations/MUSIC-RECOMMENDATION-BRAIN.md`;
- `donor-reviews/listenbrainz/DONOR-ASSESSMENT.md`;
- `donor-reviews/listenbrainz/PROTOTYPE-VERIFICATION.md`;
- `donor-reviews/music-assistant/DONOR-ASSESSMENT.md`.

The implementation pattern follows the existing Arc 62 Qt Core backend prototypes and consumes BA0 without modifying it.

## BE1 laws

1. `CanonicalEntityRef` is the candidate key and is restricted to `Recording`, `ReleaseGroup`, or `Artist`.
2. A generator or provider proposes a candidate. It never becomes canonical identity authority.
3. Provider candidate IDs are opaque provenance only.
4. A raw/provider score is optional evidence. It is not a Vinyl score and does not imply final rank.
5. Missing score means unknown. BE1 does not invent zero, neutral, or fallback scores.
6. Every contribution retains the generator, source family, provider provenance when applicable, evidence records, and seed provenance when applicable.
7. Human-readable recommendation reasons must be backed by an evidence record. BE1 does not accept a naked unexplained reason string.
8. Multiple generators may propose the same canonical entity. BE1 deduplicates by exact `CanonicalEntityRef` equality while preserving every contributing evidence record.
9. Different canonical refs never merge because UUID text, titles, provider IDs, artist labels, scores, or reasons happen to match. Entity kind remains part of equality.
10. Optional Artist and Release Group context enrich a contribution without changing candidate identity.
11. Only `Recording` is directly playable. `ReleaseGroup` and `Artist` require their normal catalogue/navigation path; a Release Group still needs `representativeRelease()` before tracks can be queued.
12. BE2 `FeedbackTasteState` owns mutation of local/private taste and BE3 consumes that state for deterministic ranking; BE1 does not own or mutate taste state.
13. BE1 does not sort candidates by upstream score. First-seen candidate order is preserved through normalization.
14. BE1 is mock-agnostic and presentation-independent.

## Candidate source families

BE1 names only source families justified by the current recommendation architecture:

| Family | Meaning |
| --- | --- |
| `collaborative` | population/user-neighbor evidence such as collaborative filtering or co-listens |
| `graph` | canonical catalogue or creator/relationship graph evidence |
| `sonic` | audio-feature or embedding similarity evidence |
| `editorial` | human, canon, new-release, or curated discovery evidence |
| `session` | current-session context evidence |
| `history` | personal listening-history-derived candidate evidence |

These names describe where candidate evidence came from. They do not prescribe final weighting.

## RecommendationGeneratorId

A recommendation generator has a stable machine identity independent of canonical music identity.

`RecommendationGeneratorId` uses BA0's safe provider-token syntax and normalization, but remains a BE1-specific strong type. A generic `ProviderId` cannot silently become a recommendation generator ID.

Examples include a collaborative generator, a catalogue-graph generator, a sonic generator, or a local history generator.

## Canonical context

Each contribution may carry:

```text
RecommendationCanonicalContext
  artistMbid?
  releaseGroupMbid?
```

These fields explain relevant canonical context for the proposal. They are not deduplication keys and do not replace the candidate's required `CanonicalEntityRef`.

## Seed identity and context

A candidate contribution may retain an explicit `RecommendationSeed`:

```text
RecommendationSeed
  canonicalEntity
  context
```

The canonical entity is a BA0 `CanonicalEntityRef`. The context is a stable, nonblank machine-facing description of why that entity seeded the generator, such as `similar_recording`, `artist_radio`, or `session_anchor`.

Seed identity is provenance. It is never inferred from a reason string.

Seedless candidates are legal for generators such as global editorial or population-prior sources.

## Evidence-backed reasons

`RecommendationReasonEvidence` contains:

```text
kind
detail
relatedCanonicalEntity?
```

Evidence kinds are:

- `collaborative_prior`;
- `canonical_relationship`;
- `sonic_similarity`;
- `editorial_selection`;
- `session_fit`;
- `history_signal`.

The detail must be nonblank. It records what the generator can actually support, not a marketing sentence BE1 wishes were true.

For example, ListenBrainz model provenance can support "ListenBrainz collaborative prior" or "population similarity to this seed Recording." It does not by itself support a fabricated claim about a specific social cohort.

User-facing copy is outside BE1. Later presentation may render these evidence records into explanations without changing their provenance.

## Provenance

`RecommendationCandidateProvenance` contains:

```text
generatorId
sourceFamily
providerId?
providerCandidateId?
providerModelId?
seed?
```

Rules:

- `generatorId` and `sourceFamily` are required;
- provider identity is optional because Vinyl-owned local generators do not need a third-party provider;
- a provider candidate ID cannot exist without its provider ID;
- a provider model ID cannot exist without its provider ID;
- provider candidate IDs remain opaque BA0 `ProviderItemId` values;
- model/algorithm IDs are provenance only and never Vinyl policy;
- seed provenance is preserved when supplied.

## Raw/provider score

A `RecommendationContribution` may carry:

```text
rawProviderScore?
```

Rules:

- absence remains `unknown`;
- a present score must be finite;
- BE1 does not assume a universal score range;
- BE1 does not normalize scores from different generators;
- BE1 does not compare candidates by score;
- BE1 does not expose a final Vinyl score or rank.

This is deliberate because ListenBrainz ALS, similarity datasets, editorial sources, and future sonic models can have unrelated score semantics.

Score calibration and final weighting belong to BE3.

## RecommendationCandidate

The canonical candidate shape is:

```text
RecommendationCandidate
  canonicalEntity
  contributions[]
```

Each contribution contains:

```text
RecommendationContribution
  canonicalContext
  provenance
  rawProviderScore?
  evidence[]
```

At least one validated evidence record is required for every contribution.

A candidate begins with one contribution and may accumulate more only when the exact same canonical entity ref is proposed again.

## Canonical deduplication

`RecommendationCandidateSet::normalize()` deduplicates on exactly one key:

```text
CanonicalEntityRef(kind + MBID)
```

Normalization is stable:

1. the first occurrence of a canonical entity establishes its position;
2. later occurrences of that exact canonical ref append their contributions;
3. all generator/provider/score/reason/seed evidence survives;
4. no score-based reorder occurs.

The following are explicitly not deduplication keys:

- provider candidate ID;
- provider ID;
- title or display text;
- Artist MBID;
- Release Group MBID;
- source family;
- generator ID;
- evidence text;
- raw/provider score.

This permits one canonical entity to accumulate independent support without collapsing distinct entities that happen to look alike.

## Typed BE1 errors

BE1 defines lane-local invalid-candidate failures:

- `none`;
- `invalid_canonical_kind`;
- `invalid_generator_id`;
- `invalid_provider_provenance`;
- `invalid_provider_score`;
- `missing_evidence`;
- `invalid_evidence`;
- `invalid_seed_context`.

BA0 remains the owner of malformed/missing MBID and provider-item parsing errors. BE1 consumes already validated BA0 identities rather than duplicating BA0's error vocabulary.

## Downstream ownership

### BE2 - Feedback + Taste State

BE2 owns local likes/dislikes, hides, snoozes, explicit recommendation feedback, and durable/private taste state.

BE1 neither reads nor writes that policy state.

### BE3 - Deterministic Vinyl Ranker

BE3 owns:

- score calibration across generators;
- private affinity application;
- final deterministic ranking;
- diversity and fatigue;
- exploration policy;
- final explanation synthesis.

BE1 merely preserves the raw evidence BE3 will consume.

### BE4 - Radio / Autoplay Sessions

BE4 owns recommendation sequencing into sessions/autoplay and feeds the normal BA1 queue contract.

BE1 never manufactures a queue or autoplay order.

## Explicit non-goals

BE1 does not implement:

- recommendation feedback;
- user taste/profile mutation;
- final Vinyl scoring;
- final ranking;
- score calibration;
- diversity;
- repetition fatigue;
- exploration policy;
- radio sequencing;
- autoplay queue generation;
- user-facing "For You" surfaces;
- network calls;
- ListenBrainz login or authentication;
- upstream ListenBrainz writes;
- provider adapters;
- QML;
- playback/source resolution.

## Prototype

Reference implementation:

`backend/be1-recommendation-candidate-prototype/`

Files:

- `RecommendationCandidateContract.h`;
- `RecommendationCandidateContract.cpp`;
- `test_recommendation_candidate_contract.cpp`;
- `CMakeLists.txt`.

The prototype uses Qt Core only and compiles BA0's existing domain types as its canonical identity dependency.

## Acceptance contract

BE1 is accepted when the native harness proves:

1. Recording, Release Group, and Artist candidates are accepted as exact CanonicalEntityRef keys;
2. Release, Release Track, and Work candidate kinds fail closed with invalid_canonical_kind;
3. only Recording reports direct playability; Release Group and Artist remain navigation/discovery entities;
4. the same canonical ref proposed by multiple generators deduplicates while preserving multiple contributions/evidence sources;
5. identical UUID payloads under different canonical kinds remain distinct;
6. provider/raw score cannot become a final Vinyl rank and normalization does not score-sort;
7. explanation evidence survives normalization and missing optional scores stay unknown;
8. provider candidate ID never replaces canonical identity and matching noncanonical text never merges distinct canonical refs;
9. seed identity/context remains explicit provenance;
10. invalid generator/provenance/score/evidence/seed input fails with typed BE1 errors;
11. the prototype compiles against exactly Qt 6.11.1;
12. no network, QML, feedback, ranker, diversity, radio, or autoplay dependency exists.

Production Colosseum adoption is a later state and is not implied by this prototype.
