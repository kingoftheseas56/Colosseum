# Arc 62: Artist-specific media query (isolated native proof)

This is a compilable C++17/Qt Core native reference prototype, **not** a live
Colosseum Artist-page integration. New code and build output stay in `media/`.
The original BA0 and BF2 .cpp files are compiled **read-only** into this
standalone harness; no canonical source or peer worker code was patched.

## Provenance and scope

Inspected Colosseum branch `codex/vinyl-live-listening` at frozen SHA
`860b96ae27a59c955f6aa9bf4642351ed8ce21e3`, including the dirty
`tests/fixtures/vinyl-world/ArtistMedia.js` and `scripts/vinyl/provider.py`.
The fixture video IDs, video covers and titles are **provider observations**.
It supplies no independently verified ArtistMbid/RecordingMbid, officiality
or live-performance relationships. This code never upgrades those records.
Test MBIDs, curated links and live performances are synthetic **test-only**
inputs, not assertions about real songs, artists or performances.

`provider.py` currently provides album search/details and audio-only
`resolve` (`bestaudio`). It has no artist video source or external video
playback integration. Until a trusted source and mapping ledger exist, an
unmapped provider snapshot returns Empty, not invented official videos.

## Contract and behavior

- `ArtistMediaSource::fetch(ArtistMbid, callback)` supplies artist-scoped
  provider rows and snapshot metadata; harness never calls the network.
- Independently trusted `ArtistMediaMappingPort::lookup` must supply an exact
  ArtistMbid, optional RecordingMbid, provider tuple, BF2 relationship kind
  and structural evidence. Search title, channel, provider position and
  untrusted sourceClaim never establish officiality or live status.
- The service rejects wrong-artist envelopes, cross-scope mapping tuples,
  malformed source evidence, and conflicting duplicate provider IDs. Exact
  duplicate tuples deduplicate; order is deterministic identity order.
- BA0 types remain distinct. BF2 original code constructs recording-linked
  `RelatedVideoCandidate` and qualifies OfficialMusicVideo/Visualizer.
  Artist-only trusted mappings can admit *only* explicit LivePerformance or
  AlternatePerformance open items, never an audio-video switch.
- Results are typed Ready, Empty, Unavailable or Invalid. Videos and live
  performances are separate sections with canonical IDs and source/map
  provenance. Image provider/asset/HTTPS display locator is tracked
  separately from the playable provider item and from artist portrait art.
- The selected action is an opaque provider-ID open intent, or a BF2-qualified
  official-video switch intent. It rejects stale/cancelled tickets, wrong
  artist/recording, unknown items, and expired metadata.
- No resolved stream, headers, credentials, actual queue mutation, player,
  network, yt-dlp video resolution, or QML bridge is implemented.

## Build and execute on Laptop 1

PowerShell, using the already installed VS 2022, Qt 6.11.1 and Qt CMake:

```powershell
$root = 'C:\Users\Suprabha\Desktop\Preflight-Architect\arcs\62-vinyl\backend\artist-live-prototypes-20261015\media'
$cmake = 'C:\Qt\Tools\CMake_64\bin\cmake.exe'
$ctest = 'C:\Qt\Tools\CMake_64\bin\ctest.exe'
$env:PATH = 'C:\Qt\6.11.1\msvc2022_64\bin;' + $env:PATH
& $cmake -S $root -B "$root\build" -G 'Visual Studio 17 2022' -A x64 -DCMAKE_PREFIX_PATH='C:\Qt\6.11.1\msvc2022_64'
& $cmake --build "$root\build" --config Release --parallel 2
& "$root\build\Release\test_arc62_artist_media.exe"
& $ctest --test-dir "$root\build" -C Release --output-on-failure
```

Actual results: `TEST-OUTPUT.txt` contains 15 named PASS cases and CTest
1/1 PASS. All generated build outputs are under `media/build/`.

## Adopter instructions and unverified boundary

Provide the independent artist-media feed and canonical mapping ledger.
Do not map QML artist names, provider source IDs or synthetic UUIDv5 IDs to
MusicBrainz MBIDs merely because text looks similar. Only select a direct
switch when this service yields BF2 qualified candidate + matching
RecordingMbid, and pass it to real BF3
`VideoPlaybackSelection::create(expectedRecording, candidate)`, then into
BB5's request lifecycle. BB5 still owns fresh request/expiry/continuation;
do not invent a parallel BF3 asynchronous resolution system. Live items
must use `OpenMedia` via a provider-aware route using opaque ProviderId and
ProviderItemId, rather than direct Audio | Video replacement. The adopter
must validate actual network image fetches and provider-open policy.

Unverified: real provider-video ingestion; contents/trust of a real mapping
ledger; actual live video availability; real image fetching; BF3/BB5/BA2
integration; current Qt Artist UI binding; player behavior or release.