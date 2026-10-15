# Vinyl Backend BF2 - Related Video Mapping

Status: production-intent backend contract and native reference prototype
Date: 2026-10-06
Owner: Arc 62 Vinyl

## Purpose

BF2 maps one canonical RecordingMbid to provider video candidates while preserving the distinction between song identity and video identity.

The boundary is:

    canonical RecordingMbid
      -> provider-scoped video candidate
      -> explicit relationship kind
      -> relationship evidence + provenance
      -> deterministic qualification
      -> later presentation/playback policy

BF2 is presentation-independent. It does not define a Listening Room switch, rail, card, player, downloader, or provider search implementation.

## Governing ownership

BF2 consumes BA0 without changing it.

BA0 remains authoritative for:

- RecordingMbid as canonical playable-song identity;
- ProviderId as provider identity;
- ProviderItemId as opaque provider-owned item identity;
- the rule that provider identity never replaces MusicBrainz identity.

BB1 is a sibling source-candidate contract. Its two lessons carried into BF2 are that provider result order is not authority and provider-owned IDs remain provenance rather than canonical identity. BF2 does not turn videos into BB1 audio-source candidates.

## Product laws

1. RecordingMbid remains canonical even when a provider video ID looks globally unique.
2. A video relationship is a claim backed by evidence, not an identity substitution.
3. Provider search order, result order, popularity, and first-success order have no relationship authority.
4. Strong structured evidence and weak text similarity remain distinct states.
5. Unknown evidence stays unknown and cannot establish an official relationship.
6. Official music video and official audio/visualizer require explicit strong official evidence.
7. Live and alternate performances remain distinguishable from the canonical official music video.
8. Missing duration/title/channel facts remain absent.
9. Qualification is derived by BF2 from evidence. Callers do not supply an isOfficial or isQualified boolean.
10. Video mapping never selects an audio source, ranks playback sources, or changes canonical Recording identity.

## RelatedVideoCandidate

The normalized candidate contains:

    RecordingMbid recordingMbid
    RelatedVideoCandidateKey key
    RelatedVideoProvenance provenance
    RelatedVideoRelationshipKind relationshipKind
    RelationshipEvidence[] evidence
    RelationshipEvidenceStrength strongestEvidenceStrength
    RelatedVideoQualification qualification
    RelatedVideoFacts facts

The canonical Recording is always stored separately from provider video identity.

## Provider video identity and deterministic candidate identity

A video itself is identified at the provider boundary by:

    ProviderId + ProviderItemId

The BF2 mapping candidate is identified by:

    RecordingMbid + ProviderId + ProviderItemId

This matters because one provider video can be discussed in relation to multiple recordings, and two different provider videos can both relate to one Recording.

The prototype exposes this versioned deterministic serialization:

    rv1|<recording-mbid>|<normalized-provider-id>|<base64url(provider-item-id-utf8)>

The tuple is authoritative. The serialized key is for deterministic storage, comparison, fixtures, and provenance references only. It carries no ranking meaning.

## Relationship kinds

BF2 keeps these relationships distinct:

- OfficialMusicVideo
- OfficialAudioVisualizer
- LivePerformance
- LyricVideo
- AlternatePerformance
- OtherUnknown

LivePerformance and AlternatePerformance are intentionally not synonyms for an official music video. A relationship can be strong and still represent a different performance.

## Relationship evidence

Evidence kinds are:

- CanonicalMapping - an explicit canonical graph/mapping relation;
- ProviderRelationshipMetadata - structured provider relationship/type metadata;
- ProviderOfficialDesignation - structured provider evidence that the item is official;
- CuratedMapping - Vinyl/Colosseum-owned curated mapping;
- MetadataMatch - title/channel/duration or equivalent metadata similarity;
- TextSearchSimilarity - weak text-search similarity;
- Unknown - relationship evidence is not known.

Evidence strength is:

- Unknown
- Weak
- Strong

Rules:

- Unknown evidence must have Unknown strength;
- TextSearchSimilarity may be Unknown or Weak, never Strong;
- strong qualification requires strong structural evidence, not merely a high-looking text match;
- evidence detail is optional and explanatory, never identity.

## Qualification

Qualification is derived, not caller-assigned.

### Qualified

A non-unknown relationship becomes Qualified only when at least one Strong evidence item is one of:

- CanonicalMapping;
- ProviderRelationshipMetadata;
- ProviderOfficialDesignation;
- CuratedMapping.

### Weak

A candidate is Weak when it has meaningful weak evidence but does not meet the qualified rule.

Text-search similarity is therefore weak by construction.

### Unqualified

A candidate is Unqualified when the relationship remains unsupported or only unknown evidence exists.

OtherUnknown never becomes Qualified, even if a strong relation exists, until the actual relationship kind is known.

## Officiality rule

OfficialMusicVideo and OfficialAudioVisualizer assert officiality, so BF2 refuses to construct those kinds unless there is strong official evidence:

- ProviderOfficialDesignation; or
- CuratedMapping.

Canonical relation evidence can prove that a video is related without proving that the provider item is official. Metadata similarity and text similarity never establish officiality.

This is the negative control that prevents unknown/search evidence from becoming official.

## Product eligibility boundary

BF2 exposes two conservative eligibility predicates without implementing presentation.

### Audio | Video

eligibleForAudioVideoSwitch() is true only for a Qualified:

- OfficialMusicVideo; or
- OfficialAudioVisualizer.

This keeps live performances, alternate performances, and unverified lyric videos from silently becoming interchangeable with the canonical audio Recording.

### Related videos

eligibleForRelatedVideos() is true for Qualified and Weak mappings.

Therefore weak text-search similarity can feed a related-videos surface, but it cannot power Audio | Video.

Unqualified mappings are retained as evidence/provenance but are not eligible for either predicate.

## Provenance

Every candidate retains:

- the exact ProviderId;
- the exact opaque ProviderItemId;
- discovery method.

Discovery methods are:

- CanonicalMapping;
- ProviderStructuredRelation;
- ProviderSearch;
- CuratedMapping;
- ImportedIndex.

Discovery method is provenance, not qualification by itself. Provider search does not become strong merely because a provider returned it first.

## Optional candidate facts

RelatedVideoFacts may carry:
- duration in milliseconds;
- title;
- channel.

All are optional.

Known duration must be positive. Known title/channel values must be non-blank. BF2 does not infer relationship kind or officiality from these facts.

## Candidate sets and provider ordering

RelatedVideoCandidateSet groups candidates for exactly one RecordingMbid.

Rules:

1. every member retains that Recording;
2. duplicate deterministic candidate keys are rejected;
3. different provider items remain separately addressable;
4. input order is discarded by deterministic stable-key canonicalization.

Canonicalized vector order is not relevance, confidence, provider preference, officiality, or playback preference.

## Donor translation

### InnerTune

Keep the useful observation that a YouTube video ID is provider identity and that provider metadata can expose music-video type information.

Reject its YouTube-video-ID canonical identity model. The donor does not establish a robust MusicBrainz Recording-to-video relation, so BF2 makes that relationship explicit and evidence-backed.

### Nuclear

Carry forward provider/candidate provenance discipline and the rule that provider ordering is not host authority.

Do not import Nuclear source ranking, yt-dlp path, provider loader, or first-candidate selection into BF2.

## Explicit non-goals

BF2 does not implement:

- yt-dlp;
- YouTube networking or search;
- video playback;
- BF3;
- current mock switches or Listening Room UI;
- source ranking;
- source selection;
- BB1/BB2 exact audio-source resolution;
- provider authentication;
- provider popularity/relevance ranking;
- persistence schema.

## Prototype

Reference implementation:

backend/bf2-related-video-prototype/

Files:

- RelatedVideoContract.h
- RelatedVideoContract.cpp
- test_related_video_contract.cpp
- CMakeLists.txt

The prototype depends only on Qt Core plus the frozen BA0 value types. It reuses BA0 source files at build time without modifying them.

## Required native harness controls

The deterministic harness proves:

1. video provider ID never replaces RecordingMbid;
2. official relationship and weak text similarity remain distinct;
3. two provider videos for one Recording remain separately addressable;
4. live performance is distinguishable from canonical music video;
5. unknown evidence cannot become official;
6. provider ordering carries no relationship authority;
7. candidate identity is deterministic.

It additionally checks:

- strong text-search evidence is rejected rather than upgraded;
- Audio | Video eligibility is narrower than related-video eligibility;
- invalid optional facts fail instead of being guessed;
- candidate sets cannot mix Recording identities;
- stable enum/error names remain provider-neutral.

## BA0 compatibility conclusion

BF2 is fully representable as additive lane-local types.

No BA0 or BB1 change is required.
