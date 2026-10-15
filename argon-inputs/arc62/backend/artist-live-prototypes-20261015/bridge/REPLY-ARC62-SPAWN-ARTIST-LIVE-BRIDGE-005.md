TASK: arc62-spawn-artist-live-bridge-005
STATUS: DONE

# Scope and frozen evidence
Owner: **bridge** only. Implementation, CMake artifacts, executable harness, and logs are confined to:
`C:\Users\Suprabha\Desktop\Preflight-Architect\arcs\62-vinyl\backend\artist-live-prototypes-20261015\bridge`

Source inspected READ ONLY: `C:\Users\Suprabha\.codex\worktrees\vinyl-live-listening\Colosseum`
Branch: `codex/vinyl-live-listening`
Frozen and independently confirmed HEAD before and after: `860b96ae27a59c955f6aa9bf4642351ed8ce21e3`.
Original 9 modified and 2 untracked source/artist files were observed unchanged in Git status. No Colosseum source modifications, git commits, pushes, resets, deployment, provider login, dependency download, or shared/other-worker file writes.

Read the dispatch job, Arc62 backend README/BH1 positioning, BG1/BA5/BF2 laws, native `VinylProvider.h`, `VinylTorrentProvider.h`, `VinylLiveSession.qml`, `VinylLiveWorld.qml`, `VinylArtistData.qml`, `VinylArtistCatalogue.js`, and `qml/Main.qml`.

# Delivered implementation
`ArtistServiceBridge` is a compiled QtCore QObject, with independently injectable `IArtistReadPort`, `ISavedArtistPort`, and `IPlaybackCommandPort`. It provides Q_INVOKABLE artist/profile entry, leave and profile switch, artist lookup, Release Group discography(cursor,filter), media, artwork, saved-artist read/toggle, typed retry, and exact Recording/Release selection. Typed per-lane State is Idle/Loading/Ready/Empty/NotFound/Unavailable/Error; Q_PROPERTY contents and `result(channel)` include source provenance and errors.

Each request carries global token, context generation, explicit ProfileScopeId, canonical Artist MBID, lane, filter and opaque cursor. Artist/profile/return navigation increments generation, cancels pending requests, clears all visual and personal state; same-lane refresh only cancels that lane. Stale callbacks after cancel/switch/leave cannot project data. In-flight replies with wrong artist/profile, missing source identity, wrong continuation, contradictory payload, duplicate ReleaseGroup MBID or source revision are rejected. Cursor is echoed, never parsed/generated.

Saved Artist membership is read/written only under explicitly passed profile and Artist MBID and guarded against reentrant profile changes; offline state never silently becomes an unsaved result. Mere browsing and return navigation never send playback commands. Selecting a Recording requires Artist and Discography Ready, an active listed Release Group, and canonical ReleaseMbid + RecordingMbid; selecting a Release is a browse command and leaves the player alone. The playback port must independently verify edition/group and recording/edition relationships before accepting.

All helper responses are injected in the tests: NO imports of fixtures, provider.py, Youtube networking or in-progress parallel-owner headers, and no painting/cropping, tabs, or BF2 officiality logic in this bridge.

# Actual Windows execution
Toolchain: Laptop 1 BDC, CMake with Visual Studio 2022 x64, MSVC 19.44.35223.0, C++17, Qt6 Core/Test **6.11.1**.

- CMake configure: PASS, local `build/` generated.
- CMake Release native library + harness build: PASS.
- `ctest --test-dir build -C Release --output-on-failure -V`: **1/1 PASS**, 0 failed.
- Direct QTest with `-o qt-tests.txt,txt -v1`: **14 passed, 0 failed, 0 skipped, 0 blacklisted**, includes 12 substantive test cases plus setup/cleanup.
- Logs: `configure.txt`, `build.txt`, `ctest.txt`, `qt-tests.txt`. Actual `build/Release/test_artist_service_bridge.exe` and `build/Release/vinyl_artist_service_bridge.lib` exist.
- Static source dependency negative scan for test fixtures, provider.py and live Vinyl providers: NO MATCHES in bridge .h/.cpp.

Tests verify QMetaObject typed state, multi-lane independence, canceled replies after artist switch and return, late/foreign-profile callbacks, profile-saved separation and synchronous reentrancy, browsing without stopping a playing song, strong exact-recording/release selection, failed playback command followed by successful explicit retry, Empty/NotFound/Unavailable/retry, invalid cursor and foreign-filter requests, source snapshot/revision changes, missing provenance, duplicate or missing ReleaseGroup MBIDs, wrong Artist MBID, and invalid response identity.

Actual QTest terminal receipt: `Totals: 14 passed, 0 failed, 0 skipped, 0 blacklisted, 6ms`
Actual CTest terminal receipt: `100% tests passed, 0 tests failed out of 1`

# Exact owned deliverables
- `ArtistServiceBridge.h` (SHA-256 4589A11F6382DFB0DCDE1EC25839C24CDEAB729B5905689D0564C36F76E79727)
- `ArtistServiceBridge.cpp` (SHA-256 B4502B8CFB2F9373FA96E00A85BD5359D5BF67DF301B1139323E2D4DC8FE210F)
- `test_artist_service_bridge.cpp` (SHA-256 354DDCF242A25DDCD7A7E22E89F5A2D40FBF8DFE46577302450304458C7313EB)
- `CMakeLists.txt` (SHA-256 3379F7D3CF6EDA7B05600AC754CE53A907B7D3FE52C04F7EB177E0EA9C6EC047)
- `README.md` (build/run recipe and adopter interface guidance)
- `configure.txt`, `build.txt`, `ctest.txt`, `qt-tests.txt`, and CMake-generated `build/` including the MSVC executable/static library. All paths relative to the owned directory.

# Contract mismatch and adopter guidance
1. The frozen LIVE `VinylProvider` only accepts search/album/resolve operations and `VinylTorrentProvider` resolves audio sources. Neither supports artist endpoints. `VinylLiveWorld.qml`'s artist navigation goes to search, `Main.qml` loads VinylLiveWorld, and `VinylArtistData.qml` imports test fixtures. There is **no production Artist navigation or data service**.
2. Build a separate BG1 adapter for `artistById`/redirect and `artistDiscography` with Release Groups, opaque query-bound cursors and canonical MusicBrainz IDs. Match BG1 failure codes without treating Redirected as NotFound.
3. Bind BA5 Artist membership to actual host ProfileScopeId and make profile lifecycle call `setProfile`/leave/enter. Never key saves by artist display name or provider ID.
4. Have distinct sourced artwork/media adapters and apply BF2's strong/weak video relationship qualification upstream. Artwork layout, slides, blur and fallback are QML/presentation owners, not the bridge.
5. Add native playback-port adaptation that validates exact group/edition/recording ownership against authoritative BG1 mappings, then hands an exact selected recording to existing audio/session semantics without breaking the active song on browsing. Do not pass YTM video IDs as Recording MBIDs.
6. Register the QObject and enum for QML at future integration time, test real async/host-thread callback delivery and profile transitions, and qualify end-to-end runtime separately.

# Explicit unverified boundary
Only the isolated QtCore/QTest executable is compiled and executed on Windows; no production Colosseum library/QML integration, Arc65 corpus, real network adapter, BA5 durable storage, BF2 media, actual profile owner, native mpv playback journey, background-thread callback marshalling, or other OS/device runtime was built/tested. The port contract intentionally requires read callbacks on the bridge QObject thread; a future network adapter must marshal them before invoking completion. No other worker's output was read or linked. DONE means this bounded native reference seam is compiled and tested, **not** a production Artist feature.
