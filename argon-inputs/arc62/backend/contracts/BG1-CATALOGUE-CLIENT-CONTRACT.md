# Vinyl Backend BG1 - Catalogue Client Contract

Status: contract and native reference prototype
Date: 2026-10-06
Owner: Arc 62 Vinyl

## Purpose

BG1 defines Vinyl's presentation-independent catalogue/query contract.

It is the consumer-side vocabulary for asking a logical catalogue service about canonical MusicBrainz entities. It does not define how Arc 65 builds, shards, stores, transports, publishes, or serves that data.

BA0 remains the owner of canonical identity. BG1 consumes BA0 strong types and adds catalogue metadata, query, pagination, relationship, and error vocabulary around them.

## Basis inspected

Arc 62:
- backend/README.md
- backend/BA0-DOMAIN-CONTRACT.md
- catalogue/CATALOGUE-FOUNDATION.md
- catalogue/ARTWORK-STRATEGY.md

Arc 65:
- arcs/65-colosseum-cloud-warehouse/README.md
- arcs/65-colosseum-cloud-warehouse/research/MUSICBRAINZ-VS-GCD-AUDIT-2026-10-06.md
- arcs/65-colosseum-cloud-warehouse/research/R2-RELIABILITY-AND-COST-BASELINE.md

Read-only Colosseum architecture references:
- native/engine/BiblioCatalogTypes.h
- native/engine/BiblioCatalog.h
- native/engine/ImdbCatalog.h
- native/engine/CatalogVaultClient.h

Inspected Colosseum basis: master at a126f0cd793043c5c6b3011cf2dad54783f8adbc.

## Architecture correction

Older Arc 62 catalogue material describes PostgreSQL plus Colosseum Server as the likely production serving path.

That storage/serving detail is superseded by the current Arc 65 direction:

    official MusicBrainz dump
    -> build-time normalize/reduce
    -> immutable Vinyl query corpus in R2
    -> thin Cloudflare Workers API
    -> small logical catalogue responses

BG1 does not expose that mechanism.

The current R2/Workers path is one implementation of the logical catalogue service. A future local mirror, offline pack, different object store, different edge runtime, or different indexing strategy must be able to implement the same BG1 contract without changing Vinyl's domain vocabulary.

This also resolves one wording overlap in Arc 65. BG1 owns the Vinyl consumer contract. Arc 65 consumes that contract when it chooses the minimum corpus, index/shard layout, endpoint wire format, cache policy, and publishing pipeline required to satisfy it.

## Entity law

1. ArtistMbid, ReleaseGroupMbid, ReleaseMbid, ReleaseTrackMbid, RecordingMbid, and WorkMbid come from BA0 and remain distinct strong types.
2. Release Group is Vinyl's normal logical album, single, or EP family.
3. Release is one concrete edition/version of a Release Group.
4. Recording is the canonical recorded performance used as song identity.
5. Release Track is one track occurrence on one concrete Release and remains distinct from Recording.
6. Artist discography returns Release Groups, not a flattened list of Releases.
7. Multiple CD, vinyl, digital, regional, remastered, promotional, or other Releases under one Release Group do not become duplicate logical albums.
8. Release-specific views may enumerate every qualifying Release without changing the Release Group's identity.
9. Aliases, transliterations, alternate scripts, credited names, titles, dates, statuses, countries, and other presentation metadata never replace an MBID.
10. Work is future-compatible relationship context. BG1 may point a Recording at a WorkMbid without forcing a classical/work-specific product surface now.

## Public value vocabulary

BG1 adds lane-local value types in namespace vinyl::catalogue.

### Alias

Alias contains name, sortName, locale, script, and primary.

Aliases are searchable metadata. They may express transliteration, alternate script, locale-specific spelling, or another upstream alias. An alias match returns the canonical entity's BA0 identity. It never becomes a second identity.

### Artist

Artist contains:
- ArtistMbid
- name
- sortName
- disambiguation
- aliases

### ArtistCredit

ArtistCredit contains ArtistMbid plus creditedName.

A credited name records how an artist is written in that context while retaining the canonical ArtistMbid.

### ReleaseGroup

ReleaseGroup contains:
- ReleaseGroupMbid
- title
- aliases
- artistCredit
- primaryType
- secondaryTypes
- optional firstReleaseYear

The current primary-type vocabulary is Album, Single, EP, Broadcast, and Other.

Secondary types remain string metadata so the catalogue can preserve values such as Live, Compilation, Soundtrack, Remix, and future upstream values without coupling BG1 to a Discover-page taxonomy.

### Release

Release contains:
- ReleaseMbid
- ReleaseGroupMbid
- title
- artistCredit
- status
- country
- releaseDate
- barcode

A Release always retains the Release Group it belongs to.

### Recording

Recording contains:
- RecordingMbid
- title
- artistCredit
- lengthMs
- aliases

### ReleaseTrack

ReleaseTrack contains:
- optional ReleaseTrackMbid
- required RecordingMbid
- position
- title
- artistCredit
- lengthMs

RecordingMbid is required.

ReleaseTrackMbid is optional because the upstream context may not always provide one. Absence stays absence. BG1 never invents a Release Track MBID.

### ReleaseMedium and ReleaseTracklist

ReleaseMedium contains position, format, title, and ordered tracks.

ReleaseTracklist contains ReleaseMbid and ordered media.

Media and track positions are release context. They do not change Recording identity.

### RecordingAppearance

RecordingAppearance contains:
- ReleaseGroupMbid
- ReleaseMbid
- optional ReleaseTrackMbid
- mediumPosition
- trackPosition
- releaseTitle

This allows Vinyl to answer where a Recording appears without collapsing edition and recording identity.

### EntityRelationship

EntityRelationship contains CanonicalEntityRef target plus relationType.

BG1 currently uses this for Recording relationships where useful. A Recording-to-Work relation can therefore be represented without making Work a first-class search or UI requirement.

## Catalogue client

The public seam is ICatalogueClient.

It provides:
- artistById(ArtistMbid) -> found Artist, Redirected { to ArtistMbid }, or typed failure
- releaseGroupById(ReleaseGroupMbid) -> found ReleaseGroup, Redirected { to ReleaseGroupMbid }, or typed failure
- releaseById(ReleaseMbid) -> found Release, Redirected { to ReleaseMbid }, or typed failure
- recordingById(RecordingMbid) -> found Recording, Redirected { to RecordingMbid }, or typed failure
- lookupCanonicalBatch(CanonicalEntityRef[]) -> deterministic per-request Found / Redirected / NotFound items
- representativeRelease(ReleaseGroupMbid) -> one concrete Release selected by catalogue canonical-release mapping, a release-group Redirected outcome, or typed failure
- artistDiscography(ArtistMbid, ArtistDiscographyQuery, PageRequest)
- releaseGroupReleases(ReleaseGroupMbid, ReleaseVersionQuery, PageRequest)
- releaseTracklist(ReleaseMbid)
- recordingAppearances(RecordingMbid, PageRequest)
- recordingRelationships(RecordingMbid)
- search(SearchQuery, PageRequest)

The interface contains no URL, HTTP, Worker, R2, SQL, database-path, cache-path, QObject, QML, or current-mock concept.

A concrete adapter is responsible for translating these logical requests to its implementation.

## Wave 4 identity-over-time amendment

MusicBrainz canonical identity can change through merges. BG1 therefore treats redirect as a successful identity fact, not as `NotFound` and not as `Unavailable`.

The direct identity lookups return `CatalogueLookupResult<T, Id>` with exactly one meaningful outcome:
- Found: `value` is present;
- Redirected: `redirectedTo` is present and names the same canonical entity kind;
- failure: neither payload is present and `CatalogueError` names the failure.

Consumers must not guess through `NotFound`. A redirect target is explicit evidence that the requested MBID merged into another MBID. Redirect does not change provider/source identity and does not permit cross-kind coercion.

`lookupCanonicalBatch` exists for convergence work over saved IDs. It preserves input order and duplicates. Each requested identity receives one deterministic `Found`, `Redirected`, or `NotFound` item. Service-wide `Unavailable` fails the batch rather than manufacturing per-item absence. The reference surface intentionally supports the entity kinds with first-class BG1 identity lookup: Artist, Release Group, Release, and Recording. Release Track and Work remain outside this batch until BG1 owns direct lookup payloads for them.

A `Found` batch item may carry `LastKnownDisplaySnapshot { primaryText, artistCredit[] }`. This snapshot is presentation fallback only. It contains no replacement canonical identity, no source ID, no ranking score, and no claim that its text is current. Durable state may persist it beside a canonical ID so a temporary catalogue outage does not erase human-readable queue/library/history text. It must never satisfy an identity lookup, override a redirect, or turn `Unavailable` into canonical certainty.

`representativeRelease(ReleaseGroupMbid)` resolves the explicit edition-selection seam needed before a Release Group can supply a concrete tracklist. The returned value is always a real `Release` retaining its own ReleaseMbid and its owning ReleaseGroupMbid. The catalogue projection should use MusicBrainz canonical-release mapping when available. If the projection has no representative mapping, BG1 returns `NotFound` for `representativeRelease`; it does not invent an edition from country, date, barcode, list order, or whichever query completed first.

A redirected Release Group produces `Redirected { to ReleaseGroupMbid }` from this seam. The caller resolves that canonical group before requesting its representative edition.

## Query vocabulary

### ArtistDiscographyQuery

Contains a ReleaseGroupFilter.

The filter may constrain:
- primary Release Group types
- secondary Release Group types
- first-release-year lower bound
- first-release-year upper bound

The query does not contain current UI concepts such as Albums rail, Essentials, Discover row, See All, hero, carousel, or card.

### ReleaseVersionQuery

May constrain:
- release statuses
- release countries

It exists to browse concrete editions under a known Release Group.

### SearchQuery

Contains:
- search text
- optional allowed entity kinds
- optional Release Group filter

Public search entity kinds are Artist, Release Group, and Recording.

Release is intentionally not a top-level search kind in BG1. Concrete editions remain reachable under their Release Group instead of flooding global results.

An empty entity-kind list means all three public search kinds.

### Filter composition

Within one list-valued filter field, values are ORed.

Across different filter dimensions, constraints are ANDed.

An empty list means unconstrained.

An invalid range, unsupported structural query, blank search term, or invalid page limit returns InvalidQuery. A backend must not silently reinterpret an invalid query into a different one.

## Search results

Each SearchHit contains:
- explicit SearchEntityKind
- CanonicalEntityRef
- canonicalText
- matchedText

Consumers never infer whether a hit is an Artist, Release Group, or Recording from text shape.

canonicalText is the canonical catalogue display text for the entity.

matchedText records the title, alias, or transliteration that matched the query when useful.

A match on Hikaru Utada, for example, may return the canonical Artist whose primary name is 宇多田ヒカル. The alias explains the match. It does not replace the ArtistMbid.

## Deterministic pagination contract

Paged queries use PageRequest with limit plus optional PageCursor and return Page<T> with items plus optional nextCursor.

PageCursor is opaque to the consumer.

Rules:
1. limit is 1 through 100 inclusive in the reference contract.
2. A consumer may store and echo a cursor but must not parse, increment, or synthesize it.
3. A cursor is bound to the logical query that produced it and may also be bound to a corpus/snapshot revision.
4. Repeating the same query against the same immutable catalogue revision produces the same total ordering and therefore the same page boundary/cursor sequence.
5. Ordering must have a deterministic canonical tie-breaker, ultimately using canonical MBID when other keys tie.
6. A malformed cursor, cursor for another query, cursor for another incompatible snapshot, or otherwise invalid continuation returns InvalidPage.
7. The public contract does not require an offset cursor, a database row key, an R2 object key, or any other implementation encoding.
8. A service may invalidate old cursors when adopting a new immutable catalogue revision. It must fail with InvalidPage rather than silently continuing against a different logical result set.

The deterministic fake uses a private query-bound token only to prove these semantics. Its token format is not a wire contract.

## Ordering requirements

Artist discography uses stable catalogue ordering. The reference fake uses first-release year, title, then ReleaseGroupMbid.

Release Group versions use stable edition ordering. The reference fake uses release date, then ReleaseMbid.

Recording appearances use stable concrete-release, medium, and track ordering with canonical IDs as final tie-breakers.

Production search may use deterministic textual relevance. Popularity, recommendation, editorial, or personal-taste scores are not BG1 inputs. Ties must resolve deterministically with explicit entity kind and canonical identity, and pagination must pin the resulting order for the cursor's logical result set.

## Typed catalogue errors

BG1 defines catalogue-layer errors separately from BA0 parse/identity errors.

### NotFound

A syntactically valid canonical identity is not present in the catalogue projection. This does not mean the MBID was malformed.

### Unavailable

The logical catalogue service cannot currently answer the request.

The underlying reason may be a network outage, unavailable local corpus, failed edge service, or another adapter-specific condition. That mechanism is deliberately hidden from the consumer contract.

### InvalidQuery

The request is structurally invalid, such as blank search text, invalid year bounds, or an invalid page limit.

### InvalidPage

The continuation state is invalid for the requested logical query, including a malformed, stale, foreign-query, or incompatible-snapshot cursor.

Error codes expose stable snake_case names:
- not_found
- unavailable
- invalid_query
- invalid_page

Adapter-specific diagnostics may add detail text, but callers branch on typed codes rather than parsing prose.

## Storage and transport boundary

BG1 consumes one logical capability: Vinyl catalogue service.

Today, Arc 65 intends to implement that capability through baked immutable MusicBrainz query objects in R2 behind thin Workers.

BG1 does not expose:
- bucket names
- object keys
- shard layout
- Worker routes
- HTTP status codes
- JSON field names
- CDN behavior
- PostgreSQL
- SQLite
- source dump paths
- MusicBrainz dump parsing
- R2 publish versions

Those are serving/build concerns owned by Arc 65.

A future adapter can satisfy ICatalogueClient from another backend without changing these value/query types.

## Artwork boundary

BG1 does not download or transport artwork bytes.

The current Artwork Strategy keeps artwork as a separate lazy plane. Release Group and Release MBIDs are sufficient for a later artwork resolver to request logical art without changing catalogue identity.

Missing artwork never means a missing catalogue entity.

## Discovery boundary

BG1 answers what canonical catalogue entities exist and how they relate.

BG1 does not implement:
- popularity
- trending
- recommendations
- For You
- editorial Essentials
- current mock Discover rails
- regional/editorial ranking
- personal taste
- artwork downloading

Those belong to BG2, other later Arc 62 planes, or Arc 65 serving/build work as appropriate.

## Explicit non-goals

BG1 does not implement:
- MusicBrainz dump parsing
- corpus reduction
- R2 upload
- R2 object/index/shard design
- Cloudflare Worker code
- live HTTP
- production cache policy
- production QML facade
- current mock composition
- artwork downloading
- recommendation ranking
- popularity/trending/editorial discovery

## Reference prototype

Path: backend/bg1-catalogue-client-prototype/

Files:
- VinylCatalogueTypes.h
- VinylCatalogueTypes.cpp
- VinylCatalogueClient.h
- FakeVinylCatalogueClient.h
- FakeVinylCatalogueClient.cpp
- test_catalogue_client.cpp
- CMakeLists.txt

The prototype uses Qt Core only plus BA0's existing VinylDomainTypes implementation.

It contains no live network, SQL, R2, Worker, QML, artwork, recommendation, or production Colosseum dependency.

## Acceptance contract

The deterministic native harness must prove:

1. Artist to paginated Release Groups.
2. Release Group to multiple concrete Releases/versions.
3. Release to media to ordered tracks.
4. Release tracks retain required RecordingMbid and optional ReleaseTrackMbid separately.
5. Search hits expose explicit Artist, Release Group, and Recording kinds.
6. Alias and non-Latin/alternate-script metadata resolve to canonical MBIDs without replacing identity.
7. Pagination is stable and deterministic; a cursor from another query is rejected with InvalidPage.
8. Unknown canonical entities return typed NotFound.
9. Storage/backend implementation vocabulary is absent from the public consumer contract.
10. QML and current-mock concepts are absent from the public consumer contract.

The harness additionally proves Recording appearances, a future-compatible Work relationship, typed invalid-query/page behavior, and typed Unavailable behavior.

Production Colosseum adoption and Arc 65 serving implementation are later states. They must not be inferred from this prototype.
