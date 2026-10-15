# Arc 62 Artist personal backend proof

Owner: artist saved state and manual artist-created playlists only.
Implementation is isolated under this directory. The Colosseum source worktree,
BA0, and BA5 sources remain read-only. This is NOT runtime integration.

## Frozen inputs

- Colosseum: codex/vinyl-live-listening, SHA 860b96ae27a59c955f6aa9bf4642351ed8ce21e3.
- BA0 VinylDomainTypes, BA5 VinylLibrary, VinylPlaylist, VinylPersonalStatePersistence are compiled as read-only sources.
- Existing BA5 persistence schema: colosseum.vinyl.personal-state, writer v3, readers v1 through v3.
- Live VinylLiveSession.qml currently uses Collection with Vinyl album/browse IDs.
  VinylArtistData.qml uses name-indexed session fixtures. Neither is a
  canonical MBID personal-state consumer yet.

## Build and execute on Laptop 1 (PowerShell)

All CMake outputs and fixture files stay below personal/build.

~~~powershell
$root = 'C:\Users\Suprabha\Desktop\Preflight-Architect\arcs\62-vinyl\backend\artist-live-prototypes-20261015\personal'
$cmake = 'C:\Qt\Tools\CMake_64\bin\cmake.exe'
$ctest = 'C:\Qt\Tools\CMake_64\bin\ctest.exe'
$qt = 'C:\Qt\6.11.1\msvc2022_64'
& $cmake -S $root -B "$root\build" -G 'Visual Studio 17 2022' -A x64 "-DCMAKE_PREFIX_PATH=$qt"
& $cmake --build "$root\build" --config Release --parallel 4
$env:PATH = "$qt\bin;" + $env:PATH
& "$root\build\Release\artist_personal_harness.exe"
& $ctest --test-dir "$root\build" -C Release --output-on-failure
~~~

No network or dependency install, external write, model CLI, archive, git
operation, provider login, or production personal-state access is required.

## Implementation and contract

- ArtistPersonalState accepts host-supplied BA0 ProfileScopeId and canonical
  ArtistMbid/RecordingMbid strings, parsed to strong MBIDs before mutation.
  Provider IDs and display titles never substitute for canonical IDs.
- Saved Artist is BA5 set membership. Repeat save preserves savedAtMs and
  optional non-authoritative snapshot; toggle/unsave are durable.
- User-created playlists are BA5 Manual provenance with explicit opaque
  PlaylistId, PlaylistItemId, positive timestamps, names, and item order.
  Repeated RecordingMBIDs are allowed under different item occurrence IDs.
  Playlist list presentation is createdAtMs order, then PlaylistId for ties.
- PersonalBytesPort is the future adapter seam. AtomicFilePort is for hermetic
  tests: SHA-256(profile ID UTF-8) partitions filenames; BA5 payload carries
  the actual case-sensitive profile ID and restore validates it. Commits use
  QLockFile, QSaveFile and expected-prior-bytes conflict detection. State
  changes in memory only after successful durable commit.
- Empty/truncated/malformed JSON, unknown fields, invalid identity, future
  versions and profile mismatch fail closed, without overwrite or mutable
  state. BA5 v1/v2 load without invented display snapshots; the next changed
  transaction rewrites current v3.
- Catalogue is optional for save/mutation. Optional DisplaySnapshot title and
  artistCredit are bounded fallback text, never lookup or sync authority.

## Observed proof (2026-10-15)

MSVC 19.44.35223 x64 + Qt 6.11.1, Release build: PASS.
artist_personal_harness.exe: 5 PASS, 0 FAIL.
CTest: 1/1 PASS. See TEST-EVIDENCE.txt.

## Adoption boundaries

Reference file port must NOT be installed alongside Colosseum's profile
database as a second authoritative personal state store. Implement
PersonalBytesPort against a single profile-owned BA5/Collection transaction
instead, with commit acknowledgements and conflict handling. Map the real
ProfileContext scope to BA0 ProfileScopeId; resolve actual artist/recording
MusicBrainz IDs before calls; preserve Manual playlist and item IDs, and
treat snapshots only as non-authoritative display fallback. Existing QML
savedNames/customPlaylists are name-keyed/session-only and need explicit
canonical identity resolution before migration.

Not verified: production DB integration, artist QML/bridge, cross-device sync,
multi-process crash/power-loss durability, malicious filesystem adversaries,
album/browse-to-MBID resolution, or arbitrary manual reordering of the
playlist COLLECTION (BA5 persistence sorts playlists by ID; this adapter
shows creation chronology). Playlist ITEM order is preserved exactly.
