# Native local indexing continuation

This extends `LOCAL-ADDON-2026-11-13.md`; the earlier report's filename parser,
IMDb fallback, torrent indexing and append/meta-cache persistence gaps are now
addressed by the independently added components below. This is not a claim of
complete Server 1.0 or every local-addon SDK/platform edge-case parity.

## Added native components

- `LocalFilename`: M304 filename/path parsing (strict/fromInside/fileLength/hints,
  seasons, multiple episodes, date/year/disk clues, samples and tags).
- `LocalImdb`: opt-in M1069 suggestion HTTP lookup and year-qualified retry,
  edit-distance/name/type/year ranking, source-result projection, cancellation.
  Its search-term mapping follows M243/M1067, including source-preserved combining
  characters. `LocalSearchTerm.inc` is a generated functional character mapping
  from the pinned oracle, not a bundled JavaScript runtime or dependency.
- `LocalIndexer`: M501 file and parsed-torrent indexing. Uses the native filename
  parser, Cinemeta names provider first, IMDb suggestions second, three concurrent
  recognition tasks, same-key coalescing and 12-hour successful-result cache.
  Torrent decoding uses the added metadata-only `policy::TorrentMetadata::parseForIndexing` path after bounded bencode
  preflight (16 MiB, depth 64, 100,000 structural tokens). Preserves original file
  indexes through extension filtering and builds DHT/tracker source descriptors.
- `LocalAddonLog`: M1079 JSONL append, first-save-only behavior, latest-record reload,
  version/readability validation, invalid-log rewrite, ordered retained records.
  Metadata records use the same class without file readability validation.
- `LocalAddonRuntime`: optional composition connecting native discovery, indexing,
  enrichment, entry append log and `<database>Meta` cache. Construction has no
  filesystem discovery/network side effects. `start(database)` requires an explicit
  nonempty database path. Cancellation stops addon work and both provider clients.

## Integration delta

The shared `TorrentMetadata` metadata-only parse addition is included. It preserves
existing parse ABI/default geometry behavior.

Include `tests/cmake/LocalIndexing.cmake` after `LocalAddon.cmake`, then call
`server1_register_local_indexing_tests()`. This adds only new source files/targets;
no previously frozen addon source/header or `LocalAddon.cmake` was changed.

An application may own `LocalAddonRuntime`, mount `runtime.addon()` through
`installLocalAddonRoutes()`, and call `runtime.start(explicitDatabasePath)` only when
its configured roots and provider use are approved. Defaults remain no roots,
catalog support off and all online providers disabled. To enable source endpoints,
assign `LocalMetadata::Config::sourceEndpoints()` to indexing.metadata/enrichment,
then explicitly enable the desired provider configurations. IMDb has its source
suggestion base URL but remains disabled until explicitly enabled.

Alternatively use `LocalIndexer::indexFile` directly as the existing addon's
`indexFileAsync` callback, retaining application-owned persistence/enrichment.
No caller needs to supply pre-parsed filenames anymore.

## Evidence

Pinned source SHA-256:
`405eb494d6708406a30e716c3cfb5abae7a5e9c7a8b79446d64c3f821385930f`.
Executed oracle modules: M304, M501, M1057, M1069, M243, M1067 and M1079.

- 588 native/source filename comparisons across 49 nonempty paths, strict/loose,
  inside/outside priority and file-size thresholds. Empty/root-only source inputs
  throw; native handles them safely and those source throws are not counted as passes.
- 1,063 native/source comparisons covering suggestion ranking and actual retry URL
  sequence, parsed/synthetic-byte torrent indexing, native torrent projection,
  search-term transformation for U+0000 through U+040F, and append-log duplicate,
  corrupt-record and version-repair behavior.
- Real loopback tests exercise filename-to-entry recognition, suggestion requests,
  indexing composition, cancellation, owner teardown and runtime stream projection.
- Dedicated tests prove ordinary parse still provides geometry, metadata-only parse
  preserves info hashes while omitting geometry, and a torrent declaring 10^15 bytes
  with piece length 1 is indexed without proportional memory allocation.
- Synthetic temporary fixtures cover sparse media files, torrent files, log repair,
  metadata-cache reload and bounded malformed/deep/oversized torrent input.
- Built with GCC C++17, Qt 6.11.1, `-Wall -Wextra -Werror`, linking existing HTTP,
  torrent metadata and policy targets. Fixtures rerun independently of oracle access.

All tests used disposable synthetic data and loopback HTTP. No public provider,
actual user filename/media, HOME scan, listener binding change, account setting,
credential or security/privacy permission was involved.

## Deliberate limits and remaining differences

- Explicit roots, no symlink traversal, online opt-in and platform discovery limits
  in the previous report remain unchanged. No default activation is requested.
- Safe malformed-input limits/timeouts and safe empty filename handling differ from
  unbounded or throwing source behavior. Source callback errors are represented by
  an empty object in the indexer; the addon does not index that error result.
- Native filename parser supports the tested M304 surface; unusual JavaScript
  coercion/regex edge cases beyond the fixture corpus are not a universal proof.
- Source M1066's optional provider/hintUrl arguments and exact ASCII cache-key
  collision quirks are not exposed. The normal M501 query pipeline is implemented.
- Queue bound/coalescing/cache are implemented; exhaustive source namedQueue timing
  differential coverage remains outstanding.
- Uncached `bt:` metadata is now supported through optional
  `LocalAddonRuntime::Config::lookupTorrent`. The application supplies an EngineRegistry
  create/statistics adapter; the addon never makes a hardcoded self-HTTP request.
  Pass statistics JSON unchanged: source M501 ignores `sources` without `announce`
  and yields `sources:null`. A source differential fixture locks this behavior.
  Empty provider results denote errors and produce HTTP 200 `null`. Logical request
  cancellation and owner teardown complete pending responses and discard late results,
  without removing shared engines. Valid empty-object engine responses are treated as
  errors rather than source's degenerate empty metadata object.
- Additional addon tests now execute 109 source comparisons, including absent/null/
  false/zero/invalid/ranged year fields, omitted generic-meta fields, and uncached
  torrent-meta success/failure. Source-date projections and native deferred-route
  success/cancellation/owner teardown are covered. There is no direct registry adapter
  in the addon library; application composition owns that optional callback.
- Express SDK OPTIONS/HEAD/error-path details and source incidental array-mutation
  effects still need comprehensive differential coverage.
- Append/cache files are created only at explicitly requested locations. Existing
  application composition must deliberately adopt the runtime; merely compiling
  these components does not enable scanning or network lookups.

New C++ code remains under the repository MIT license. The source local-addon
package identifies its license as MIT; no third-party runtime or bundled media was
added. Character mapping fixtures are derived from execution of the pinned source.
