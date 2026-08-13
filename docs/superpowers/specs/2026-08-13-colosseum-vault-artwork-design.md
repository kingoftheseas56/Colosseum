# Colosseum Vault — Browse Artwork (the wall of art)

**Design specification** · 2026-08-13 · Agent 0 (Claude, Opus) · **Status: locked design, ready to plan**

## 0. Context & lineage

Hemanth opened the finished Vault Browse face and asked *"where are the posters?"* The wall of art
had no art. This design fills it. It builds on:

- the Vault locked design (`2026-08-12-colosseum-vault-complete-locked-design.md`) — **§4.7**
  (typographic fallback, never an empty/broken frame), **§4.25** (canonical default / local override);
- the Browse face design (`2026-08-12-colosseum-vault-browse-face-design.md`).

**Ground truth that shaped it (all verified in code):** identification already derives a
Cinemeta/metahub poster URL (`VaultIdentifier.cpp:182`,
`https://live.metahub.space/poster/medium/<tt>/img`); the player already frame-grabs with ffmpeg
(`native/player/seekthumbnailer.cpp` — `ffmpeg -ss <t> -i <src> -frames:v 1 -vf scale=320:-2 -f mjpeg`);
comic/book covers already route in the browse grid (`dc8a5fd`, 2026-08-13); the Vault's cache dir +
`VaultStoreIo` already persist `durations.json` keyed by `(path, size, mtime)`. The V1 rule *"never
fetch remote artwork"* is deliberately relaxed here — **for artwork only, always through a cache.**

## 1. Experience promise & scope

**Promise:** the Vault's browse grid becomes a real wall of art — every tile shows the best picture it
can find for what it holds, and nothing is ever an empty or broken frame.

**In scope:** a per-tile artwork ladder; fetch-and-cache of canonical posters so they paint offline;
on-demand frame-grabs from individual videos, persisted; artwork carried up to Show/Season/Folder
tiles (which get none today); consistent card shapes with cover-crop fit.

**Out of scope (deferred):** the per-item override controls (use local / use canonical / lock this
poster) — §4.25; DLNA; any change to the ownership model.

## 2. The ladder — the one rule

Every browse tile resolves its picture by walking these rungs in order and **stopping at the first
that yields a local, ready-to-paint image**:

1. **Locked pick** — a user-chosen/locked image. *Field reserved; no UI this slice — always empty for now.*
2. **Local poster file** — an adopted `poster.` / `folder.` / `cover.` companion
   (`VaultEnricher::findLocalArtwork`, already shipping) → `file://`.
3. **Canonical poster** — the Cinemeta/metahub poster for a recognized item, **fetched once and cached.** *New.*
4. **Frame-grab** — a frame extracted from the video itself, **persisted and cached.** *New.*
5. **Typographic** — the real title set in type (§4.7). Existing fallback.

**Rung reach by node type:**

| Node | Own video? | Rungs it can reach |
|---|---|---|
| Folder / Show / Season | no | local file → canonical poster → type |
| Movie (single-video Film) | yes | local file → canonical poster → frame-grab (cover-cropped to 2:3) → type |
| Episode / Clip | yes | local file → canonical poster (rare) → frame-grab (native 16:9) → type |
| Comic / Book | archive cover | `image://comiccover` / `image://vaultbookcover` → type — **shipped** |

Folders/Shows/Seasons have no single video of their own, so they never reach the frame-grab rung — an
unrecognized folder shows honest type.

## 3. Card shape & fit

- **Poster tiles** (`VaultPosterCard`, 2:3): folder, show, season, movie, comic, book.
- **Wide tiles** (`VaultWideCard`, 16:9): episode, clip.
- **Shape is fixed by node type, never by which image resolved** — verified against jellyfin-web:
  card shape follows the view (`options.shape`), and the image is `background-size: cover`. Jellyfin
  does **not** swap a movie to a wide tile when only a wide image exists.
- **Fit is cover** (center-crop to fill). A native-16:9 frame in a 2:3 movie tile is center-cropped;
  an episode frame fills its 16:9 tile exactly; a 2:3 poster fills a poster tile exactly.

## 4. Primary journey

**First open:** the grid appears immediately with structure and titles. Tiles that already have cached
art show it at once; tiles without show the typographic treatment and, as each tile's art resolves
(fetch or grab completes), it **fades in in place.** The user never waits on a blank grid.

**Steady state:** recognized movies and shows wear their real posters; episodes wear a still from
themselves; comics wear their covers; local-only clips wear a real frame from the footage; anything
genuinely without a picture wears clean type.

## 5. Resolution, timing & load

- **On-demand:** art is requested when a tile enters the grid viewport — not the whole library up
  front. The wall opens instantly and fills as you scroll.
- **Canonical poster:** if the item is recognized (has an identity/metahub URL) and not cached, fetch
  the image once, write it to the artwork cache, then signal the tile to re-project (the same
  re-projection path identify-in-place already uses).
- **Frame-grab:** run one ffmpeg still (SeekThumbnailer's invocation) at **~10 % into the file**
  (never the first few seconds — skip black/logo), scaled, written to the artwork cache as a file,
  then signal re-project. A **season** with no season-specific catalogue art **inherits the show's poster.**
- **Concurrency:** a small bounded worker pool (a few jobs at once), newest-visible-first, off the GUI
  thread — QML paints, C++ decides. ffmpeg already runs as an async `QProcess`.
- **Cache:** persistent, in the Vault's existing cache dir (the `VaultStoreIo` `cacheDir` that holds
  `durations.json`). Frame-grabs keyed by `(path, size, mtime)` — the same key the duration cache
  uses, so a replaced file re-grabs. Canonical posters keyed by identity id. The cache survives
  restarts; art paints from it offline and when the drive is away.

## 6. States, interruptions, recovery, edges

- **Offline / drive away:** cached art paints; uncached tiles under an away root show type + the
  existing away treatment; **no fetch is attempted for away items.** Returning re-resolves them.
- **Fetch failure** (no network, 404): tile stays typographic; retried opportunistically on a later
  visit, never a broken-image glyph.
- **Grab failure** (dead/unseekable file): tile stays typographic (SeekThumbnailer already fails
  silently to no-image).
- **Wrong canonical match:** shows the wrong poster; **no in-app correction this slice** (accepted
  trade — the fix is the deferred §4.25 override).
- **Replaced/edited file:** mtime change invalidates the cached frame-grab; re-grabbed on next visit.
- **Local-only clips (e.g. Cricket):** get a real frame-grab from the footage — see §9.
- **HDR / odd source frame:** cosmetic only; acceptable.

## 7. Feedback, accessibility, integration

- **Fade-in on art arrival is the only motion.** No spinners on tiles — a typographic tile is a
  complete state, not a loading state.
- **House tokens only** (`Theme.qml`); no color/emoji/taglines; art sits inside the existing tile
  chrome unchanged.
- **Accessibility:** every tile keeps its real title as accessible text whether art or type is
  showing (already true).
- **Integration:** reuses the existing browse re-projection (`syncGridModel`) for "art arrived →
  repaint", the existing image-provider convention (comic/book), `VaultEnricher`'s cache dir + local
  artwork adoption, and `VaultIdentifier`'s already-derived metahub URL. The Player's frame-grab is
  the reference for the extraction; the **persisted-poster variant is the new part.**

## 8. Technical shape (enough to plan, not to dictate)

- A single **artwork resolver** in the Vault engine that, given a browse group, returns **one**
  ready-to-paint local ref by the §2 ladder, kicks off any missing fetch/grab, and emits a *ready*
  signal that triggers re-projection. QML binds one field (the tile already binds `coverRef`) — no
  QML art logic.
- `browseAt` already prefers `identityCoverUrl` then local/comic covers (as of `dc8a5fd`). The
  resolver **replaces the remote `identityCoverUrl` with the cached local ref** before it reaches the
  tile, so the tile never holds a network URL.
- **New producers:** (a) a poster fetcher (download metahub URL → cache file); (b) a persistent
  frame-grabber (ffmpeg one-frame → cache file). Both write into `VaultEnricher`'s `cacheDir`; served
  to QML as `file://` (like adopted local artwork) or a thin `image://` provider — implementer's call,
  as long as the tile binds a **local** ref.
- Bounded concurrency; GUI-thread-free transport.
- **No schema change** beyond what `FileRow` already carries (`identityCoverUrl`, `coverRef`, `path`,
  `kind`, `size`, `mtime`).

## 9. Reversal recorded — Cricket / local-only clips

The parent handoff constrained *"Cricket's clips stay typographic forever (local-only, no catalogue
match)."* That line predates the frame-grab idea; it assumed the only art was a catalogue poster. The
locked ladder **supersedes it**: local-only individual videos are exactly the case frame-grabs serve,
so Cricket's clips now show real frames from their footage. This is the intended improvement, not a
failure. **⚠ Flagged for Hemanth's veto at spec review** — if he wants local-only clips to stay type,
add a "type-only for local-only clips" branch at rung 4.

## 10. Acceptance criteria (observable)

1. A recognized movie/show/season shows its Cinemeta poster (cover-fit) in the browse grid, painted
   from local cache **with the network off.**
2. An episode/clip shows a frame grabbed from itself, native 16:9 in a wide tile, from cache.
3. An unrecognized movie shows a center-cropped frame filling its 2:3 tile; card shape unchanged.
4. A local-only clip (Cricket) shows a real frame, not type.
5. A comic/book shows its cover (already true; unchanged).
6. **No tile ever shows an empty frame or broken-image glyph;** missing art is always type.
7. The grid opens without waiting for art; each tile's art fades in when ready.
8. With a drive away or offline, cached art still paints; uncached tiles show type + away treatment;
   no failed fetch is visible.
9. Replacing a video file re-grabs its frame on next visit (mtime-keyed).

## 11. Non-goals / deferred

- Per-item override UI (use local / use canonical / lock) — §4.25, next slice.
- Backdrops / logos / banners — only the primary tile picture is in scope.
- Bulk pre-generation of the whole library's thumbnails — on-demand only.
- DLNA and any ownership-model change.

## 12. Discarded alternatives (with reasons)

- **Strict type-branched split** (folders = Cinemeta, all videos = frame-grab): rejected — a
  recognized standalone movie would show a random frame instead of its poster, and a show Cinemeta
  misses would show nothing.
- **Wide tile for unmatched movies:** rejected — two tile shapes for movies on one wall reads as
  inconsistent, and it is not what Jellyfin does.
- **Borrow an episode frame for an unmatched show folder:** rejected — noise; an unidentified folder
  shows honest type.
- **Eager whole-library thumbnailing at import:** rejected — slow first experience; on-demand paints
  fastest.
