# Vinyl Backend BG2 - Discovery Enrichment Contract

Status: VERIFIED locally - contract/reference model complete; Qt 6.11.1 Release build and native harness pass
Date: 2026-10-06
Owner: Arc 62 Vinyl

## Purpose

BG2 defines Vinyl's presentation-independent, explicitly noncanonical discovery enrichment plane.

BG1 answers: "What exists?"
BG2 may answer: "What is new, popular, notable, regional, editorially selected, or currently interesting?"

Those planes never merge.

BG2 may point at a canonical BA0 entity, but it cannot rewrite that entity's MusicBrainz identity or BG1 metadata.

## Basis inspected

Arc 62:
- backend/README.md
- backend/BA0-DOMAIN-CONTRACT.md
- backend/BG1-CATALOGUE-CLIENT-CONTRACT.md
- catalogue/CATALOGUE-FOUNDATION.md
- recommendations/MUSIC-RECOMMENDATION-BRAIN.md
- donor-reviews/listenbrainz/DONOR-ASSESSMENT.md

Nearest native implementation precedent:
- backend/ba0-domain-prototype/
- backend/bg1-catalogue-client-prototype/

## Ownership law

1. BA0 owns canonical Vinyl identity.
2. BG1 owns canonical catalogue metadata, relationships, search, and browse truth.
3. BG2 owns noncanonical discovery collections and the evidence that caused canonical entities to appear in them.
4. BE owns personalized recommendation candidates, local taste, and final recommendation ranking.
5. A BG2 score or rank is source-owned evidence. It is never Vinyl's final recommendation score.
6. Popularity is not quality and is not canonical metadata.
7. Editorial status such as "Essential" is not MusicBrainz truth.
8. Region context describes a discovery observation. It never changes Artist, Release Group, Release, Recording, Release Track, or Work identity.
9. The same canonical entity may legitimately appear in multiple discovery collections with different evidence.
10. QML, current Discover-page rails, and mock composition are consumers, not owners of this contract.

## Discovery kinds

BG2 exposes these stable kinds:

- Popularity
- TrendingCurrentInterest
- NewReleases
- Editorial
- Regional
- ChartAggregate
- Other

The kind is explicit on every DiscoveryCollection.

Editorial and Popularity are therefore different evidence classes even when they contain the same canonical entity.

## Canonical entity references

A DiscoveryItem stores exactly one BA0 CanonicalEntityRef.

BG2 does not embed or own:
- BG1 Artist;
- BG1 ReleaseGroup;
- BG1 Release;
- BG1 Recording;
- canonical title;
- canonical artist credit;
- canonical release date;
- canonical aliases.

A consumer that needs canonical display metadata resolves the CanonicalEntityRef through BG1.

This one-way reference is the core separation that prevents discovery evidence from mutating catalogue truth.

## Discovery source and provenance

Each DiscoveryCollection has one sourceProvider, represented by BA0 ProviderId.

Each DiscoveryItem also carries DiscoveryProvenance:
- providerId: the provider or dataset that supplied the item's evidence;
- evidenceId: an opaque, bounded, non-empty evidence identifier;
- detail: optional descriptive diagnostics.

Item provenance may differ from the collection source for aggregate collections.

That permits a Vinyl-owned aggregate to preserve contributor evidence without making a contributor canonical.

## Discovery snapshot and freshness

Every collection has a DiscoverySnapshot:
- token: opaque snapshot/revision identity;
- capturedAtUtc: observation time, normalized to UTC;
- freshness: Fresh or Stale.

Freshness is explicit state.

BG2 core does not call the wall clock or invent a universal TTL. A concrete adapter/cache policy decides whether a snapshot is fresh or stale and stamps that state before creating the collection.

A consumer that requires fresh data uses requireFresh(). A stale collection then fails with typed Stale rather than silently masquerading as current data.

Stale data may still be retained or shown by a caller whose policy explicitly allows stale enrichment.

## Editorial collection identity

Editorial collections must preserve provider-owned editorial collection identity.

BG2 represents this as an optional BA0 ProviderItemRef named editorialCollectionRef.

Rules:
- DiscoveryKind::Editorial requires editorialCollectionRef;
- non-editorial kinds must not carry editorialCollectionRef;
- the ProviderItemRef provider must equal the collection sourceProvider;
- the provider item ID remains opaque and never becomes canonical music identity.

This keeps "Essential Albums", "Editors' Picks", or another curated list identifiable without turning the editorial label into BG1 truth.

## Regional context

A collection may carry DiscoveryRegionContext:
- scheme: the namespace for the region code, for example an ISO or provider-area scheme;
- code: the area/region identifier inside that scheme;
- label: optional human-readable context.

DiscoveryKind::Regional requires region context.

Other kinds may also carry region context when the source is scoped, for example a country-specific chart.

BG2 deliberately does not add an AreaMbid to BA0. Region is enrichment context, not one of Vinyl's canonical music entity identities.

## Source rank and source score

A DiscoveryItem may carry:
- sourceRank: positive one-based rank when the source supplies one;
- sourceScore: any finite numeric score supplied by the source.

BG2 never:
- clamps a source score;
- normalizes it into a Vinyl-wide score;
- infers a missing score;
- converts a score into canonical metadata;
- treats a source rank as personal recommendation rank.

Different providers may use incomparable score scales.

BE may later consume BG2 evidence as an input, but BE owns calibration and personalized ranking.

## Deterministic item ordering

DiscoveryCollection normalizes item order deterministically.

The ordering key is:
1. supplied sourceRank, ascending; ranked items precede unranked items;
2. supplied sourceScore, descending; scored items precede unscored items when rank ties/does not exist;
3. CanonicalEntityKind in BA0 enum order;
4. canonical MBID string;
5. provenance provider/evidence ID as a final deterministic tie-breaker.

Ordering never changes sourceRank or sourceScore.

A duplicate CanonicalEntityRef inside one collection is invalid rather than silently merged, because competing evidence for one item would otherwise be discarded.

The same entity across different collections is valid.

## Deterministic collection ordering

BG2 also exposes orderedDiscoveryCollections().

This is a canonical reproducibility order for storage, fixtures, tests, and transport normalization only. It is not an editorial or product-priority ranking.

The deterministic collection key includes:
- DiscoveryKind;
- sourceProvider;
- editorial collection identity when present;
- regional scheme/code when present;
- snapshot token/time;
- already-normalized item identities/evidence.

Presentation code may choose a different explicit product order without changing BG2 data.

## Typed errors

BG2 defines:
- None
- Unavailable
- Stale
- Invalid

Stable names:
- none
- unavailable
- stale
- invalid

Unavailable means the discovery source/adapter cannot currently answer.

Stale means fresh data was required but the explicit snapshot state is stale.

Invalid means a collection violates the BG2 contract, including malformed snapshot/provenance context, invalid score/rank, missing required editorial/region identity, provider mismatch, or duplicate canonical item.

These errors are BG2 errors. They do not replace BA0 identity parse errors or BG1 catalogue errors.

## Validation rules

DiscoveryCollection::create() fails Invalid when:
- snapshot token is blank, surrounded by whitespace, contains control characters, or exceeds 256 characters;
- capturedAtUtc is invalid;
- provenance evidenceId is blank, surrounded by whitespace, contains control characters, or exceeds 512 characters;
- sourceRank is zero or negative;
- sourceScore is NaN or infinite;
- one CanonicalEntityRef occurs more than once in the same collection;
- Editorial lacks editorialCollectionRef;
- a non-Editorial collection carries editorialCollectionRef;
- editorialCollectionRef belongs to another provider;
- Regional lacks region context;
- a supplied region scheme/code is blank, unsafe, or over 128 characters.

Valid timestamps are normalized to UTC without changing the represented instant.

## Personalization boundary

BG2 does not implement:
- For You;
- Quick Picks;
- collaborative user recommendations;
- personal taste state;
- feedback/hide/snooze state;
- recommendation explanation policy;
- final recommendation score;
- final recommendation ranker.

Editorial/new/popular discovery may later feed BE as evidence, but BE remains a separate plane.

## Serving boundary

BG2 does not implement:
- live ListenBrainz calls;
- MusicBrainz writes;
- Worker endpoints;
- R2 objects or bucket layout;
- HTTP wire format;
- cache transport;
- current Discover-page rails;
- QML.

A future adapter may obtain BG2 data from ListenBrainz, another public dataset, a Colosseum-owned aggregate, or another provider.

The adapter must translate its evidence into this contract without changing BG1 canonical truth.

## ListenBrainz donor interpretation

The frozen ListenBrainz assessment supports BG2 in a narrow way:
- global MBID popularity counts are population evidence;
- such popularity is a prior, not quality or canonical metadata;
- provider/model scores remain provider-owned;
- MusicBrainz MBIDs remain canonical;
- public/open data can bootstrap discovery without uploading private Vinyl listening history.

ListenBrainz recommendation candidates themselves belong in BE, not BG2 personalized ranking.

## Reference prototype

Path:
backend/bg2-discovery-enrichment-prototype/

Files:
- DiscoveryEnrichment.h
- DiscoveryEnrichment.cpp
- test_discovery_enrichment.cpp
- CMakeLists.txt

The prototype is Qt Core + BA0 only at runtime.

The harness may include BG1 value types to prove the separation boundary, but the BG2 public header does not import BG1 catalogue objects.

## Acceptance contract

The native harness is authored to prove:

1. discovery metadata cannot mutate BG1 canonical entities;
2. the same entity may appear in multiple discovery collections with different evidence;
3. Fresh and Stale snapshot state remains explicit, and requireFresh() rejects stale data with typed Stale;
4. source score/rank values survive normalization unchanged;
5. Editorial and Popularity remain distinct discovery kinds and editorial identity is explicit;
6. region metadata never changes canonical identity;
7. deterministic item ordering survives input-order changes;
8. deterministic collection ordering survives input-order changes;
9. malformed collection state returns typed Invalid;
10. Unavailable, Stale, and Invalid have stable typed names;
11. the public BG2 surface has no QML, Worker/R2, current-rail, or BG1 metadata-object dependency.

## Guardrail checkpoint

BG2 crosses the current Preflight oversized-slice threshold because authoring and Qt 6.11.1 build/harness qualification are separate expensive latency domains.

BG2-A in this turn owns:
- this contract;
- native BG2 value/domain model;
- deterministic harness source;
- CMake wiring;
- static/diff verification.

The next guardrail checkpoint, after an explicit later "continue", owns:
- Qt 6.11.1 configure/build;
- native harness execution;
- failure diagnosis if the first qualification attempt fails;
- final verification status update inside BG2-owned files only.

Production Colosseum remains untouched.

## BG2-B first qualification attempt

Attempted on 2026-10-06 with:
- Qt 6.11.1 MSVC 2022 x64;
- CMake 3.30.5;
- Visual Studio 2022 generator;
- source: bg2-discovery-enrichment-prototype/;
- out-of-tree temporary build directory.

Configure succeeded.

The first build failed before harness execution with one BG2-local compiler error:
- DiscoveryEnrichment.cpp: editorial provider validation used ProviderId operator!=;
- BA0 ProviderId intentionally exposes operator== but not operator!=;
- no BA0 change is required or justified.

Current strongest state after the second qualification attempt:
- contract authored;
- BG2 native model/tests/CMake authored;
- static scope checks pass;
- Qt 6.11.1 configure verified;
- BG2 library target compiles successfully;
- harness compilation still fails before execution;
- native harness has not run.

Second-attempt failure:
- test_discovery_enrichment.cpp:129 compares two ProviderId values with operator!=;
- this is the same test-local operator assumption as the first BG2-owned implementation defect;
- inspection confirms the remaining failing ProviderId inequality is in the harness helper;
- DiscoveryEnrichment.cpp's remaining != comparison is between QString values returned by ProviderId::value(), so it is valid;
- BA0 remains unchanged and requires no compatibility edit.

Third qualification attempt:
- repaired the single harness-only ProviderId inequality without touching BA0;
- Qt 6.11.1 Release build succeeded for both vinyl_bg2_discovery and vinyl_bg2_discovery_harness;
- the built harness executable exists;
- test execution did not begin because the script invoked ctest by bare name and ctest was not on PATH;
- diagnosis confirmed C:\Qt\Tools\CMake_64\bin\ctest.exe exists and the built harness executable exists;
- no runtime assertion has failed because the harness has not executed yet.

Current strongest state:
- contract authored;
- BG2 native model/tests/CMake authored;
- static scope checks pass;
- Qt 6.11.1 configure verified;
- BG2 library build verified;
- BG2 harness build verified;
- native harness execution still unverified.

Fourth qualification attempt:
- C:\Qt\Tools\CMake_64\bin\ctest.exe launched successfully against the existing Release build;
- the harness process failed before entering its assertions with Windows exit code 0xc0000135;
- dumpbin confirms the harness depends on Qt6Core.dll plus the standard MSVC runtime;
- Qt6Core.dll exists at C:\Qt\6.11.1\msvc2022_64\bin\Qt6Core.dll;
- VCRUNTIME140.dll and VCRUNTIME140_1.dll are present in Windows System32;
- C:\Qt\6.11.1\msvc2022_64\bin is not on the current PATH;
- therefore the remaining blocker is the test-process runtime environment, not compilation or a proven BG2 assertion failure.

Current strongest state:
- contract authored and locally verified;
- BG2 native model/tests/CMake authored;
- static scope checks pass;
- Qt 6.11.1 configure verified;
- BG2 library build verified;
- BG2 harness build verified;
- CTest 1/1 passed;
- direct native harness execution exited 0;
- BG2 prototype verdict is VERIFIED locally.

Final qualification:
- prepended C:\Qt\6.11.1\msvc2022_64\bin to PATH only for the test process;
- reused the already-built Qt 6.11.1 Release binaries;
- CTest: 1/1 passed, 0 failed;
- direct vinyl_bg2_discovery_harness.exe execution exited 0;
- no runtime assertion failed;
- final verdict: VERIFIED locally.

BG2 acceptance is complete for this prototype. Production Colosseum adoption, live discovery-provider adapters, Worker/R2 serving, personalized recommendation ranking, QML, and current Discover-page composition remain outside BG2 by design.
