# Biblio Canonical Graph Spike

## Verdict

The winning model is a local canonical SQLite graph seeded by Goodreads-style
book/series data, then strengthened by source assertions. No single scraped or
shadow-library source should become canonical truth directly.

## Current Evidence

- Goodreads seed file: `goodreads_series_seed_2000.csv`
- LibGen benchmark: `_large_libgen_pass_goodreads_2000/`
- OceanofPDF benchmark: `_oceanofpdf_pass_goodreads_2000/`
- Z-Library benchmark: `_zlib_pass_goodreads_100/`
- ReadAnyBook benchmark: `_readanybook_pass_goodreads_100/`
- Anna's Archive dataset probe: `anna_archive_dataset_probe.md`
- Anna local metadata parser scaffold: `anna_local_metadata_probe.py`
- Source comparison: `biblio_source_comparison_goodreads_2000.md`
- 100-row comparison with Z-Library:
  `biblio_source_comparison_goodreads_100_with_zlib.md`
- Canonical graph: `biblio_canonical_graph_goodreads_2000.sqlite`

## Source Roles

- Goodreads dump: best current seed for canonical work identity and expected
  series membership.
- OceanofPDF: best current assertion source for series name and ordinal.
- LibGen: best current assertion source for ISBN, edition shape, file hash, and
  direct download candidates.
- Z-Library public search: useful title/discovery corroboration, but not a
  series or ISBN source from the public search surface. In the first 100 rows it
  hit 99/100 searches and 95/100 exact titles, but only 11/100 correct
  series/order assertions.
- ReadAnyBook: useful routing/title corroboration plus a small number of valid
  series/order assertions, but not strong enough to displace OceanofPDF. In the
  first 100 rows it hit 80/100 searches, 77/100 exact titles, and 35/100
  correct series/order assertions.
- Anna's Archive public UI: do not benchmark from live search pages. Current
  mirrors are domain-rotating, partly protected, and noisy. The official route
  for programmatic metadata is local generation/download of ElasticSearch or
  MariaDB databases, or local exploration of raw JSON records. A local parser
  scaffold now exists for `aarecord_elasticsearch` JSON/JSONL records.

## Graph Shape

- `work`: individual creative works, such as `A Game of Thrones`.
- `series`: series containers, such as `A Song of Ice and Fire`.
- `series_membership`: work-to-series edges with display/sort position.
- `edition`: ISBN/language/date/publisher/format claims.
- `download_candidate`: non-canonical source/file candidates.
- `source_assertion`: every claim preserved with source, confidence, and JSON
  evidence.
- `resolver_alias`: title/series aliases for routing and matching.
- `quality_flag`: mismatches, missing series, no-search hits, and source errors.

## Promotion Rule

The app should route against `work` and `series` IDs, not source IDs or download
IDs. Source rows are evidence. Canonical tables are the app-facing graph.

## Next Tests

Run metadata-only probes for Z-Lib and Anna's Archive only after this graph is
stable. The question for each new source is not "can it replace everything?";
it is "which claim type does it improve?"

Z-Library has now been tested on the first 100 Goodreads seeds. It improves
search/title corroboration but does not improve the canonical graph's
series/order or ISBN layers.

ReadAnyBook has now been tested on the first 100 Goodreads seeds and added to
the canonical graph as low-weight evidence only. It adds useful aliases,
internal read URLs, and some valid series/order corroboration, but it remains a
secondary source behind OceanofPDF for series/order and behind LibGen for
edition/download evidence.

Anna's Archive was inspected through its official FAQ/datasets guidance and one
browser-readable `aarecord_elasticsearch` sample record. It should enter the
pipeline only after we have a local metadata dump/export. Expected useful claim
types are title/author corroboration, publisher/year, cover URL, extension, and
filesize. Series/order remains unproven.
