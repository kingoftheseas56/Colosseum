# Vinyl Backend BB1 - Source Candidate Contract

Status: production-intent backend contract and native reference prototype
Date: 2026-10-06
Owner: Arc 62 Vinyl

## Purpose

BB1 defines the smallest provider-neutral vocabulary for discovering possible playback sources for one canonical Vinyl recording.

BB1 stops before exact-track acceptance, source ranking, exact access resolution, and playback.

The boundary is:

```text
RecordingMbid requested by Vinyl
  -> eligible source-provider adapters
  -> provider-scoped source candidates
  -> normalized discovery facts and provenance
  -> later Vinyl exact-source decision
```

A discovered candidate is evidence that a provider has something worth evaluating. It is not proof that the provider item is the requested recording.

## Governing ownership

BB1 consumes BA0 without changing it.

BA0 remains authoritative for:

- `RecordingMbid` as canonical playable-song identity;
- `ProviderId` as stable provider identity;
- `ProviderItemId` as opaque provider-owned identity;
- the rule that provider mappings cannot replace canonical MusicBrainz identity;
- provider identifier validation and normalization.

BB1 adds lane-local source vocabulary only.

The Track Identity Contract remains authoritative for deciding whether a discovered candidate is actually the requested track. That decision belongs to BB2 and later exact-resolution work.

## Product laws

1. `RecordingMbid` remains the canonical song identity.
2. A provider candidate never becomes canonical identity, even when its opaque ID happens to look like an MBID.
3. Metadata providers and source providers are independent planes.
4. Provider result order is not ranking, acceptance, or selection authority.
5. Candidate discovery and exact source resolution are separate operations.
6. Candidate facts are observations or provider claims, not exact-track verdicts.
7. Unknown facts remain unknown.
8. A candidate may exist before any exact URL, local path, token, or other playable handle exists.
9. BB1 neither selects the correct candidate nor plays media.
10. Root types stay transport-neutral. Provider-specific extensions may be added later without changing canonical source-candidate identity.

## SourceProviderId

BB1 defines `SourceProviderId` as a lane-local strong type around BA0 `ProviderId`.

The wrapper serves two purposes:

- reuse BA0 normalization and validation;
- prevent a generic or metadata-provider `ProviderId` from silently becoming source-provider authority through implicit conversion.

Creation is explicit through `SourceProviderId::parse()`.

The underlying machine token still follows BA0:

- lowercase normalization;
- 1 to 128 characters;
- first character alphanumeric;
- remaining characters limited to `a-z 0-9 . _ : -`.

A metadata provider may suggest compatibility information in a future lane. It cannot force a `SourceProviderId`, change the selected source plane, or create an implicit provider pairing.

## Source provider descriptor and capabilities

`SourceProviderDescriptor` contains:

```text
SourceProviderId id
SourceProviderCapabilities capabilities
```

Capabilities are explicit flags:

- `DiscoverCandidates`
- `ResolveExactSource`
- `LocalFileSources`
- `HttpSources`
- `PeerToPeerSources`
- `ExternalResolverSources`
- `ProviderMediatedSources`
- `AuthenticatedAccess`

A descriptor must declare candidate discovery, exact resolution, or both.

Only a descriptor with `DiscoverCandidates` may emit a BB1 candidate.

A candidate whose `SourceKind` is known must be supported by the matching provider capability.

`AuthenticatedAccess` states that later access may require provider-owned authentication. BB1 stores no credentials and defines no authentication protocol.

## Source kind

`SourceKind` is a small transport or access-shape vocabulary:

- `Unknown`
- `LocalFile`
- `Http`
- `PeerToPeer`
- `ExternalResolver`
- `ProviderMediated`

This is deliberately broader than any one source technology.

Examples that remain expressible without changing the root contract:

- a local library provider -> `LocalFile`;
- a direct public audio endpoint -> `Http`;
- a torrent-backed or other peer-distributed adapter -> `PeerToPeer`, with protocol-specific fields left to a later extension;
- a yt-dlp-backed adapter -> `ExternalResolver`;
- an account-backed service -> `ProviderMediated` plus `AuthenticatedAccess`;
- a future provider whose access shape is not yet classified -> `Unknown`.

Service legality, regional policy, trust, and provider qualification are not transport identity and therefore do not belong in `SourceKind`.

## Candidate identity

### SourceCandidateId

`SourceCandidateId` is a lane-local strong type around BA0 `ProviderItemId`.

It remains opaque and provider-owned:

- non-empty;
- at most 1024 characters;
- no surrounding whitespace;
- no control characters;
- case and punctuation preserved.

It is never parsed as canonical music identity.

### SourceCandidateKey

Candidate identity is always provider-namespaced:

```text
SourceCandidateKey =
  SourceProviderId
  + SourceCandidateId
```

Therefore these are different candidates:

```text
provider-a / shared-id
provider-b / shared-id
```

even though the local candidate ID is identical.

The tuple is authoritative. The prototype also exposes a deterministic serialization key:

```text
sc1|<normalized-source-provider-id>|<base64url(candidate-id-utf8)>
```

The `sc1` prefix versions the serialization rule. The encoded candidate component preserves arbitrary valid provider-owned IDs without delimiter collisions.

The stable key is suitable for deterministic comparison, storage keys, fixtures, and provenance references. It has no ranking meaning.

## Candidate provenance

`SourceCandidateProvenance` contains:

```text
SourceCandidateKey key
SourceDiscoveryMethod discoveryMethod
```

Discovery methods are:

- `ProviderSearch`
- `CanonicalMapping`
- `LocalScan`
- `ImportedIndex`

Provider provenance follows the candidate through later evaluation. It never replaces `RecordingMbid`.

## NormalizedSourceCandidate

The BB1 normalized candidate contains:

```text
RecordingMbid requestedRecordingMbid
SourceProviderDescriptor provider
SourceCandidateProvenance provenance
SourceKind sourceKind
CandidateResolutionMode resolutionMode
SourceCandidateFacts facts
SourceCandidateFailure? failure
```

### Requested Recording identity

`requestedRecordingMbid` means:

> the canonical recording for which Vinyl asked source providers to discover possibilities.

It does not mean:

> this provider candidate has already been proven to be that recording.

That distinction is load-bearing. BB2 later evaluates candidate evidence against the Track Identity Contract.

### Discovery facts

`SourceCandidateFacts` may carry facts already known at discovery time:

- title;
- artist;
- album;
- duration;
- content length;
- bitrate;
- sample rate;
- bit depth;
- channel count;
- codec;
- container;
- MIME type;
- provider quality label;
- lossless flag.

Every field is optional.

A missing fact stays absent. BB1 does not invent a default duration, bitrate, codec, lossless state, container, or quality tier.

Known numeric facts must be positive. A textual fact supplied as known must not be blank.

These values remain candidate evidence only. Provider-reported metadata does not establish exact Recording identity.

## Delayed resolution and freshness

Nuclear demonstrates an important two-stage lifecycle:

```text
candidate discovery
  -> later exact source resolution
  -> resolved access may later expire
  -> re-resolution
```

BB1 preserves that lifecycle without putting resolved-source state into the candidate.

`CandidateResolutionMode` is:

- `StableReference`: the candidate itself is a stable provider reference for later resolution;
- `Deferred`: exact access is not available at discovery and must be resolved later;
- `Refreshable`: exact access can be resolved again because later access data may expire or become stale.

`Deferred` and `Refreshable` require the provider to declare `ResolveExactSource`.

BB1 intentionally does not store:

- a resolved URL;
- a local playback handle;
- a bearer token;
- an expiry timestamp for resolved access;
- `lastResolvedAt`;
- a resolved protocol or request header set.

Those facts belong to the later `ResolvedTrackSource` contract.

This is the Nuclear lesson expressed at the correct ownership boundary: candidate discovery survives while exact access can be refreshed independently.

## Candidate failure state

Construction and lifecycle failures are separate concepts.

### Contract-boundary errors

`SourceCandidateErrorCode` covers invalid BB1 values or set construction:

- `None`
- `InvalidSourceProviderId`
- `InvalidCandidateId`
- `InvalidProviderDescriptor`
- `ProviderMissingCapability`
- `InvalidFactHint`
- `DuplicateCandidateKey`
- `RecordingMismatch`

These are stable typed errors with snake_case names.

### Candidate lifecycle failure

An already discovered candidate may retain a failure observation:

`SourceCandidateFailureCode`:

- `TemporarilyUnavailable`
- `Unavailable`
- `ProviderFailure`
- `ResolutionFailed`

`SourceCandidateFailure` also carries explicit `retryable` state and optional detail.

A candidate failure does not select another candidate. **BB5 `SourceResolutionSession` owns consumption of candidate failure state for bounded fallback/session policy.**

## Candidate sets and order

`SourceCandidateSet` groups candidates discovered for exactly one requested `RecordingMbid`.

Rules:

1. every candidate must retain the same requested Recording identity;
2. an identical `SourceCandidateKey` may appear only once;
3. same local candidate ID under different providers is legal;
4. candidates are canonicalized by stable key only to make storage, comparison, and tests deterministic.

Canonicalized order is not:

- relevance;
- provider preference;
- quality rank;
- exact-match confidence;
- playback preference;
- failover sequence.

A consumer that treats vector position as source-selection authority violates this contract.

## Metadata/source-provider independence

BB1 deliberately has no metadata-provider pairing field.

There is no:

```text
metadataProvider -> forcedSourceProvider
```

relationship in the contract.

The prototype makes generic BA0 `ProviderId` and BB1 `SourceProviderId` distinct types and supplies no implicit conversion between them.

A future compatibility hint may be modeled as evidence, but provider choice remains Vinyl-owned policy.

## Boundary with BB2 and ResolvedTrackSource

BB1 answers:

> What possible source candidate did this source provider discover for the requested Recording, and what facts were actually observed?

BB2 answers:

> Does this candidate or candidate manifest satisfy Vinyl's exact-track identity contract for the requested Recording/release context?

A later `ResolvedTrackSource` answers:

> What exact access handle and resolved transport facts can be handed toward playback right now?

Therefore BB1 does not contain:

- accepted/rejected exact-track verdicts;
- confidence scores;
- best-candidate ranking;
- release-track matching;
- edition substitution;
- ambiguity resolution;
- resolved URLs;
- access headers or tokens;
- playback state.

## Determinism rules

BB1 determinism is structural:

1. source provider IDs inherit deterministic BA0 normalization;
2. candidate IDs preserve provider-owned spelling exactly;
3. candidate keys are provider-namespaced;
4. stable-key serialization is versioned and deterministic;
5. candidate-set normalization is independent of provider result order;
6. absent optional facts remain absent;
7. typed invalid inputs fail rather than being repaired by guessing.

## Donor translation

### Nuclear

Adopt conceptually:

- explicit stream/source provider capabilities;
- candidate discovery separate from exact source resolution;
- provider-scoped candidate provenance;
- delayed and refreshable resolution;
- candidate failure state.

Reject:

- metadata provider automatically switching the source provider;
- first non-failed provider result becoming selection authority;
- provider-derived track identity;
- resolved-stream expiry state living inside BB1 candidate identity.

### Music Assistant

Adopt conceptually:

- strong separation between canonical identity and provider mappings;
- provider ownership/provenance discipline;
- explicit ambiguity and reconciliation later in the resolver.

Keep outside BB1:

- library-row identity;
- metadata merge authority;
- exact track reconciliation;
- `ResolvedTrackSource`.

### Muffon

Adopt conceptually:

- normalized heterogeneous source envelopes;
- inspectable source variants;
- provider fallback as policy owned by BB5 `SourceResolutionSession`.

Reject:

- weak artist/title matching as automatic acceptance;
- provider/result order as the winner;
- provider extraction mechanisms as root contract assumptions.

## Explicit non-goals

BB1 does not implement:

- provider networking;
- live indexers;
- provider authentication flows;
- exact-track matching;
- source ranking;
- automatic candidate selection;
- album-manifest matching;
- CUE parsing;
- exact URL/path/token resolution;
- playback;
- mpv integration;
- persistence schema;
- provider trust or legal policy.

## Prototype

Reference implementation:

`backend/bb1-source-candidate-prototype/`

Files:

- `SourceCandidateContract.h`
- `SourceCandidateContract.cpp`
- `test_source_candidate_contract.cpp`
- `CMakeLists.txt`

The prototype depends only on Qt Core and the frozen BA0 value-type prototype.

It reuses BA0 source files at build time but does not modify them.

## Required negative controls

The native harness proves:

1. a provider candidate cannot replace the requested `RecordingMbid`;
2. duplicate local candidate IDs from different providers do not collide;
3. generic provider identity cannot implicitly become source-provider identity;
4. reversing provider result order produces the same canonical candidate-set order;
5. a deferred candidate exists without a resolved URL field;
6. unknown quality facts remain absent;
7. malformed source-provider and candidate identifiers fail with typed BB1 errors.

It additionally checks:

- duplicate keys within one provider are rejected;
- candidate sets cannot mix requested Recording identities;
- delayed resolution requires explicit resolver capability;
- source kind must be supported by provider capabilities;
- empty provider capability descriptors fail;
- invalid known fact hints fail;
- candidate failure and retryability remain explicit.

## BA0 compatibility conclusion

BB1 is representable entirely as additive lane-local types.

No BA0 change is required.

The frozen BA0 ownership boundary remains intact:

```text
RecordingMbid
ProviderId
ProviderItemId
    -> consumed by BB1 wrappers and candidate types
    -> never mutated or redefined
```
