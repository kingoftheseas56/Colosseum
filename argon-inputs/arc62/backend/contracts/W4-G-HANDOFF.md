# W4-G Recommendation Inputs + BE3 Handoff

AGENT: Agent-7
TASK: T-7 / W4-G Recommendation Inputs + BE3
STATE: DONE
DATE: 2026-10-06

## COMPLETED

- Widened BE1 RecommendationCandidate key from RecordingMbid to exact CanonicalEntityRef restricted to Recording, ReleaseGroup, and Artist.
- Preserved exact canonical dedup and all generator/provider/raw-score provenance.
- Added direct-playability law: only Recording is directly playable.
- Added pure versioned BA4 + EnqueueOrigin -> BE2 behavioral derivation.
- Added deterministic per-Recording history aggregates.
- Added profile-scoped versioned recommendation exposure records/log.
- Added deterministic BE3 with injected clock/config/weights/calibrations.
- Added deterministic suppression, explicit-taste, history, exposure/fatigue, BF1 relationship, BG2 discovery, exploration, and diversity policy.
- Added structured traceable explanation evidence.
- Kept BE4 radio/autoplay, network fetch, source resolution, QML, and personal-store mutation out of BE3.

## VERIFIED

Qt 6.11.1 / MSVC 19.44 native verification:

- BE1 clean configure/build: PASS.
- `vinyl_recommendation_candidate_harness`: 1/1 PASS.
- BE-DERIVATION clean configure/build: PASS.
- `vinyl_be_derivation_harness`: 1/1 PASS.
- BE3 configure/build: PASS after one local type-mismatch repair.
- `vinyl_be3_ranker_harness`: 1/1 PASS.

The first BE3 compile attempt exposed only three `qsizetype`/int `std::min` mismatches. Those exact sites were corrected; no architecture or policy change was required.

## PROVEN ACCEPTANCE

- Recording / ReleaseGroup / Artist candidates accepted.
- Release / ReleaseTrack / Work candidates fail closed.
- Canonical dedup preserves multiple generators.
- Raw scores do not rank unless the exact generator has explicit Vinyl calibration.
- User-origin and radio-origin plays of the same Recording derive different behavioral meaning.
- Early skip is distinct negative evidence.
- Unsupported queue-removal / recommendation-avoidance / album-completion signals are not fabricated.
- Hide / snooze / do-not-recommend suppress deterministically.
- Explicit positive and negative remain distinct from behavior.
- Missing evidence contributes nothing rather than fake neutral certainty.
- Exposure drives fatigue but never positive taste.
- Deterministic tie-breaking uses canonical identity.
- Candidate/provider input permutation produces identical output.
- Bounded diversity and exploration are deterministic.
- BF1 and BG2 evidence is consumed only when actually supplied.
- Structured explanation evidence is stable and traceable.
- Two complete ranking runs produce identical structured output.

## CHANGED

- BE1-RECOMMENDATION-CANDIDATE-CONTRACT.md
- be1-recommendation-candidate-prototype/
- BE-DERIVATION-HISTORY-EXPOSURE-CONTRACT.md
- be-derivation-prototype/
- BE3-DETERMINISTIC-RANKER-CONTRACT.md
- be3-deterministic-ranker-prototype/
- W4-G-HANDOFF.md
- append-only WAVE4-AGENT-ROOM.md messages

## UNRESOLVED

None inside W4-G.

Production Colosseum adoption is not claimed. Exposure database placement/sync is not frozen by this reference lane. W4-H remains the composed integration seal and may still expose cross-lane integration defects.

## NEXT

Lead should independently inspect W4-G source, contracts, and native test evidence. If accepted, T-8 / W4-H may consume W4-G but Agent-7 does not start Slice 8.

## IMPORTANT EVIDENCE

- W4-D-HANDOFF.md and EnqueueOrigin public interface.
- W4-E-HANDOFF.md and BA4/BE2 public interfaces.
- W4-F-HANDOFF.md catalogue public interface.
- BE1 clean harness 1/1 PASS.
- BE-DERIVATION clean harness 1/1 PASS.
- BE3 harness 1/1 PASS.
