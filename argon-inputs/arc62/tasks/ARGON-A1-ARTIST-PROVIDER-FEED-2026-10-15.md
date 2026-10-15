# ARGON A1: Vinyl artist data feed (provider.py artist operations)

Task ID: `20261015-vinyl-artist-provider-feed-a1`. Runs in parallel with A2. No shared files.

## Goal (one sentence)
Teach Vinyl's provider to answer "give me this artist", so the artist page can show a real catalogue instead of test fixtures.

## Where to read
Everything is in the GitHub branch `argon-source-vinyl` of kingoftheseas56/Colosseum, a reading snapshot of `codex/vinyl-live-listening` @ `860b96ae`. Paths below that start `arcs/62-vinyl/` live in that snapshot under `argon-inputs/arc62/` (see `SOURCE_README.md` there): `arcs/62-vinyl/backend/` maps to `argon-inputs/arc62/backend/`, and `arcs/62-vinyl/prototypes/62-vinyl-artist-v4-claude/` maps to `argon-inputs/arc62/v4-claude/`. Colosseum paths (`scripts/`, `native/`, `tests/`, `qml/`) are at the snapshot root. Submit changes against `codex/vinyl-live-listening` @ `860b96ae`, never against the snapshot.

## Where things stand
- The provider is `scripts/vinyl/provider.py` (117 lines) on branch `codex/vinyl-live-listening` at commit `860b96ae`. It handles only three operations: `search`, `album` and `resolve` (see `handle()`).
  - Native callers go through `native/vinyl/VinylProvider.h`. Tests live in `tests/vinyl-live/test_provider.py` and `fake_provider.py`.
- The artist page today imports fixtures from `tests/fixtures/vinyl-world/` (`EminemReleases.js`, `ArtistMedia.js`).
- The fetch-and-normalise recipe has already been proven against live YouTube Music. Read these and reuse their logic; don't redesign it:
  - `arcs/62-vinyl/prototypes/62-vinyl-artist-v4-claude/tools/yt_fetch.py`: `get_artist`, `get_artist_albums(limit=None)` continuation, and the songs and videos playlists.
  - `arcs/62-vinyl/prototypes/62-vinyl-artist-v4-claude/tools/build_data.py`: edition grouping, MusicBrainz release-group matching (including bilingual titles), edition-qualifier stripping, popular-song dedupe, and live-video split.
  - `arcs/62-vinyl/prototypes/62-vinyl-artist-v4-claude/CATALOGUE-EVIDENCE.md`: real counts, gaps and quirks for Eminem, Lifafa and Easy Wanderlings.
- The QML-facing contract the feed must eventually satisfy is `ArtistServiceBridge` (`arcs/62-vinyl/backend/artist-live-prototypes-20261015/bridge/README.md`). Discography is release groups, not duplicate editions. Identity is by stable ID, never by name. There are no fake ranks, counts or dates.

## Build
Add operations to `provider.py`, keeping its existing request/response style and existing operations byte-for-byte unchanged:
1. `artist`: input is a YouTube Music channel ID. Output: name, channel ID, artist images with their pixel sizes, the songs shelf in provider order (up to the first 10 after title dedupe, each with video ID, title, album name and ID, duration and explicit flag), the related artists (ID, name, thumbnail), and continuation handles for releases and videos.
2. `artistReleases`: input is the channel ID and the kind (`albums` or `singles`). Output is the complete list via continuation (`limit=None`), each entry with browse ID, title, year, type and thumbnail. Report whether the list is complete.
3. `artistVideos`: input is the channel ID. Output is the videos playlist (video ID, title, duration, thumbnail), with a separate `live` flag only when the title contains the word "Live". Fall back to the inline shelf when no playlist handle exists, and say so in the response.
4. `artistIdentity` (optional but preferred): input is an artist MBID or name. Output is MusicBrainz release groups mapped to the YouTube Music releases (key1, then key2, then Latin-only matching, as in `build_data.py`), each result marked with how it matched or as unmatched. Respect MusicBrainz rate limits: a User-Agent header, at least 1.1 s between calls, and backoff on 503.

Rules:
- Every response carries `source`, `fetchedAt` and, for lists, `complete: true|false`.
- On provider failure return a typed error; never return an empty list as if it were success.
- Never invent missing data.

## Tests
- Extend `tests/vinyl-live/test_provider.py` and `fake_provider.py` with recorded fixtures, committed as JSON. You cannot reach the network, so build the fixtures from the real responses in `argon-inputs/arc62/raw-ytm-pulls-20261014/` (`<artist>/artist.json`, `catalogue.json` and `mb.json`; ytmusicapi 1.12.3, location IN, fetched 2026-10-14). Cover:
  - Eminem: rich, with continuation;
  - Lifafa: inline shelves only, bilingual MusicBrainz titles, and videos with no durations;
  - Easy Wanderlings: small artist image, EPs;
  - an unknown channel: typed error;
  - a provider timeout: typed error.
- The tests must run offline. Add one optional live smoke test that is skipped by default.

## Boundaries
- Change only `scripts/vinyl/provider.py`, `tests/vinyl-live/test_provider.py`, `tests/vinyl-live/fake_provider.py` and new fixture JSON under `tests/vinyl-live/fixtures/`.
- No QML. No `native/` C++ changes; the C++ side is A2 and later work.
- No credentials or accounts. Unauthenticated ytmusicapi only, matching the existing provider.
- Images: return URLs and sizes only. Do not download or rehost them, because their rights are not cleared.

## Return
Individual raw changed files plus one plain `.patch` against `860b96ae`, test output, and a short report that marks anything not run. **No ZIPs or other archives.** When uploading to Drive, upload raw files and do not let Drive convert them to .docx.
