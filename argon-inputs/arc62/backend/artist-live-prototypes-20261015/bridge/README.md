# Arc 62 - isolated ArtistServiceBridge proof

This directory is **reference-only**, not linked into Colosseum. All project writes and build receipts remain here. Frozen LIVE source: codex/vinyl-live-listening at 860b96ae27a59c955f6aa9bf4642351ed8ce21e3.

## Interface and ownership

- `ArtistServiceBridge` is a QtCore QObject with Q_INVOKABLE `enterArtist(profileScopeId, artistMbid)`, `leaveArtist()`, `setProfile(profileScopeId)`, `requestArtist(artistMbid)`, `requestDiscography(cursor,filter)`, `requestMedia()`, `requestArtwork()`, `refreshSavedArtist()`, `toggleSavedArtist()`, `retry(channel)`, `selectRecording(artistMbid, releaseGroupMbid, releaseMbid, recordingMbid)` and `selectRelease(artistMbid, releaseGroupMbid, releaseMbid)`.
- Independent `IArtistReadPort`, `ISavedArtistPort` and `IPlaybackCommandPort` are injected. This code imports no fixtures, provider.py, HTTP request URLs, torrent search or QML. The read port returns independent Artist, Discography (Release Groups), Media and Artwork projections, and mandatory source/provenance identity for Ready responses.
- `State` is a Qt enum (Idle, Loading, Ready, Empty, NotFound, Unavailable, Error); each lane has a typed Q_PROPERTY and separate contents. `result(channel)` carries state/error/source/profile/artist/generation/pagination. Saved membership is explicitly profile-scoped, keyed by Artist MBID, never artist name.
- `enterArtist`, `setProfile` and `leaveArtist` increment context generation, cancel reads and clear view/saved state. Return navigation does not touch playing audio. Same-lane refresh cancels only that lane; token + generation + artist + profile check fences late replies. **Port callbacks must be on the bridge's QObject thread**; cancellation is best-effort.
- A BG1 cursor is opaque. A continuation can only echo the latest cursor of the same artist/profile/filter/source revision. A Ready page has 1..100 distinct validated ReleaseGroup MBIDs; invalid/duplicated pages and wrong revision fail with typed errors, never fake pagination. Discography is Release Groups, not duplicated concrete releases.
- Exact selection requires Artist and Discography Ready, and a currently listed Release Group; it passes distinct ArtistMbid, ReleaseGroupMbid, ReleaseMbid and RecordingMbid values to the playback command port. The playback adapter **must independently validate edition/group and recording/edition relationships** before queueing. Provider video IDs are never substituted as canonical Recording IDs. Release selection is a browse command; only selecting a recording can change playback. Failure shows an error and can be explicitly retried.
- Redirected BG1 identities must be resolved upstream or returned as `Error` with `errorCode=redirected`; the bridge never silently rewrites an MBID. An offline saved store does not default to a false-saved result. The bridge does not blur artwork, own tabs, rank videos, or qualify BF2 officiality.

## Local build and tests (PowerShell)

```powershell
$here = 'C:\Users\Suprabha\Desktop\Preflight-Architect\arcs\62-vinyl\backend\artist-live-prototypes-20261015\bridge'
$cmake = 'C:\Qt\Tools\CMake_64\bin\cmake.exe'
$ctest = 'C:\Qt\Tools\CMake_64\bin\ctest.exe'
$env:PATH = 'C:\Qt\6.11.1\msvc2022_64\bin;' + $env:PATH
& $cmake -S $here -B "$here\build" -G 'Visual Studio 17 2022' -A x64 -DCMAKE_PREFIX_PATH='C:\Qt\6.11.1\msvc2022_64'
& $cmake --build "$here\build" --config Release --parallel 2
& $ctest --test-dir "$here\build" -C Release --output-on-failure
```

For per-case QtTest PASS/FAIL output on Windows, run `& "$here\build\Release\test_artist_service_bridge.exe" -o "$here\qt-tests.txt,txt" -v1; Get-Content "$here\qt-tests.txt"` with Qt bin on PATH. Tests need no service/network/account, production data or installation.

## Adopter warning

Frozen LIVE `VinylProvider` supports only browse search/album and play resolve; `VinylLiveWorld`'s artist signal routes to search. `VinylArtistData.qml` imports test fixtures; none of these is a production Artist API. Do **not** pass `VinylProvider.request("browse","artist",...)` or promote fixture album/YTM video IDs into MBIDs.

Implement later adapters for BG1 artistById/artistDiscography (including redirect and InvalidPage), BA5 profile-owned Artist membership, sourced artwork/media with BF2 evidence and a native exact-selection playback port; QML registration, real profile lifecycle, provider network wiring and runtime playback are separate acceptance work. This is an executable interface **prototype**, not production integration.
