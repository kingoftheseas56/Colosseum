# Theatre detail — Arctic Fuse 2 audit (surface #1 of the AF2 map)

**Date:** 2026-07-19 · **Owner:** A0 (multi-domain) executing, A4's Theatre lane · **Reference:** `Desktop/Kodi Reference/skin.arctic.fuse.2-omega` — `DialogVideoInfo.xml` + `Includes_DialogInfo.xml` (`_DialogInfo_VideoDetails`) · **Ours:** `qml/TheatreSeries.qml` · **Mock:** `agents/theatre-detail-af2-audit-mock.html` · **Status:** audit + mock done; awaiting Hemanth's eyes-on + build ratify.

## The delta (read from both, 2026-07-19)

| | Arctic Fuse 2 | Ours today |
|---|---|---|
| People | Director + writer as faces; full Cast widget row | **none** |
| Metadata | icon + label + value rows, two columns (Director/Studio/Genre/Language/specs/where-to-watch) | one thin inline line: `year · genres · ★ · runtime` |
| Below the plot | a hub: Cast, More Like This, Seasons, Extras rows that slide in (posy 160, delay 400) | ends at the episode list |
| Play button | bold "backing" primary control | modest `> Watch` pill |

## What we adopt (composition, not pipeline)
1. **Two-column fact block** replacing the thin meta line — labeled rows, hide-when-blank (AF2's own `<visible>` discipline).
2. **Cast row** — the single biggest gap on a media detail page.
3. **More Like This row** — turns the card into a hub (AF2's signature).
4. **Stronger Play button** — AF2 backing treatment; Watch reads as THE action.
5. **Slide-in choreography** for the below-plot rows.

## What we DON'T take
- JustWatch "where to watch" (needs TMDbHelper — banned). Our sources sheet already answers "where".
- Live-action cast **faces** (TMDB/fanart.tv — banned). Names as monogram chips instead.
- The TMDbHelper/fanart.tv art pipeline entirely.

## Data truth (probed 2026-07-19, so the mock promises nothing we can't get)
- **Cinemeta** meta (already fetched per title) carries `cast` (names), `writer`, `genre`, `country`. `director`/`network` present for many, null for some → hide-when-blank.
- **AniList** (anime lane) carries character faces + voice actors + studios + staff — the richer source for anime; use it there.
- **More Like This** = same-genre / same-franchise pulled from our OWN catalog (Cinemeta catalog + MAL genre catalog), never a recommendations API.
- Face circles: art where the source gives it (AniList characters), initialed monogram where it doesn't (Cinemeta name-only).

## Build tiers (each its own commit, mock-ratified first)
- **T1 (data we already fetch):** fact block + stronger Play button. No new network.
- **T2:** Cast row (Cinemeta names / AniList faces).
- **T3:** More Like This row (own-catalog by genre) + slide-in choreography.

## Testing
Pure logic (fact-row assembly, hide-when-blank, cast-source selection anime-vs-live) → headless harness. Wiring → grep contract. Feel → Hemanth's eyes-on against the mock, then the live app.
