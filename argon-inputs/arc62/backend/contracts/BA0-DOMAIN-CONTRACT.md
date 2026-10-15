# Vinyl Backend BA0 - Domain Contract

Status: W4-D shared-type convergence complete and locally verified; production Colosseum adoption pending
Date: 2026-10-06
Owner: Arc 62 Vinyl

## Purpose

BA0 freezes the presentation-independent identity vocabulary that every later Vinyl backend lane must share.

This is deliberately below Home, Discover, Library, the Listening Room, queue drawers, carousels, rails, cards, and any other current or future presentation.

The backend is mock-agnostic. HTML and QML mocks are reference consumers only. They do not define durable backend state, type names, or ownership.

## Basis inspected

Arc 62:
- `README.md`
- `contracts/TRACK-IDENTITY-CONTRACT.md`
- Music Assistant donor assessment and normalization prototype
- beets donor assessment
- Feishin donor assessment
- Nuclear donor assessment

Colosseum vinyl branch:
- `native/engine/BiblioCatalogTypes.h`
- `native/comicreader/ComicReaderTypes.h`
- `native/engine/ColosseumTitleIdentityRegistry.h`
- `native/CollectionStore.h`
- `native/ProgressStore.h`
- `native/SessionStore.h`
- `native/account/RatingsReviewsDeliveryTypes.h`

Inspected Colosseum basis:
`vinyl` at `e5bd76c638ae19b808eab2371cad70eacce96090`.

## BA0 laws

1. MusicBrainz owns canonical Vinyl music identity.
2. Provider IDs are mappings and provenance, never canonical Vinyl identity.
3. Artist, Release Group, Release, Release Track, Recording, and Work IDs are distinct types even though each is represented upstream by a UUID.
4. Release Group is the normal logical album-family identity.
5. Release is one concrete edition.
6. Recording is the canonical playable-song identity.
7. Release Track is the track occurrence inside one concrete MusicBrainz release and remains distinct from Recording.
8. Medium and track positions are release context, not Recording identity.
9. A provider mapping can point at a canonical entity but can never replace that entity's MBID.
10. Presentation metadata such as title, artwork, display artist, UI position, or card shape is not identity.
11. Source candidates, queue occurrences, playback lifecycle state, listen events, lyrics, and recommendation candidates are later domains. BA0 does not smuggle them into canonical identity.
12. QML paints; native/backend code owns these durable identity rules.
13. `ProfileScopeId` is the one host-owned profile-scope identity shared by durable Vinyl lanes. It is not canonical music identity, account-provider identity, or filesystem identity.
14. `PlayInstanceId` is the host-minted durable identity of one Play journey. It is distinct from Recording identity, QueueOccurrenceId, and BA2 `PlaybackWorkToken`; it never replaces BA2's live generation/work fencing.

## Canonical entity kinds

BA0 recognizes six MusicBrainz canonical entity kinds:

| Kind | Strong type | Product meaning |
| --- | --- | --- |
| Artist | `ArtistMbid` | canonical artist identity |
| Release Group | `ReleaseGroupMbid` | logical album/single/EP family |
| Release | `ReleaseMbid` | concrete edition |
| Release Track | `ReleaseTrackMbid` | one track occurrence on one concrete release |
| Recording | `RecordingMbid` | canonical recorded performance used as playable song identity |
| Work | `WorkMbid` | composition/work identity, supported without becoming an early UI dependency |

The C++ prototype makes these different compile-time types. Code cannot accidentally pass a Release MBID where a Recording MBID is required without an explicit conversion through a generic canonical reference.

## MBID normalization

At an ingress boundary:

- blank MBID -> `MissingCanonicalId`;
- malformed or nil UUID -> `InvalidMbid`;
- a valid UUID is normalized to lowercase, hyphenated, and without braces;
- UUID version is not restricted in BA0;
- no display text may substitute for an absent MBID.

BA0 intentionally accepts equivalent UUID spelling such as uppercase or braces and normalizes it. It does not guess a UUID from another identifier.

## CanonicalEntityRef

`CanonicalEntityRef` is the generic cross-domain reference used when a later subsystem needs to hold one canonical Vinyl entity without erasing its kind.

It contains:

```text
CanonicalEntityKind kind
QUuid mbid
```

It can only be constructed from one of the strong MBID types.

Two references with the same raw UUID but different entity kinds are not equal.

This matters because MusicBrainz identifiers are scoped by entity type. Generic persistence or provider-mapping code must preserve that scope.

## ReleaseTrackCoordinate

A concrete release-track context is:

```text
ReleaseTrackCoordinate
  releaseGroupMbid
  releaseMbid
  releaseTrackMbid?
  recordingMbid
  mediumPosition
  trackPosition
```

Rules:

- positions are one-based and must be positive;
- Release Group, Release, and Recording MBIDs are required;
- Release Track MBID is preserved when available but may be absent, matching the frozen Track Identity Contract;
- the coordinate binds a Recording to one concrete release occurrence;
- it never changes the canonical Recording identity;
- relationship consistency is resolved from catalogue data in later lanes, not guessed in BA0.

This directly preserves the distinction required by the frozen Track Identity Contract and the beets donor.

## Shared host-owned identity

Wave 4 deliberately converges the lane-local profile aliases into BA0 because BA3, BA4, BA5, and BE2 were representing the same host-profile concept with separate strong classes.

`ProfileScopeId` and `PlayInstanceId` are opaque, case-preserving host tokens with the same safety rules:

- non-empty;
- maximum 128 characters;
- no surrounding whitespace;
- no control characters.

`ProfileScopeId` scopes user-owned Vinyl state. It does not encode a provider account, MusicBrainz entity, path, or sync transport.

`PlayInstanceId` names one durable Play journey so history can survive process restart and same-occurrence transport continuation without reusing process-local BA2 generation numbers. The host owns minting/collision resistance. BA0 validates the boundary only.

These two types are compile-time distinct from one another and from canonical/provider identity.

## Provider identity

Provider identity has two separate strong values:

```text
ProviderId
ProviderItemId
```

### ProviderId

A provider ID is a stable machine token identifying an adapter/provider implementation.

BA0 canonicalizes it to lowercase and permits only:

`a-z 0-9 . _ : -`

Maximum length: 128 characters. The first character must be alphanumeric.

A provider registry in a later lane decides whether the token refers to a real installed or available provider. BA0 does not encode donor-specific names or provider ordering.

### ProviderItemId

A provider item ID is opaque provider-owned identity.

Rules:

- non-empty;
- maximum 1024 characters;
- no surrounding whitespace;
- no control characters;
- case and punctuation are preserved exactly;
- it is never parsed as Vinyl canonical identity merely because it resembles an MBID.

This permits YouTube video IDs, Spotify IDs, torrent-provider candidate IDs, local-provider object IDs, and future authenticated-provider IDs without letting any of them define Recording identity.

## ProviderItemRef and ProviderMapping

`ProviderItemRef` is:

```text
providerId + providerItemId
```

`ProviderMapping` is:

```text
CanonicalEntityRef <-> ProviderItemRef
```

The mapping is an association only.

It does not imply:

- provider authority over canonical metadata;
- source eligibility;
- exact-track acceptance;
- automatic playback selection;
- recommendation trust;
- account ownership;
- provider-to-provider coupling.

Those decisions belong to later Vinyl-owned lanes.

This carries forward the strongest Music Assistant and Nuclear lesson: provider identity and stream-provider identity remain separable, while canonical MBID identity remains above both.

## Error vocabulary

BA0 freezes these domain-boundary errors:

- `None`
- `MissingCanonicalId`
- `InvalidMbid`
- `InvalidProviderId`
- `InvalidProviderItemId`
- `InvalidTrackCoordinate`
- `InvalidProfileScopeId`
- `InvalidPlayInstanceId`

The prototype exposes a stable snake_case name for each code.

Later lanes may define richer typed failures such as `Ambiguous`, `CanonicalConflict`, `TargetMissing`, or playback/provider failures. They must not overload BA0 parse/identity errors with resolver semantics.

## Explicit non-goals

BA0 does not define:

- queue semantics;
- queue occurrence identity;
- playback lifecycle/session state beyond the shared `PlayInstanceId` token;
- persistence format;
- database schema;
- sync schema;
- source candidates;
- exact source resolution;
- stream URLs;
- CUE parsing;
- lyrics/timed text;
- Signal Path;
- history/listen events;
- library membership;
- playlists;
- recommendation candidates/ranking;
- recommendation feedback;
- artwork;
- UI presentation models;
- QML registration;
- R2/Workers API wire format.

Those are intentionally downstream so BA0 remains stable if the Vinyl UI is redesigned.

## Downstream ownership

BA1 and later slices consume BA0, not the current mock.

Expected immediate consumers:

- BA1 Queue Core: `RecordingMbid`, `ReleaseTrackCoordinate`
- BA2 Playback Session: BA1 occurrence identity plus BA0 Recording identity; live `PlaybackWorkToken` remains BA2-owned
- BA3/BA4/BA5/BE2: shared `ProfileScopeId`
- BA4/coordinator journey accounting: shared durable `PlayInstanceId`
- BA5 Library/Playlists: canonical entity refs
- BB1 Source Candidate Contract: Recording identity plus provider refs
- BB2 Exact Resolver: `ReleaseTrackCoordinate` plus the Track Identity Contract
- BC timed text: Recording identity
- BE recommendations: Recording, Artist, and Release Group identity
- BF context/video: Recording/Work identity plus provider mappings
- BG catalogue client: all MusicBrainz entity kinds

## Parallel-work rule

After BA0 is frozen, parallel lanes may add their own types in their own directories.

They must not casually edit BA0 to solve local convenience.

A BA0 change requires:

1. a concrete downstream requirement;
2. proof the requirement cannot be represented by an additive lane-local type;
3. explicit compatibility review across existing BA0 consumers;
4. BA0 harness updates.

This prevents parallel workers from turning the shared identity header into a junk drawer.

## Prototype

Adoption-ready reference prototype:

`backend/ba0-domain-prototype/`

Files:

- `VinylDomainTypes.h`
- `VinylDomainTypes.cpp`
- `test_vinyl_domain.cpp`
- `CMakeLists.txt`

The prototype uses Qt Core only. It has no QML, networking, persistence, provider implementation, database, artwork, or playback dependency.

## Acceptance contract

BA0 is accepted when the native harness proves:

1. MusicBrainz entity kinds are compile-time-distinct strong types.
2. Valid MBIDs normalize deterministically.
3. Missing, malformed, and nil MBIDs fail with typed errors.
4. A generic canonical reference preserves entity kind.
5. The same UUID under two entity kinds remains two different canonical references.
6. Release-track coordinates require one-based medium/track positions and preserve an optional Release Track MBID without inventing one.
7. Provider IDs normalize deterministically and reject unsafe tokens.
8. Provider item IDs remain opaque and case-preserving.
9. Provider mappings preserve canonical and provider identities separately.
10. `ProfileScopeId` accepts only safe opaque host tokens and remains distinct from canonical/provider identity.
11. `PlayInstanceId` accepts only safe opaque host tokens and remains a different strong type from `ProfileScopeId`.
12. Neither shared host-owned ID changes MusicBrainz canonical identity rules or BA2 live-work fencing.
13. No UI/mock dependency exists in the prototype.

The W4-D BA0 harness was rebuilt and passed on Qt 6.11.1/MSVC 19.44 on 2026-10-06. Production Colosseum adoption is a later state and must not be inferred from this prototype.
