TASK: arc62-spawn-artist-media-004
STATUS: DONE

## Completed boundary
Implemented, compiled, executed and locally verified a C++17/Qt Core, isolated
native ArtistMediaQuery proof under the one owned `media/` directory. This
is **not production integration**, live video playback or a source provider.
Source was read-only throughout. No changes to sibling worker areas.

## Frozen source and authority
- Colosseum: `C:\Users\Suprabha\.codex\worktrees\vinyl-live-listening\Colosseum`
- Branch: `codex/vinyl-live-listening`
- Observed start AND post-test HEAD: `860b96ae27a59c955f6aa9bf4642351ed8ce21e3`.
- Existing nine modified paths and two untracked QML paths remained present
  at the end. No source file, source Git index, commit or branch was changed.
- Read BF2 related-video contract and real BA0/BF2 prototype, BF3 playback
  contract and adapter, current dirty `tests/fixtures/vinyl-world/ArtistMedia.js`
  and `scripts/vinyl/provider.py`.

## Produced files (all inside media/)
- `ArtistMediaQuery.h` : source, mapping, result, thumbnail and intent API.
- `ArtistMediaQuery.cpp` : full query lifecycle, validation, mapping,
  BF2 qualification, deterministic grouping/dedup and gated intent logic.
- `test_artist_media_query.cpp` : 15 focused executable test cases.
- `CMakeLists.txt` : compiles reference BA0/BF2 source read-only, links Qt Core.
- `README.md` : exact Windows build/run commands, contract and adoption guide.
- `TEST-OUTPUT.txt` : actual MSVC build, standalone per-case PASS and CTest log.
- `build/Release/arc62_artist_media.lib` : compiled native proof library.
- `build/Release/test_arc62_artist_media.exe` : compiled test harness.
- `build/` : owned CMake/Visual Studio generated intermediates and CTest data.
- `REPLY-ARC62-SPAWN-ARTIST-MEDIA-004.md` : this canonical report.

## Behavior and trust boundaries
- Distinct typed ArtistMbid and RecordingMbid vs opaque ProviderId/ProviderItemId
  using actual BA0 code. A provider ID shaped like a UUID never becomes MBID.
- Injected ArtistMediaSource supplies source-backed exact provider rows,
  artist identity and snapshot. Independently trusted ArtistMediaMappingPort
  supplies exact artist/provider and optional recording relation plus evidence.
- Real BF2 `RelatedVideoCandidate::create` qualifies recording-linked records.
  Titles, fuzzy similarity, source-proclaimed officiality and first-result
  order confer no authority; official requires BF2 strong official evidence.
- Only explicit strong artist-only mapping may yield standalone live/alternate
  open media. Official/visualizer without canonical RecordingMbid is rejected.
- Qualified OfficialMusicVideo/OfficialAudioVisualizer can request Audio|Video
  switch **intent** only for exact expected RecordingMbid. Live/alternate
  never request direct switch; they remain explicit OpenMedia intentions.
- Video thumbnails retain their own image provider, asset ID and optional
  HTTPS display locator, not a song media URL or artist portrait.
- Exact duplicate provider IDs dedup, conflicting ones fail Invalid; video
  and live groups sort by deterministic stable identity keys.
- Ready, Empty, Unavailable, Invalid statuses; supersede/cancel fences,
  duplicate callback suppression, destructor-safe late replies, stale action
  rejection, explicit expiration boundary and provider failure handling.
- No network, no provider scraping, no video transport resolve, no BA2 player
  handoff, no QML/bridge, no provider or production data access.

## Actual local execution on Laptop 1
- CMake: `C:\Qt\Tools\CMake_64\bin\cmake.exe`, generator VS 2022 x64.
- Compiler: MSVC 19.44.35223.0; Qt Core 6.11.1; C++17; Release build.
- First compile PASS; first CTest exposed provider-exception callback dropped
  due to moving a completion function. Fixed callback ownership in owned CPP.
- Rebuild: `BUILD_EXIT=0`; standalone: `HARNESS_EXIT=0`,
  `PASS total=15 failed=0`; CTest: `CTEST_EXIT=0`, `1/1 Passed`.
- Negative controls explicitly passed: empty/no fabricated media, provider
  officiality spoofing, strong fuzzy relation invalidation, cross-artist/item
  mismatch, cross-recording switch rejection, live/alternate switch denial,
  conflicting duplicates, malformed thumbnail/duration/source envelope,
  provider and mapping failures, expired action, stale/cancel/reentrant and
  late callback after service destruction. See `TEST-OUTPUT.txt` for all names.
- Source SHA/branch/status were rechecked read-only after tests.

## Discovered contract / source mismatches
- Current `ArtistMedia.js` fixture has actual provider-video observations
  but **no trusted canonical artist/recording mappings**, trustworthy BF2
  officiality, or explicit sourced live-performance classification. Treat
  its entries as unqualified, not as proven official/live records.
- Existing `scripts/vinyl/provider.py` only handles album search/detail and
  `bestaudio` audio resolve; there is no video/live ArtistMediaSource producer.
- The extra ArtistMbid envelope and artist-only performance mapping are a
  documented additive reference boundary; BF2 itself is recording-scoped.
- BF3 adapter/BB5 were inspected but not linked or executed by this proof.
  An intent returned here is not a resolved video or functioning player.

## Adopter guidance / unverified work
Implement the independent source port and trusted relation ledger; never
infer live/official from titles. Wire ArtistMediaQuery from real ArtistMbid;
pass only a BF2-qualified switch selection into BF3
`VideoPlaybackSelection::create(expectedRecording, candidate)`, then BB5
with its own request sequence and expiry/stale enforcement. For live and
alternate performances dispatch the typed OpenMedia provider-ID intent via
a distinct explicit open path, not the Audio|Video switch. Retain image
security checks at the actual fetching boundary.
Unverified: live provider fetching, true mapping provenance/trust, real
official/live inventory, remote image policy, actual BF3/BB5/BA2 processing,
QML binding, runtime playback, source integration, deployment.

Stop boundary: standalone prototype is implemented and tested. No further
work or mutation outside the permitted owned directory was attempted.