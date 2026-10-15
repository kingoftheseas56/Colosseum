# Static catalogue contract, schema 1

I05 Phase 1 candidate. Not deployed. This is a data protocol for the existing
native catalogue owners, not remote QML or a whole-database download. Public
catalogue data contains no account records, tokens, progress or private library.

## Publication and routing

Build from I01's qualified `static-release`, without rebuilding its SQL oracle:

```text
node projection/public_static.mjs <I01-static-release> <content-publication> <fresh-output> <report-outside-output.json>
```

The content publication is produced by the existing `publish_content.py` owner.
Provide the complete current manifest, documents, RPC bundle and immutable art,
including retained old art. The converter validates input sizes, shared-file
hashes, references, document/art hashes and bundle correspondence. It refuses an
existing output, links, traversal, cycles and unlisted references. Failed builds
are unusable: `current.json` is written last. Reports, databases, credentials and
the original private catalogue payload tree are not copied into served output.

`/catalogue/*` and `/assets/content/*` are asset-first. The existing Wrangler
`run_worker_first` list is `/v1/*`, `/admin/*`, `/healthz`, `/internal/*`; do not add
`/catalogue/*` or a catch-all. Present public files require no Worker, guest lease
or D1 access. An absent asset may reach the Worker and fail; it must never be
interpreted as an empty successful catalogue. Disable SPA/HTML fallback for JSON.
The publisher emits `_headers` with:

| Path | Cache-Control |
| --- | --- |
| `/catalogue/current.json` | `public, max-age=60, must-revalidate` |
| `/catalogue/:revision/*` | `public, max-age=31536000, immutable` |
| `/assets/content/*` | `public, max-age=31536000, immutable` |

The asset service supplies content-based ETags and handles `If-None-Match`/304.
`_headers` is a control file, not an HTTP resource. JSON has `nosniff`, public
catalogue paths allow CORS `*`, and catalogue paths carry `X-Robots-Tag: noindex`.
Do not attach account Authorization headers to public requests.

Cloudflare's published Free limits are 20,000 assets/version and 25 MiB/file.
The converter counts the **whole output**, conservatively including `_headers`,
compatibility files, content documents and supplied artwork. It does not impose
the abandoned 128 KiB resharding constraint. Current I01 files remain unchanged
in logical grouping (256-item pages, 128-record blocks, 2,048-row search blocks).
Its existing 4 MiB publisher/legacy-parser bound remains for older clients.

## Discovery, identity and integrity

`GET /catalogue/current.json` is a small JSON object:

```json
{"schemaVersion":1,"revision":"<publication-sha256>","sourceRevision":"<LIBRARY_REVISION>","index":{"path":"/catalogue/<publication-sha256>/index-<sha256>.json","sha256":"<index-sha256>","bytes":12345}}
```

Hashes are lowercase hexadecimal SHA-256 of the exact UTF-8 JSON bytes after
HTTP content decoding. Fetch only same-origin, absolute paths with the selected
publication prefix; verify bytes/size/hash **before parsing or saving**. For
`shared/<sha256>.json`, the filename is the expected digest. Do not reserialize
JSON to calculate its digest. Provider artwork URLs inside records are external
URLs and are not catalogue-shard fetch instructions. Existing content-manifest
hashes apply to content documents and owned artwork; provider URLs stay URLs.

`sourceRevision` identifies the frozen I01/LIBRARY data. `revision` identifies
the entire public generation. It is SHA-256 of compact JSON, in this exact field
order: `{contract:"i05-static-v1",sourceManifestSha256,contentManifestSha256,date,year}`.
The latter hashes refer to the exact source manifests, and date/year come from
I01's report. New data, selection publication date or content generation changes
the public identity. Index/shard hashes transitively bind all rewritten data.
Within a publication, URLs and bytes must never change.

The entry is trusted via HTTPS at the configured origin; hashes detect corruption
and mixed generations, not compromise of that origin. Follow no cross-origin
redirects. Reject unknown schemas rather than guessing. Retain a validated
last-good entry/index for offline use; a failed refresh must not replace it.

## Revision index and all file types

`index-<hash>.json` has these fields:

| Field | Meaning |
| --- | --- |
| `schemaVersion:1`, `version:1` | Public protocol and I01 prepared-layout version |
| `revision`, `sourceRevision` | Public generation and compatible legacy source identity |
| `publicationDate`, `publicationYear` | I01 report's `YYYY-MM-DD` and `YYYY` |
| `timePolicy` | `comics:"publisher-year"`, `malNewReleases:"not-before-no-future-titles"`, `theatreRotation:"client-utc-day"` |
| `lists` | Direct selection-key to list descriptor map; currently empty after bucket packing |
| `listBuckets` | Two lowercase hex digits to shared selection-map path |
| `fixed` | Selection-key to shared complete JSON result path |
| `lookups` | Two lowercase hex digits to shared exact-lookup map path |
| `search` | Family (`imdb`, `anime`, `manga`, `comics`) to `{names:Ref[],keys:Ref[]}` |
| `tankobanSourceHash` | Original Tankoban series manifest identity |
| `content` | Public universe/tribute content references, described below |

`Ref` is `{path,first,last}`. Bounds are inclusive; name bounds use ASCII-folded
UTF-16 lexical comparison, ID bounds use numeric comparison for MAL/Comics and
lexical comparison for IMDb `tt` strings. Boundary-overlapping name blocks can
both match. All shared files are plain JSON, with their type determined by the
index/parent reference, not by trying to infer it from their hashed filename:

1. **Selection map:** `{selectionKey:List,...}`. `List` is
   `{parts:string[],length:number,chunkSize:number,shape,period,notBefore?}`.
   `shape` is `array`, `mal` or `comics`. `parts` are ordered array-block paths;
   `length` is the complete result count, not just the first page.
2. **Ranked block:** an array of existing result objects, in final ranking order.
   Fields and null/number/string/boolean distinctions are preserved from I01.
3. **Fixed result:** the complete existing method result (array/object/boolean).
4. **Lookup bucket:** `{selectionKey:result,...}`. It includes Comics details,
   coverage/downloads/norm lookups; MAL genre counts; Tankoban series envelopes.
   Tankoban envelope fields are `apiVersion:1`, `sourceHash`, `ready:true`,
   `seriesInfo:{volumeCount,countBasis}`, `volumes:[{number,cover,name},...]`.
5. **Record block:** array of `{identity,normal?}`. IMDb identity contains
   `tt,type,title,origLang,year,endYear,runtimeMin,votes,episodes,rating,genres,
   isAnime,synopsis`. MAL uses the existing identity row, with `normal` containing
   the existing mangaById row for manga. Comics identity is the existing legacy
   series row. Do not rename fields or fill absent fields during transport.
6. **Name block:** array of tuples
   `[name,id,recordPath,recordIndex,nativeNorm,year,scoreOrDownloads,votesOrOrdinal]`.
   IMDb uses rating/votes; MAL uses score/source ordinal; Comics uses download
   count/source ordinal. Names include both native-normalized and English
   aliases, with duplicate aliases resolved by ID as described below.
7. **ID block:** array of `[id,recordPath,recordIndex,inlineRecord?]`, ordered by
   ID. IMDb includes inline `{identity:{title,type,rating,votes,isAnime},ordinal}`
   to avoid fetching full record blocks for facts/presentation.
8. **Content manifest/document/bundle:** exact bytes from the existing content
   publisher, additionally content-addressed under the public shared tree.
   Manifest is `{schemaVersion:1,revision,documents,assets}`; a document is
   `{schemaVersion:1,revision,data}`; bundle is
   `{apiVersion:1,revision,method:"bundle",result:{manifest,documents}}`, where
   `documents` maps IDs to the exact document JSON **strings**.

`content` in the index is `{manifest:{path,sha256},bundle:{path,sha256},
documents:{id:{path,sha256,revision}}}`. Content revisions are independent of
catalogue source revisions. Existing external Cinemeta/Apple/AniList/iTunes
URLs remain external; no provider cover mirroring is introduced.

## Keys: preserve request identity separately from selection identity

**Do not replace `CatalogRemote::key`.** It remains SHA-256 of the UTF-8 compact
Qt `QJsonDocument(QJsonObject{{"method",method},{"args",QJsonArray::fromVariantList(args)}})`.
For example the input bytes for `search(["one",20,"manga"])` are
`{"args":["one",20,"manga"],"method":"search"}`. Qt sorts object members;
array order, argument count, string case and nulls are significant. Preserve
the original method/args when notifying `queryChanged` or inspecting F2 state.
Dataset is already owned by the CatalogRemote instance. Do not conflate
different requests merely because their prepared selection is shared.

**Index selection keys** are I01's `selectionKey(dataset,method,args)`:
`JSON.stringify([dataset,method,canonical(args)])`, with object keys recursively
sorted in ascending UTF-16 order, arrays unchanged. They are literal JSON object
keys, not URL-encoded strings. Bucket = first byte of SHA-256(UTF-8 selection key)
as two lowercase hex digits. Page offset/size are removed only when selecting
a full list. Canonical request identity above still includes them. Examples:

```text
["imdb","titleCatalog",[{}]]
["mal","discoverPageFiltered",["popular",[],false]]
["mal","discoverPageFiltered",["top-rated",[{"axis":"genre","key":"romance"}],false]]
["tankoban","series",[13]]
```

Use I01 `text` (null/missing -> `""`), integer (finite numeric conversion,
truncate toward zero; invalid -> 0), and clamp (integer 1..100, method default
only when missing). Do not add trimming, Unicode normalization, provider title
normalization or sorting to request keys. The method-specific **lookup** rules
below do not change the request key.

## Resolving prepared methods

The executable reference is `src/catalogue-prebuilt.ts` at the Phase 1 commit.
Unspecified combinations are **not prepared**, and go to the existing Worker.

* `discoverFilters`/`cachedDiscoverFilters`: fixed arguments are text axis and
  boolean explicit; Comics forces the explicit lookup value to false.
* MAL/Comics `discoverPage` converts its axis/key into a zero/one filter list.
  `discoverPageFiltered` uses its filter array. Drop empty axis/key filters,
  ASCII-fold filter keys, preserve axis spelling and boolean explicit.
  Prepared selections cover zero/one filter; two or more valid filters are not
  prepared. Invalid axes/catalogue names produce the existing successful empty
  envelope. Do not treat a missing referenced file as that empty envelope.
* IMDb `titleCatalog` and MAL `animeCatalog` use their query object as selection.
  Empty/default `order` aliases are tried both with and without explicit
  `votes`/`members`. Other query objects require an exact prepared selection.
* MAL `genreEntries` selects text medium/tag and `score` or default `members`.
  Its fourth argument limits the list; negative means all, missing means 24.
* Comics `curatedGenreShelves(n)` uses exact fixed variants if present, else
  the complete `[-1]` result and slices **each shelf's covers**; negative is all.
* Exact lookup argument rules: `series`/`downloadsFor` integer ID; `genreCount`
  text medium/tag; curated methods text ID, except `curatedByNorm` trims its
  text. Absent keys in a verified complete bucket yield `0` for genreCount,
  `false` for curatedHasDownloadable, `[]` for downloadsFor, `{}` for other
  Comics methods, and an empty ready Tankoban envelope with original sourceHash.
* `mangaById`, `titlePresentation` and `titleFacts` use ID directories. A known
  directory with no ID returns the existing empty result, not a network error.
  Presentation is `{title,type}`. Facts accepts an array of IDs (non-array ->
  empty object), preserves caller batches of 100, orders found rows in each
  batch by source ordinal, and inserts `{rating,votes,isAnime}` by ID; repeated
  IDs overwrite without moving their original object position.
* `content/bundle([])` is prepared by `index.content.bundle`, not a selection
  bucket. Validate its outer envelope, then return its `result` unchanged to
  SourceContentStore through CatalogRemote. Its source identity is the **content
  manifest revision**, not `index.sourceRevision` (the catalogue database).
  SourceContentStore remains the owner of atomic document/asset installation.

Tankoban series has a separate existing native transport in
`TankobanCatalog.cpp` (`GET /v1/tankoban/series/:id`), not a CatalogRemote RPC.
The public lookup is prepared, but Phase 2 must also connect this existing owner
to the shared resolver for its routine series loads to avoid the Worker. This
is native transport work; no new QML API or second state store is needed.

Pagination: offset = max(0, integer(offset)); normal page size is clamp 1..100
(defaults: IMDb 100; MAL/Comics/anime 24). Fetch blocks intersecting
`[offset,min(length,offset+limit))`, slice each with its actual position, and
concatenate in order. No fixed-depth limit. `array` returns the array directly.
MAL/Comics return `{items,nextOffset:offset+items.length,exhausted,
freshness:"bundled"}`. Comics exhaustion is `nextOffset>=length`; MAL exhaustion
is `items.length<limit` (an exactly full final page is not yet exhausted).
MAL also has `fallbackCatalog:"popular"` for trending, else `""`. Comics format
filter rows echo the **original caller's** format key spelling.

## Search rules

For IMDb search only `imdb`; for MAL a specified third-argument medium, otherwise
`anime` then `manga`; for Comics only `comics`. A bad MAL medium or empty query
returns `[]`. IMDb/MAL query text is already normalized by the native owner;
do not re-normalize it. Comics trims query text.

IMDb/MAL select name blocks whose folded bounds intersect the prefix range
`[fold(query),fold(query)+"\uffff"]`; exact methods use the single folded target.
For literal `%`/`_`, scan all name blocks. Matching itself is case-sensitive
`name.startsWith(query)` for search, `name===query` for exact; `%` and `_` are
literal characters, never SQL wildcards. Exact title matches optionally require
the supplied nonzero year. Deduplicate by ID while scanning in directory order;
the last matching alias supplies its ranking tuple. Rank search by nativeNorm
exactness, year descending (null last), score descending, then IMDb votes
descending and `tt` lexical, or MAL numeric ID. Exact omits the exactness rank.
Limits apply **per family** (IMDb default 100; MAL default 20, clamp 1..100).
Exact methods return all matches, ignoring search limits.

**Phase 2 review decision:** native Comics `search` (contains) remains on the
existing Worker, with measured fallback counts, until actual usage justifies
additional client machinery. It must not scan/download the full name index.
Exact Comics title lookups can use the public index. The following contains
semantics remain the I01/Worker oracle contract for any future implementation.

Comics scans the complete name index. Split query on non-Unicode-letter/number;
each word must occur as a case-insensitive ASCII-folded substring. Punctuation
only yields empty search. Exact compares folded full names. Rank search by full
name equality, starts-with, then other matches; download count descending, year
descending, then source ordinal ascending. Exact uses downloads then ordinal.
Default limit is 30; negative is unlimited; zero is zero. Deduplicate by ID.
Retrieve identity objects by recordPath/index; never truncate the scanned index
to fit a request budget. Native fetching/concurrency/disk limits are Phase 2.

## Time, version switches and fallback

* **Comics:** prepared ranking descriptors have `period:"YYYY"`. Native compares
  to the current UTC year. Publisher republishes at the year boundary, because
  scores depend on year. Stale-year selections are not prepared; existing D1
  queries remain the fallback. No daily republish is required.
* **MAL new releases:** the I01 publisher rejects valid start dates after its
  publication day. Descriptors use `period:"stable"`, `notBefore:"YYYY-MM-DD"`.
  They are valid on/after that UTC day for this frozen no-future snapshot. New
  source data requires republication. A future-admission snapshot must be
  explicitly supported by a later publisher/contract, not silently assumed.
* **Theatre daily recipes:** client UTC-day rotation remains in the existing
  Theatre rules. I01 prepares **every** recipe in MOVIE_DAILY_POOL, plus normal
  Movie/Show/Anime recipes. No daily server publication is needed to rotate.
* Other lists use `period:"stable"`. A non-stable period mismatch or notBefore
  violation yields not-prepared, never silently stale ranking.

Schema changes require a new supported schemaVersion. Additive index metadata
may be ignored; changed result semantics need explicit resolver qualification.
Changing `current.json` is atomic with its complete asset version. Keep a
previous deploy/version for rollback; do not combine full old catalogue trees
until recounting the asset budget. Already-downloaded immutable shards stay
usable offline. An old online revision may be absent after a switch: refresh
the entry once, do not mix generations or spin on missing URLs.

Phase 2 must keep **public revision and legacy source revision distinct**.
Public revision changes invalidate prepared query results even if the DB source
has not changed (e.g. content/time publication). A fallback envelope's revision
must match selected `sourceRevision`; it must not replace the public identity or
trigger alternating invalidations between transports. A different fallback
source requires entry refresh/reconciliation, not cross-generation cache reuse.
That database-revision comparison applies to IMDb/MAL/Comics. Content bundle
responses instead match their content manifest revision; Tankoban envelopes
match `tankobanSourceHash`. Do not collapse these existing identity domains.
Missing/bad-hash prepared files are failures with F2 retry/stale-cache semantics;
only genuinely unprepared queries use fallback. Existing Worker routes keep
authentication, readiness, result envelopes and SQL unchanged. Their private
manifest gains `publicRevision` and points at the same public shard tree; the
legacy parser permits only that exact revision's hashed shared paths.

## Native resolver (Phase 2)

`StaticCatalogueResolver` shares public file I/O within the existing network
manager, catalogue origin and cache directory. `CatalogRemote` remains the
owner of exact-query state, results, cooldown and retry; `TankobanCatalog` keeps
its existing series validation, local SQLite precedence and result cache.
Prepared calls do not acquire a guest lease. Unprepared calls retain the old
transport. No QML API or query-key calculation changes.
The existing query-state `enabled` field includes an available last-good static
transport, so F2 consumers wait for disk assembly without a network manager.
The `enabled()` method still describes the legacy network transport itself.

Entry revalidation uses ETag and a 60-second freshness interval. A new public
revision invalidates owner result caches even when the dataset source revision
is unchanged. Corrupt entry/index/shard data fails explicitly; a network outage
may use the validated last-good publication. Failed prepared calls do not
silently become empty results or SQL fallback. Explicit retry revalidates the
entry and preserves the existing exact-query notification contract.

Hash verification precedes parsing and immutable disk persistence. Disk writes
use `QSaveFile` with direct-write fallback disabled. Each origin's cache lives
under `static-catalogue-v1/<SHA256(origin)>/`; shard filenames are their hashes.
`last-good.json` contains the origin, public entry and ETag. Restoring it verifies
the entry and cached index again; cached shards are rehashed when loaded.
Damaged shards are discarded and fetched again when online, or fail when offline.
No credentials, account state or reading progress enter this cache.

Limits: 128 pending resolver jobs, four simultaneous transfers, 15 seconds per
transfer, 45 seconds per query, 512 distinct shard files per query, 25 MiB per
file, 4 MiB per assembled result, 64 MiB of retained serialized shards per query,
128 MiB across active queries, 32 MiB of serialized-cost parsed-cache entries,
and 256 MiB of immutable disk blobs per origin plus small last-good metadata.
These byte budgets describe serialized costs, not a guarantee on Qt heap size.
Cancellation or consumer destruction suppresses delivery and aborts a shard
transfer when its last owner leaves. Files shared by other queries stay active.
Publication churn is bounded to two query restarts.

The `measured(dataset, method, metrics)` signal reports logical file events,
network/disk bytes and file counts, shared/memory hits, elapsed time, and the
largest-file and slowest-read timings. The logging category
`colosseum.catalogue.static` emits the aggregate summary without arguments or
credentials. Its `outcome: worker` counts deliberate fallback, including Comics
contains search; representative fixtures do not establish real user frequency.
Warm cached owner reads need not invoke the resolver and therefore emit no new
file measurement. Local publication probe timings are not live CDN latency.

## Review and deployment boundary

Phase 1 verifies the publisher, full local asset integrity, routing/headers/304,
and legacy TypeScript/Worker parity. It does not establish native, live CDN,
hosted Worker CPU or app qualification. Phase 2 adds the C++ resolver behind the
existing CatalogRemote owner; Phase 3/4 prove native parity and isolated app
journeys. No QML change, deploy, D1 write or Cloudflare setting change is part
of this phase. Claude reviews each gate; Hemanth approves deployment afterward.

References checked 2026-10-15: [asset routing](https://developers.cloudflare.com/workers/static-assets/routing/worker-script/),
[headers/ETags](https://developers.cloudflare.com/workers/static-assets/headers/),
[billing](https://developers.cloudflare.com/workers/static-assets/billing-and-limitations/),
[limits](https://developers.cloudflare.com/workers/platform/limits/).
