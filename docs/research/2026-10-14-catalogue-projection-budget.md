# Catalogue projection and bootstrap budget — 2026-10-14

Recommendation: evaluate a narrow D1 search/facet projection plus versioned entity documents and bounded response pages in R2. This can avoid importing most payload/relationship detail as individually indexed D1 rows. It does not eliminate searchable roots, change native identity authority, or prove a one-day bootstrap. The present Worker/publisher implement full namespaced source-table import, not this proposed projection or R2 delivery.

## Evidence and arithmetic

Computed from `server/content-api/src/source-schema.json` and `server/content-api/source-manifest.json` in the `content-api-free` worktree. Targeted SQL below used the original four databases read-only; no source writes, database transmission or cloud mutations occurred. The snapshot has 1,133,337 rows, 164,626,432 source bytes and 2,823,109 estimated source/index row writes.

Budget model: each table contributes `rows × writeWeight + ceil(rows / 32) × 10`; the additional ten units are the current chunk ledger allowance. At 60,000 estimated import writes/day, days are ceiling division. These are model results, not measured Cloudflare billing. Body-size reductions, validation/control writes, other account use, retries and a lower operating allowance can extend elapsed time.

| Searchable root table | Rows | Existing write weight |
| --- | ---: | ---: |
| IMDb title | 61,585 | 7 |
| MAL anime + manga | 40,000 | 6 |
| GCD comics series | 22,710 | 2 |
| Curated comics series | 1,134 | 3 |
| Tankoban series metadata | 10,000 | 1 |
| **Total roots** | **135,429** | Mixed |

All 10,000 Tankoban series IDs were found in the 20,000 MAL manga IDs. They can become attached metadata on manga entity documents, giving 125,429 root documents rather than 135,429, provided source namespaces and volume-count provenance survive. This does not justify collapsing unrelated GCD and curated identities.

| Scenario, computed with current weights unless stated | Rows | Estimated writes incl. ledger | Minimum capped days |
| --- | ---: | ---: | ---: |
| Full 24-table copy | 1,133,337 | 3,177,359 (35,425 chunks) | 53 |
| Root tables only | 135,429 | 772,257 (4,234 chunks) | 13 |
| Roots + IMDb genre + both MAL relationship tables + curated genre | 535,699 | 1,697,887 | 29 |
| Same, use one axis-bearing MAL relationship table | 408,282 | 1,403,233 | 24 |
| New root projection, row write only, no index charges | 135,429 | 177,769 | 3 |
| New root projection, one index charge per row | 135,429 | 313,198 | 6 |
| New root projection, two index charges per row | 135,429 | 448,627 | 8 |
| New root projection, three index charges per row | 135,429 | 584,056 | 10 |

The four new-projection cases are sensitivity estimates, not a final schema. Primary-key/index accounting, FTS maintenance and the eventual importer must be measured. Even the 135,429 root insert operations exceed 100,000 before secondary indexes; merging Tankoban metadata still leaves 125,429 entity roots. A one-day claim would require an explicit partial catalogue or a different architecture, not merely narrower rows.

The largest detail group—mirror, download, canonical issue, volume, curated edition, source, claim and source-issue tables—contains 573,486 rows and accounts for 1,444,030 modeled writes with current indexes/ledger. Payload relocation is consequential, but filtered fields and aggregate summaries must first be extracted from this group.

## Query semantics that the projection must retain

Live source anchors, relative to the repository root:

- `native/engine/ImdbCatalog.cpp:72` (`titleCatalog`), `:186` (exact match), `:233-238` (prefix search, bound `norm + '%'`), `:273` (batch enrichment).
- `native/engine/MalCatalog.cpp:54` (scoped facet counts), `:154` (tag ranking), `:199` (anime filters), `:332` (exact resolution), `:438` (search), `:552` (filtered discovery), `:700` (tag counts).
- `native/engine/ComicsCatalog.cpp:93` (series/stats), `:126` (search), `:165` (downloads), `:187` (mirrors), `:297` (edition details), `:425` (genre counts), `:597` (facet counts), `:695` (discovery predicates), `:806` (release-year projection), `:851` (native ranking).
- `native/engine/TankobanCatalog.cpp:110` and `:136` (volume-count provenance and volume details).
- `native/engine/ColosseumTitleIdentityRegistry.cpp:106` (existing `ct1:` derivation and identity space).

IMDb needs exact normalized-name resolution with optional year and prefix `LIKE` search (`norm + '%'`); genre membership and exclusions; type, rating, vote, year, runtime, language, anime and episode predicates; and vote/rating/year/episode ordering. Optional exploratory substring queries are additional workloads, not current native IMDb parity requirements. Genre membership cannot simply disappear into an unread object if arbitrary combinations remain supported.

MAL needs both title-normalization fields, exact/search ranking, numeric/type/status/date predicates, tag membership and axis-aware classification. Manga discovery intersects multiple genre/demographic facets case-insensitively, applies explicit-content policy, and computes its Bayesian ranking mean over the same filtered population. New-release results depend on the request date; trending honestly falls back to popular. A global precomputed score or unfiltered facet count would change behavior.

Read-only comparison proved both `tag EXCEPT classification(medium,value,mal_id)` and the reverse return zero rows for this snapshot. Preserve one axis-bearing classification projection (127,417 rows) and derive the legacy tag-membership view, instead of copying both physical tables. Snapshot equality does not guarantee future equality, duplicate multiplicity or identical query plans; validate these before removing either source representation. Classification rows by axis: anime genre 33,751, demographic 7,020, theme 17,884; manga genre 45,812, demographic 9,910, theme 13,040.

Comics search joins series stats, matches all escaped search words, and ranks exact/prefix matches before download count/year. Discovery uses genre, publisher, edition format, availability/coverage and community-source predicates. It counts distinct series per normalized format, not editions. Ranking derives edition/availability counts, maximum publication year, scoped rank ranges, recency and coverage/source summaries, then sorts in C++ with canonical tie breakers. Store these summaries/relationships in D1 or recompute equivalent bounded responses; storing only title/year is insufficient.

Some relationships are unavoidable **logically**, but their original SQL rows are not mandatory. D1 membership rows support arbitrary intersections efficiently. Indexed JSON/bitset membership or an inverted object index could encode equivalent sets, but introduces scan/read/CPU costs and requires proof. Precomputing every arbitrary filter combination is combinatorial; finite standard shelves are sensible cached responses, with live projection queries for combinations and request-dependent ranking.

## Object layout and measured cardinalities

Use immutable paths keyed by snapshot hash, source namespace and native source key; preserve original row IDs, parent IDs, order and provenance inside documents. A version manifest must bind D1 search rows and R2 objects to the same snapshot. Activate that manifest only after completeness/hash checks; never expose mixed snapshots.

| Proposed object family | Count derived from source | Interpretation |
| --- | ---: | --- |
| IMDb title documents | 61,585 | Root ID `tt`; scalar detail and genres may be included. |
| MAL anime/manga documents | 40,000 | Medium-qualified MAL ID; attach Tankoban metadata/volumes to manga. |
| GCD series documents | 22,710 | Stats, issues and references to download parts/posts. |
| Curated series documents | 1,134 | Series, coverage, editions, source claims/issue mappings or part references. |
| **Root-document baseline** | **125,429** | Excludes split parts, manifests, cache pages and enrichment objects. |
| Download-post documents, optional | 57,432 | Avoid repeated mirrors; baseline plus this family is 182,861 objects. |
| Edition documents, optional | 9,655 | Baseline plus posts and editions is 192,516; choose splitting only where useful. |

Targeted distinct counts: downloads reference 22,710 series; mirrors reference 57,408 posts and 22,708 joined series; curated editions cover all 1,134 curated series; sources cover 3,657 edition IDs. There are 1,695 distinct `(locg_id, lower(trim(format)))` memberships suitable for a compact format projection. These are actual query results, not forecasts of storage or request volume.

Grouping mirrors by series produced a maximum of 466,137 JSON characters for the mirror array alone, using `{post_id,url,host,label}` plus delimiters. UTF-8 bytes are at least that many: a monolithic series document can exceed the current 256 KiB admin-body cap before downloads/issues are added. The maximum observed mirror count per post was 186; maximum downloads per series was 368. Nested/pretty JSON, long text and escaped characters further change encoded sizes.

Proposed practical target: measure compact UTF-8 bytes, keep each object/response part around 128 KiB, and split on stable entity boundaries with a small part manifest. The observed largest mirror aggregate alone needs at least four such parts. This is an application target, not an R2 vendor size limit. Do not assume all posts fit: test every encoded document. Current import bodies remain at most 32 rows / 256 KiB; a future object publisher needs its own byte limit, checksum, idempotency and activation contract.

## Work before and after R2 activation

Before activation, perform entirely local, read-only preparation: lock the source snapshot operationally; verify hashes; extract narrow root columns and facet/stat summaries; serialize versioned documents into separate output storage; measure total bytes, object/part counts and worst-case response sizes; emit per-object hashes and completeness manifests; and compare candidate query outputs with native SQLite/C++ outputs. Do not rewrite originals or upload full database files. The arithmetic above is already available without R2.

After authorized R2 activation, implement API-mediated object publication/read bindings, identity-qualified routes, leases/cache policy and bounded object upload; install a separately versioned D1 projection schema/importer; publish objects plus projection under budgets; verify both remote completeness and representative responses; atomically activate the snapshot; then migrate native catalogue consumers through their existing owners with cached/offline fallback. These are proposed tasks, not work performed here. Current staging readiness does not qualify this architecture.

## Required parity and stop gates

Preserve IMDb IDs, MAL medium/IDs, GCD IDs, curated LOCG/edition IDs, source/claim/issue keys and all relationship references. The existing native identity registry remains authoritative for `ct1:` identity and alias reconciliation; a hosted object ID or provider ID must not silently replace it. Keep local library/history/progress and provider resolution outside catalogue ownership.

Prove exact resolution, native IMDb prefix matching, Comics escaped all-word substring behavior, missing/null values, case normalization, multi-facet intersections, explicit gating, scoped counts/means, sort/ties, date boundaries, pagination and fallback/freshness metadata. Verify per-series downloads/mirrors and curated editions/sources/claims/issues with their ordering and evidence fields. Identify currently unspecified SQL ties before promising stable cross-engine pagination. Compare native Comics ranking, not merely its source SQL.

Measure D1 writes/reads/query plans and object bytes/counts for the final schema; validate budgets for actual client traffic and repeated snapshot updates. Until these checks pass, retain the native catalogue route as baseline and describe the hybrid as a migration proposal. No cloud mutations or native-client migration were performed for this note.
