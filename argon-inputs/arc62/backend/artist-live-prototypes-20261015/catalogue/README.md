# Arc 62 Artist catalogue: isolated native proof

**Owner:** catalogue query, identity and pagination only. This code is **not**
integrated into the current Vinyl app, QML, playback, artwork or personal library.
The complete proof runs in process against an injectable queued fake provider.

## Reproduce (Laptop 1, Qt 6.11.1 / MSVC 19.44)

Run in PowerShell, substituting another permitted checkout path if needed:

```powershell
$owned = 'C:\Users\Suprabha\Desktop\Preflight-Architect\arcs\62-vinyl\backend\artist-live-prototypes-20261015\catalogue'
$cmake = 'C:\Qt\Tools\CMake_64\bin\cmake.exe'
& $cmake -S $owned -B "$owned\build" -G 'Visual Studio 17 2022' -A x64 '-DCMAKE_PREFIX_PATH=C:/Qt/6.11.1/msvc2022_64'
& $cmake --build "$owned\build" --config Release --parallel 4
$env:PATH = 'C:\Qt\6.11.1\msvc2022_64\bin;' + $env:PATH
& "$owned\build\Release\vinyl_artist_catalogue_harness.exe"
& 'C:\Qt\Tools\CMake_64\bin\ctest.exe' --test-dir "$owned\build" -C Release --output-on-failure
```

Build generates only under `catalogue/build/`. Source inputs from BG1/BA0
are referenced read-only by CMake, not copied or modified. Real run receipts
are `configure.log`, `build.log`, `test.log`, and `ctest.log` under this folder.

## Native contract

- `ArtistCatalogueQuery.h/.cpp`: Qt Core/C++17, BG1 `Artist`, `ReleaseGroup`,
  `Release`, `Recording`, `PageRequest`, `PageCursor`, `CatalogueErrorCode`,
  and BA0 strong MBID/provider reference types. `IArtistCatalogueProvider`
  supplies `lookup`, `releaseGroups`, and `cancel` via injectable callbacks.
- `begin(ArtistSelector::byId(...))` resolves a strongly typed ArtistMbid.
  `byExactName` checks **exact, case-sensitive** canonical names and aliases.
  A name shared by two MBIDs returns `Ambiguous`, never an arbitrary artist.
  Redirects return typed `Redirected { to ArtistMbid }`; the caller chooses
  whether to follow and must start a new session. Missing and offline are
  separate `NotFound` and `Unavailable` outcomes.
- `page(sessionVersion, PageRequest, callback)` returns groups, not editions.
  Repeated ReleaseGroupMbid entries merge their concrete ReleaseMbid editions,
  preserving per-edition release status and source/browse provenance. Conflicting
  canonical metadata, artist attribution or edition ownership fails closed.
  RecordingMbid and ArtistMbid remain distinct and source credits keep
  provider IDs separate. Multiple provider mappings for one RecordingMbid
  are retained in `additionalSources`, not converted into identities.
- `DiscographyOptions`: primary and secondary group types, first-release-year
  bounds, typed Release status filter, typed Oldest/Newest/Title sort. Within
  each field, allowed values use OR; across fields, AND. A status matches a
  group if **any** edition has that status; the group is emitted once with all
  its edition provenance (not only matching editions). Stable sort uses MBID
  as the final tie-breaker. Unknown years sort last.
- Cursors are opaque to consumers but are locally validated against canonical
  artist ID, normalized query/filter/sort, corpus revision and session generation.
  Limits are 1..100 as in BG1. Foreign, malformed, stale or incompatible cursors
  return `InvalidPage`; invalid filters/limits return `InvalidQuery`.
- Sessions are latest-request-wins. New begin/cancel and superseded page
  requests forward ticket cancellation; a queued provider's *late* callback
  is ignored even if it disobeys cancellation. Callbacks after destruction
  cannot access dead session state. Provider callbacks must arrive serially
  on the owning thread; a real threaded adapter must marshal them.

## Missing real provider endpoint / adoption instructions

Current frozen `scripts/vinyl/provider.py` supports anonymous album-only
`search` (filter=albums) plus `album` and `resolve`. Its `uuid5` labels are
**not** MusicBrainz canonical MBIDs. It exposes no artist detail/artist
discography adapter and does not supply exhaustive exact-alias candidates.
This prototype does NOT pretend that a live YTM artist endpoint exists.

Before adoption, implement a real `IArtistCatalogueProvider` adapter that:

1. Resolves MusicBrainz ArtistMbid to provider artist browse ID through
   verified mapping/provenance; preserves MBID across redirect/alias matches.
2. For a name lookup, returns ALL exact canonical-name/alias candidates or
   fails `Unavailable` if completeness cannot be established. Never silently
   choose from name collisions or manufacture MBIDs from provider text.
3. Retrieves MusicBrainz Release Groups and their concrete Release edition
   statuses with artist/recording credits. Keeps YouTube Music album/video IDs
   as `ProviderItemRef` only, not canonical entity identity.
4. Supplies immutable corpus revision identity and honours cancellation.
   The fake uses full artist discography snapshots per page to prove sorting
   and deduplication. **Production must instead paginate an authoritative,
   immutable, query-bound corpus at the BG1 service boundary** for scale.
5. Converts `ArtistResolution` / `DiscographyResult` into a separately owned
   native VinylProvider view-service adapter. Do not put Qt/QML, helper
   credentials, network calls, artwork, library actions or playback here.

Main app adoption, real catalogue transport, threaded/async source runtime,
live provider mapping completeness, corpus scalability and UI journeys remain
**unverified**. No branch changes, commits, pushes or deployments are part of
this proof.
