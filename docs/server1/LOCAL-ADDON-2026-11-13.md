# Native local addon: initial checkpoint

This first checkpoint is superseded where noted by `LOCAL-INDEXING-2026-11-13.md`,
which includes the native parser/provider/indexer/log/runtime additions and later fixes.

Source: official Stremio Service v4.21.1 server bundle, SHA-256
`405eb494d6708406a30e716c3cfb5abae7a5e9c7a8b79446d64c3f821385930f`.
Relevant modules: M1022 addon composition, M1023 SDK routes, M499/M1054 manifests,
M1055 catalog, M1056 meta, M1078 streams, M1079 storage, M1083 Linux discovery,
M501 indexer, M502 enrichment, M1068 metadata-name matching, M243 matching helpers.

## Implemented

- Native Qt/C++ ordered entry store, item-ID aggregation, both manifests, catalog,
  local/torrent stream projections, local/cached torrent meta and video projection.
- Explicit directory discovery with source Linux extensions, exclusions and depth;
  repeat discovery skips existing paths. Like source, deletion/modification does not
  invalidate records while the service is running; reload excludes unreadable paths.
- Versioned JSONL entry import and explicit atomic snapshot persistence.
- Owner-bound scheduled indexing, generation cancellation, late async result rejection,
  weak route-owner references, and async indexing/enrichment injection seams.
- Native opt-in HTTP metadata name provider, source simplify-name/year matching,
  parsed-file indexing, field-filtered Cinemeta enrichment and generic fallback.
- `/local-addon/manifest.json` and catalog/meta/stream routes, including the extra
  path segment. Missing meta serializes to HTTP 200 `null`, as M1023 does.
- No JavaScript runtime is required in the production implementation. Node runs
  only the pinned-source differential test oracle.

## Integration

Include `tests/cmake/LocalAddon.cmake` after the library target is declared, then call
`server1_register_local_addon_tests()`. The include adds the two native source files.
Construct `server1::local::LocalAddon` on the application thread. Its config defaults
have no roots and disable catalog support. `installLocalAddonRoutes(router, addon)`
mounts routes without owning the addon or starting a listener.

Set explicit roots and `indexFileAsync` only when authorized. An integration with a
full filename parser can call `LocalMetadata::indexParsedFile(path, parsed, callback)`.
Use `enrichMetaAsync` to connect `LocalMetadata::enrich`. Parsed queries contain name,
type and optional year/season/episode. All APIs/callbacks run on the owning Qt thread.
Callbacks supplied to asynchronous injection seams must complete on that same thread.
Completion of indexing means discoveries were indexed, not that all asynchronous
metadata enrichment finished. Cancel the metadata adapter separately when stopping
its owner; addon cancellation makes its outstanding results harmless.

`LocalMetadata::Config::sourceEndpoints()` returns source URLs but keeps `enabled`
false. Explicit enablement is required. Never silently enable online lookups or scan
HOME as part of composition. Responses have a 16 MiB limit and 15-second transfer
inactivity timeout by default. Network cancellation suppresses callbacks.

## Known deviations and unfinished parity (not completion claims)

- No implicit HOME/platform discovery, symlink following or automatic startup. Only
  explicitly configured roots are scanned. Source Linux uses HOME and `find -L`.
  The directory enumeration phase is synchronous and depth-limited; indexing work
  is cancellable between discoveries. Windows/macOS discovery is not implemented.
- Full M304 filename parsing and M1069 IMDb suggestion fallback are absent. The
  native metadata adapter receives already parsed queries; a filename alone cannot
  currently establish an IMDb match. M1066 queue/coalescing/12-hour cache and full
  Unicode search-term normalization are not implemented.
- Torrent-file parsing/discovery indexing and uncached `bt:` metadata's engine
  `/create` lookup are not implemented. Already indexed torrent records do have
  native catalog/meta/stream projection.
- Persistence is explicit atomic snapshot, not the source's open append stream.
  Metadata-cache disk persistence and automatic corrupt-log rewrite are absent.
- The source mutates shared file arrays during metadata sorting. Native projections
  do not reproduce incidental changes to later stream ordering from those mutations.
- Full SDK HTTP differential parity (OPTIONS/preflight and Express HEAD behavior,
  error middleware) is not yet established. GET body semantics are source-traced.

These intentional safe defaults need an explicit scope decision before broadening
filesystem discovery or online information transmission. There are no listener,
security-setting, account or permission changes in these files.

## Verification

Compiled both native sources and tests with GCC C++17, Qt 6.11.1 and
`-Wall -Wextra -Werror`, linking the current native HTTP target. Passed 20 comparisons
against executed pinned modules (manifests, catalog, streams, metadata, matching and
remote metadata-field projection). Synthetic temporary-directory tests cover new,
unchanged and deleted files, excluded paths, persistence/reload, cancellation and
owner destruction. Real loopback tests cover addon GET routing, metadata recognition,
parsed-file indexing, enrichment and cancellation. No user media was scanned and no
public network request was used in verification.

Implementation is newly authored native C++ in the MIT-licensed repository, with
behavior traced to the source modules above. The source's local-addon package M500
identifies itself as MIT. No bundled JS, third-party dependency or media asset is
added or redistributed by this change.
