# Tankoban manga browsing implementation

Baseline: `95e1916606088abeac2fbb93b4f66bb36f1983ba`. This is a source patch, not a deployment.

## What appears

Your Collection remains first. The existing static Top in Tankoban rail remains the fallback.
When catalogue data is available it becomes **Popular Manga**, followed by:

1. Completed Series (`completed`): Finished series.
2. Currently Publishing (`publishing`): Publishing series; not a promise of recent chapter uploads.
3. Completed in Five Volumes or Fewer (`short-completed`): Finished, integer volume count 1–5; unknown/zero counts do not qualify.
4. One-shots (`one-shots`): metadata type One-shot.
5. Manga That Began in the 1990s (`started-1990s`): start year 1990–1999.
6. Manga That Began in the 2000s (`started-2000s`): start year 2000–2009.
7. Manga That Began in the 2010s (`started-2010s`): start year 2010–2019.

Explore by Genre remains last. The seven new shelves accept Manga, One-shot, Manhwa,
Manhua and Doujinshi; exclude Novel/Light Novel. Existing Popular semantics are deliberately
unchanged. Shelf labels mean publication metadata, not download/source availability.
No new metadata provider, credentials, import or database migration is introduced.
Authors, magazines and thematic/editorial rails are deferred, not faked from title strings.

Rows require three eligible titles. Preview queries read 24 records; Popular shows up to10,
others up to20. Later previews prioritize less-exposed titles without dropping all overlap.
See all retains the catalogue owner's stable popularity order and exact shelf predicate.
Canonical MAL IDs route into existing series resolution. Library, progress, source mapping,
Tankoyomi, volume/chapter choice, language and reader behavior are untouched.

## Local versus online behavior

- Existing local MAL SQLite: shelves work with the modified native client, against the existing
  columns. Missing metadata excludes a title only from predicates needing that metadata.
- Updated static/online catalogue: one `landing("manga")` bundle primes all eight exact page
  queries, with separately keyed adult-policy variants. No per-shelf upstream Jikan fetch.
- Existing old online publication: lacks manga landing. Keep original Top+genre presentation
  and a retry notice; do **not** request eight unsupported shelf pages or label generic results
  as curated shelves. The source patch alone cannot make an old publication contain new rows.
- Stale validated cache remains usable with an offline notice. First-run failure is not a
  successful empty catalogue. Policy/source changes cancel old delivery and clear its cards.

Before enabling online shelves, generate a fresh static publication from the canonical source
DB using the modified projection code; publish its catalogue/index/manifests together and update
Worker code for semantic fallback. See `server/content-api/STATIC_CATALOGUE.md` for exact existing
pipeline commands/prerequisites. No publication or Worker deployment was performed here.

## Validation

JavaScript:

```sh
node --test tests/manga_browse_test.mjs tests/tankoban_discover_remote_delivery_test.mjs tests/tankoban_cached_identity_routes_test.mjs
```

Real QML components (Qt6 Quick/QuickTest development packages):

```sh
cmake -S tests/manga_browse -B build/manga-browse-tests
cmake --build build/manga-browse-tests
ctest --test-dir build/manga-browse-tests --output-on-failure
```

Native targets: existing `mal_catalog_discover_harness` and
`static_catalogue_resolver_harness`. Cover status/format/count/year boundaries,
explicit policy, facets, pagination, static missing-list fallback, landing validation/cache hydration.

Server: `cd server/content-api && npm ci && npm run check && npm test` with the project's
normal generated Worker types and generated assets prerequisites (see its documentation).
The dry-run bundle in this workspace used an empty ignored assets directory, not real catalogue assets.

Evidence here is focused Qt6.11.1/GCC14.2 native+QML harnesses and server/JS checks. This is not a
full application/package build, on-device inspection or real-cover visual approval. No push,
merge or deployment is included.

## Published-data sample (2026-10-30)

`tankoban-manga-coverage-sample.json` records hash-verified live index/shard provenance.
253 unique records from six shards referenced by the first alphabetical name shard included
246 allowed manga formats: 220 Finished, 25 Publishing; 199 positive volume counts, 241 known
years. Predicate matches before adult policy: 163 short-completed, 18 one-shots, 8/85/119
started in the 1990s/2000s/2010s. This deliberately bounded convenience sample is **not** a
representative survey or catalogue-wide total. Published normal records omit explicit
classification and normalize missing counts to zero; those source distinctions cannot be
measured from this sample. The observed publication lacks manga landing.
