# ARGON A2: wire the five Vinyl artist backend pieces into one service

Task ID: `20261015-vinyl-artist-backend-wiring-a2`. Runs in parallel with A1. No shared files.

## Goal (one sentence)
Join the five separately built artist backend prototypes into one tested C++ service that the artist page can later bind to.

## Where to read
Everything is in the GitHub branch `argon-source-vinyl` of kingoftheseas56/Colosseum, a reading snapshot of `codex/vinyl-live-listening` @ `860b96ae`. Paths below that start `arcs/62-vinyl/` live in that snapshot under `argon-inputs/arc62/` (see `SOURCE_README.md` there): `arcs/62-vinyl/backend/` maps to `argon-inputs/arc62/backend/`, and `arcs/62-vinyl/prototypes/62-vinyl-artist-v4-claude/` maps to `argon-inputs/arc62/v4-claude/`. Colosseum paths (`scripts/`, `native/`, `tests/`, `qml/`) are at the snapshot root. Submit changes against `codex/vinyl-live-listening` @ `860b96ae`, never against the snapshot.

## Where things stand
Five reference prototypes live in `arcs/62-vinyl/backend/artist-live-prototypes-20261015/`. Each has its own `.h/.cpp`, test and README, and each passes its own tests. They have never been linked together:

| Folder | Class | Role |
|---|---|---|
| `catalogue/` | `ArtistCatalogueQuery` | artist by MBID, release-group discography, cursors, aliases, redirects |
| `artwork/` | `ArtistArtworkResolver` | provenance and rights, dedupe, hero selection for 0, 1, 2 or 3+ images |
| `personal/` | `ArtistPersonalState` | profile-scoped saved artists and artist playlists |
| `media/` | `ArtistMediaQuery` | video and live mapping, exact provider IDs |
| `bridge/` | `ArtistServiceBridge` | QObject facade for QML; ports `IArtistReadPort`, `ISavedArtistPort`, `IPlaybackCommandPort` |

Read first:
- `README.md` and `REVIEW-ARC62-ARTIST-BACKENDS-20261015.md`, especially the six "Integration seams still open";
- each folder's README and `REPLY-*.md`;
- the bridge README section "Adopter warning".

## Build
1. A new folder `arcs/62-vinyl/backend/artist-service-composed-20261015/` with a `CMakeLists.txt` that compiles the five prototypes' sources from their existing folders. Do not copy or fork their code.
2. Concrete adapters:
   - `ArtistReadAdapter` implements `IArtistReadPort` by composing catalogue, media and artwork.
   - `SavedArtistAdapter` implements `ISavedArtistPort` over `ArtistPersonalState`.
   - `RecordingPlaybackPort` implements `IPlaybackCommandPort`. It validates the edition/group and recording/edition links as the bridge README demands, then emits one typed "play request" signal. It must not start real playback.
3. An injectable data-source interface (`IArtistSource`) under the read adapter, so A1's provider feed can plug in later. Ship it with an in-memory fake only. **Do not call provider.py, the network or any real service.**
4. One factory that builds a fully wired `ArtistServiceBridge`.

## Tests (QtTest, offline)
Drive the composed bridge end to end through the fake source:
- enter an artist, then get artist, discography page 1 and page 2, media, artwork and saved state, all reaching Ready;
- a redirected MBID returns a typed `redirected` error;
- an invalid cursor is rejected;
- switching profile clears saved state;
- a late reply after `leaveArtist` is dropped (generation fence);
- toggling saved round-trips through the personal store and survives a reload;
- `selectRecording` with valid IDs emits exactly one play request with all four MBIDs, and a mismatched edition is refused with an error;
- an artist with no artwork returns the deterministic fallback, not a fake image.

The five original suites must still pass unchanged.

## Boundaries
- Write only in `artist-service-composed-20261015/`. Do not edit the five prototype folders. If a prototype has a bug, report it with file and line instead of patching it.
- No QML, no Colosseum `native/` edits, no network, no real profile store.
- The target is Windows Qt 6.11.1 MSVC, the same toolchain the prototypes use. If you cannot compile in your environment, say so plainly and mark the build and tests as not run. Claude or Preflight will build and run them on Laptop 1. Never claim a pass you did not run.

## Return
Individual raw source, test and CMake files, any build and test logs you were able to produce, and a report covering the adapter design, every prototype defect found (file and line), and what was not run. **No ZIPs or other archives.** When uploading to Drive, upload raw files and do not let Drive convert them to .docx.
