# Vinyl Backend BB2 - Album Manifest + Exact Track Resolver

Status: BB2 complete locally; Qt 6.11.1 compiled and native-harness verified; production Colosseum adoption pending
Date: 2026-10-06
Owner: Arc 62 Vinyl

## Purpose

BB2 decides whether one track occurrence inside an album/source manifest is the exact track Vinyl asked to play.

It consumes the frozen BA0 identity vocabulary and BB1 source-candidate provenance without mutating either contract.

The boundary is:

    requested RecordingMbid
      + optional ReleaseTrackCoordinate
      + expected track evidence
      + BB1 source-candidate provenance
      + album/source manifest
        -> Vinyl-owned exact-track comparison
        -> exact / rejected / ambiguous / insufficient_evidence

BB2 does not resolve or require a playback URL.

## Product laws

1. RecordingMbid remains canonical playable-song identity.
2. ReleaseTrackMbid remains a distinct occurrence identity and never collapses into RecordingMbid.
3. A BB1 SourceCandidateKey is provenance only. It can never become canonical music identity.
4. Provider order is never acceptance authority.
5. Direct canonical evidence outranks release coordinates and inferred text.
6. Canonical contradictions fail closed.
7. Release/medium/track coordinates identify an occurrence only inside a concrete release context.
8. Text, filename, artist, album and duration are supporting evidence, not substitutes for canonical identity.
9. Ambiguity is a valid terminal result. Ties are never broken by provider order, manifest order, filename order, file size or probe completion order.
10. Unknown evidence stays unknown.
11. Metadata-provider identity and playback-provider identity remain independent.
12. Exact-track acceptance belongs to Vinyl, not to a source provider.

## Requested identity

RequestedTrackIdentity contains:

    RecordingMbid recordingMbid
    ReleaseTrackCoordinate? releaseTrackCoordinate
    TrackTextEvidence expectedText
    durationMs?

When a coordinate is supplied, its recordingMbid must equal the requested RecordingMbid.

The optional coordinate preserves:

- Release Group MBID;
- concrete Release MBID;
- optional Release Track MBID;
- Recording MBID;
- one-based medium position;
- one-based track position.

No field is converted into another identity type.

## Album/source manifest

AlbumSourceManifest contains:

- optional BB1 SourceCandidateKey provenance;
- zero or more CandidateTrackEntry values.

Each entry has a lane-local ManifestEntryId plus:

- optional Recording MBID;
- optional Release Track MBID;
- optional Release Group MBID;
- optional Release MBID;
- optional medium and track positions;
- title, artist, album and filename evidence;
- optional measured duration.

Manifest entry identity is not canonical identity.

The prototype canonicalizes entries by ManifestEntryId only for deterministic storage and inspection. That ordering is never used to break an evidence tie.

## Normalized text evidence

BB2 owns conservative text normalization for comparison:

- Unicode compatibility normalization;
- case folding;
- whitespace collapse;
- punctuation and separator normalization to spaces;
- no removal of meaningful words such as live, remix, acoustic, remaster, part, language or version labels.

The prototype stores both raw TrackTextEvidence and NormalizedTextEvidence.

Filename-derived matching is implemented in BB2-B1. A filename may support a structured match, but it can never override canonical conflict.

## Duration evidence

Expected and measured duration remain optional.

ExactTrackResolverPolicy freezes the initial neutral tolerance at 2000 ms, matching the Track Identity Contract.

Structured resolver duration rules:

- missing duration is not a contradiction;
- a difference inside the tolerance is supporting evidence;
- a material duration mismatch rejects an otherwise-qualified structured match;
- canonical exact identity survives a duration discrepancy because file/database duration can be stale or edition-specific.

## Evidence ladder

The resolver compares candidates categorically:

1. ReleaseTrackMbid exact;
2. RecordingMbid exact with no edition contradiction;
3. exact Release + medium + track occurrence;
4. structured metadata/filename evidence;
5. insufficient evidence.

The first three levels are verified by BB2-A. Level 4 is authored in BB2-B1 and awaits BB2-B2 native verification.

### Hard canonical contradictions

Reject a candidate when a supplied trustworthy field contradicts the request:

- Recording MBID mismatch;
- requested Release Track MBID mismatch;
- Release Group mismatch;
- concrete Release mismatch;
- medium mismatch;
- track-position mismatch.

A matching filename or title can never repair one of these contradictions.

### Release-coordinate exact

When canonical track IDs are absent, an entry can resolve exactly by occurrence when all of these are present and equal to the requested coordinate:

- Release MBID;
- medium position;
- track position.

Release Group MBID is additional compatibility evidence when present. A concrete Release MBID already identifies the edition, so its absence from candidate Release Group tags must not block an otherwise exact Release + medium + track occurrence.

This identifies the expected occurrence without inventing a Release Track MBID or Recording MBID.

## Outcomes

ExactTrackOutcome is:

- Exact;
- Rejected;
- Ambiguous;
- InsufficientEvidence.

An exact result returns one ManifestEntryId and the strongest ExactEvidenceStrength.

An ambiguous result returns no selected entry and preserves the competing entry IDs.

A rejected result means the available candidates were contradicted by authoritative request evidence.

An insufficient result means the manifest does not contain enough non-conflicting evidence to authorize exact playback.

## Reason codes

Candidate assessments and the final result expose stable reason codes, including:

- recording_mbid_exact;
- recording_mbid_conflict;
- release_track_mbid_exact;
- release_track_mbid_conflict;
- release_group_mbid_exact;
- release_group_mbid_conflict;
- release_mbid_exact;
- release_mbid_conflict;
- medium_position_exact;
- medium_position_conflict;
- track_position_exact;
- track_position_conflict;
- release_coordinate_exact;
- title_exact / title_mismatch;
- artist_exact / artist_mismatch;
- album_exact / album_mismatch;
- filename_title_exact;
- filename_medium_position_exact / filename_medium_position_mismatch;
- filename_track_position_exact / filename_track_position_mismatch;
- duration_within_tolerance / duration_mismatch;
- structured_metadata_exact;
- structured_evidence_missing;
- canonical_evidence_missing;
- equal_strongest_candidates;
- empty_manifest;
- no_candidate_reached_exact_gate.

The structured reasons never weaken the canonical gates. Canonical contradiction is evaluated first and cannot be repaired by text, filename, or duration evidence.

## Deterministic comparison

Resolution must not depend on provider or manifest ordering.

Candidates are assessed independently, then compared by categorical evidence strength.

If more than one candidate shares the strongest exact evidence and no stronger field distinguishes them, the result is Ambiguous.

Stable entry IDs may order diagnostics only. They never choose a winner.

## Boundary with BB1

BB1 answers:

> What source candidate did a provider discover?

BB2 answers:

> Which manifest track occurrence, if any, is proven to be the requested Vinyl track?

Therefore BB2 may carry BB1 SourceCandidateKey as provenance, but:

- it never parses SourceCandidateId as an MBID;
- it never promotes provider identity into Recording identity;
- it never treats provider ordering as evidence;
- it never needs a resolved URL.

A metadata provider may contribute evidence through later enrichment seams. It cannot choose or force a source provider and cannot authorize the exact match.

## Explicit non-goals

BB2 does not implement:

- BB3 CUE parsing or whole-album slicing;
- BB4 mpv playback;
- torrent networking;
- provider fetches;
- ffprobe runtime integration;
- live fingerprinting;
- resolved playback URLs or request headers;
- QML/UI.

## Prototype

Reference implementation:

backend/bb2-exact-track-resolver-prototype/

BB2-A files:

- ExactTrackResolver.h;
- ExactTrackResolver.cpp;
- test_exact_track_resolver.cpp;
- CMakeLists.txt.

The prototype uses Qt Core only and compiles against the frozen BA0 and BB1 prototypes without modifying them.

## Oversized-slice checkpoint

The current Preflight oversized-slice guardrail split BB2 into two independently verifiable implementation checkpoints.

### BB2-A - canonical/coordinate core

Implemented in this checkpoint:

- requested Recording identity;
- optional ReleaseTrackCoordinate;
- album/source manifest;
- candidate track entries;
- normalized title/artist/album evidence model;
- duration evidence plus 2000 ms policy model;
- explicit exact/rejected/ambiguous/insufficient outcomes;
- typed reason codes;
- direct canonical comparison;
- exact release/medium/track occurrence comparison;
- deterministic categorical candidate comparison;
- no playback URL surface;
- Release Track versus Recording type separation;
- BB1 candidate identity remains provenance only.

BB2-A harness acceptance:

1. exact MBID evidence wins deterministically;
2. exact release/medium/track coordinates identify the expected occurrence;
3. Release Track and Recording identity remain distinct;
4. BB1 candidate identity never becomes canonical identity;
5. canonical contradiction fails closed;
6. empty and weak evidence remain insufficient;
7. an equal canonical-strength tie is ambiguous;
8. no resolved playback URL exists in the contract.

### BB2-B1 - structured evidence implementation

Authored and statically reviewed in this checkpoint:

- messy but sufficient filename/metadata resolution;
- conservative filename track/disc prefix parsing;
- exact normalized title/artist/album structured gate;
- required requested track occurrence evidence;
- 2000 ms duration-tolerance support;
- duration-mismatch rejection;
- adversarial equal-plausibility structured ambiguity;
- provider-order reversal fixtures;
- required assignment tests 3 through 7.

BB2-B1 was authored in its implementation checkpoint and was subsequently compiled and executed in BB2-B2.

### BB2-B2 - build and native verification

Completed:

- Qt 6.11.1 configure/build of the updated resolver: passed;
- PATH-qualified native harness execution: passed;
- CTest after BB2-B1: 1/1 tests passed, 0 failed;
- adversarial review then found one wrong-disc filename edge that could otherwise pass structured matching;
- the resolver was tightened so explicit filename disc/track contradictions fail closed with typed mismatch reasons;
- a wrong-disc regression fixture was added;
- final Qt 6.11.1 rebuild: passed;
- final PATH-qualified native harness execution: passed;
- final CTest: 1/1 tests passed, 0 failed;
- no compile or harness assertion failure remained;
- BB2 final contract status promoted to complete locally.

No BA0 or BB1 change is required by BB2-A or BB2-B1.

## BB2-B1 static checkpoint

BB2-B1 was independently inspected before compilation, then verified by BB2-B2 native build and test passes.

Observed static evidence:

- all 32 ExactTrackReasonCode values have stable name-switch cases;
- required structured tests 3 through 7 are present in the native harness source;
- structured matching is represented by ExactEvidenceStrength::StructuredMetadata;
- duration mismatch has an explicit rejected path;
- filename-derived track position is represented separately from canonical Release Track and Recording identity;
- the prototype contains no QUrl playback surface;
- the prototype contains no ffprobe runtime integration;
- BB2-B1 writes were confined to BB2-owned contract/prototype files.

The broad PowerShell regex audit attempted during static review timed out and was discarded as evidence. Bounded repository searches were used instead and completed successfully.

BB2-B1 static review is now backed by the completed BB2-B2 compile, native harness, and CTest verification.

## BB2-A verification checkpoint

BB2-A final verification state:

- CMake configure: passed;
- exact Qt requirement: Qt6Core resolved from C:/Qt/6.11.1/msvc2022_64/lib/cmake/Qt6Core;
- compiler: MSVC 19.44.35223;
- BA0 support library: compiled;
- BB1 support library: compiled;
- BB2 resolver library: compiled;
- native BB2 harness executable: compiled and linked;
- first harness launch: blocked before main with Windows status 0xC0000135 because C:/Qt/6.11.1/msvc2022_64/bin was absent from PATH;
- PATH-qualified harness retry: passed;
- CTest: 1/1 tests passed, 0 failed;
- harness wall-clock under CTest: 0.03 seconds.

The initial 0xC0000135 was an environment-launch failure rather than a source or assertion failure. With the Qt runtime directory on PATH, the same compiled BB2-A harness passes. BB2-A is verified for the canonical/coordinate checkpoint.
