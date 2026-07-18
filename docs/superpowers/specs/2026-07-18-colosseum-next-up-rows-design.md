# Colosseum — "Next Up" rows: Theatre + Tankoban manga (design)

**Date:** 2026-07-18 · **Owner:** Agent 0 executing both worlds on Hemanth's order (Theatre/Tankoban are A4/A1 lanes — noted for the record) · **Status:** ratified by Hemanth (Jellyfin library-end inheritance, 2026-07-18): "next up fits for every tankoban manga too (only manga, since comics catalogue isn't a linear one)". His eyes-on is the acceptance gate.

## What it is (plain sentence)
A home row per world that answers "you finished the last one — here's the next one": the next unwatched episode for shows you're current on (Theatre), and the next chapter/volume for manga you're current on (Tankoban). Western comics are excluded by ruling — that catalogue isn't linear.

## Why (and why it's cheap)
Jellyfin's home splits memory into *Continue Watching* (half-finished, we have it) and *Next Up* (you FINISHED the last episode — the show vanishes from Continue exactly when you're most committed). Every ingredient already exists in-repo:
- `ContinueRow.qml` takes a plain `items` model + resume/detail signals — the row shell is REUSED as-is, second instance titled "Next Up" (house doctrine: reuse, no new pages).
- `EpisodeBrowser.js` already owns `seriesRootId`, the 0.90 watched line (`rowState`), `episodesFor`/`seasonsFrom`, and `queueContextFromMeta` (builds the exact playbackContext the player eats).
- `Progress.recent(kind, n)` is the same feed Continue reads; entries carry `watched` + `resume`.

## Design

### Shared derivation rule (both worlds)
Group progress entries by series (most-recent entry per series wins — `recent()` is already recency-ordered). A series earns a Next Up card iff its most-recent entry is FINISHED (`watched === true || progress ≥ 0.90` — EpisodeBrowser's existing line) AND a next unit exists. If the latest entry is unfinished, the series belongs to Continue, never both rows. Pure derivation lives in JS (`NextUp.js`, `.pragma library`) so the headless harness proves it.

### Theatre (episodes)
- **Candidates:** `Progress.recent("video", 48)` → entries passing `EpisodeBrowser.isEpisodeId` (movies never card). Cap: first 8 finished shows.
- **Next episode:** per show, fetch series meta once (same TheatreApi call the series page makes; cached per app session keyed by show root). Next = same-season `episode+1`, else first episode of the next season (`seasonsFrom` order, Specials/S0 skipped — Jellyfin rule).
- **Card:** show backdrop/cover, title, sub "S2 · E5 · <episode title>", no progress bar (it's fresh).
- **Resume click (the circle):** resolve streams for the next episode id (`Torrentio.loadStreams("series", id)`), pick rows[0], open through the EXISTING `playRequested → openMovieSession` seam with `queueContextFromMeta(...)` as playbackContext — full prev/next + drawer work from the first frame. No streams → fall through to the series page (honest, never a dead spinner).
- **Detail click:** the series page (`{id: showRoot, type: "series", title}` — the ratified tile contract).

### Tankoban (manga chapters + tankoban volumes; comics EXCLUDED)
- **Candidates:** `Progress.recent("manga", 24)` + `Progress.recent("tankoban", 24)` (the reader's two non-western progressKinds). Finished line for a read: same shared rule (chapter read to the last page records progress 1.0).
- **Next unit (download-fed doctrine — reading never streams):**
  - kind "manga": next = the DOWNLOADED chapter with the smallest number strictly greater than the finished chapter's (numbers parsed from the stored labels, the series page's own ordering). Downloaded-only keeps the card honest offline.
  - kind "tankoban": same rule over the volume service's ready volumes.
  - Nothing downloaded past the finished point → the card still shows ("Ch 113 — not downloaded") and routes to the series page to grab it ("go download it" doctrine), styled dimmed.
- **Resume click:** downloaded → straight into the reader at that chapter/volume (the downloads-page routing seam, `openSeriesAt`-family). Not downloaded → series page.
- **Detail click:** series page.

### Placement
Directly ABOVE each world's Continue row (Jellyfin's order; the freshest intent first). Row hides itself when empty (ContinueRow already does this).

## Out of scope
Western comics (ruled out) · Biblio books (no linear next-unit) · unwatched-count badges + watched ticks on series cards (the second Jellyfin inheritance — separate thread, not yet ratified) · cross-device state.

## Testing
`NextUp.js` derivations (per-series latest, finished gate, next-episode across season boundary, S0 skip, manga numeric-next, not-downloaded disposition) → headless qml.exe harness beside the EpisodeBrowser tests. Row wiring → grep contract. Feel (cards appear only when current, resume lands correctly) — Hemanth's eyes-on.
