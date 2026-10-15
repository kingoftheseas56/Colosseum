# Vinyl Artist Page V4 — Claude HTML prototype

Status: v2, 2026-10-14. Built and tested by Claude, revised after Preflight's and Hemanth's first review. Awaiting visual review. No QML was touched.
Arc: 62 — Vinyl. Brief: `../../experience/CLAUDE-ARTIST-V4-HTML-HANDOFF-2026-10-14.md`.

## Open it

Double-click `index.html` (works from disk, no server). Three states:

- `index.html#eminem` — rich catalogue, artist image eligible (blended portrait hero).
- `index.html#lifafa` — sparse catalogue (2 albums, 1 single, 4 videos), artist image eligible.
- `index.html#easywanderlings` — no eligible artist image, so the page uses the current Colosseum wallpaper.

The same three links sit in the small source line at the bottom of the page.

## What is on the page

1. **Hero B** — compact blended portrait. The portrait is feathered with an elliptical mask into a larger blurred copy of itself, inside one continuous atmosphere that fades to darkness as the page scrolls. Popular Songs starts inside the first screen at 1600x900, 1280x720, 960x720 and 390 wide.
2. **Popular songs** — one ranked column, five rows, Show more extends the same column to ten. Small square art, title, album, explicit mark, duration. Queue and More appear on hover or focus. No play counts.
3. **Discography** — Collection Shelves: Albums and Singles (or Singles & EPs) on their own shelves; compilations, soundtracks, live albums and unclassified releases share one More releases shelf with the type in the caption. Each shelf appears only when data exists. Covers stand on a faint shelf plank with title and year below. Editions are grouped into one cover ("2 editions").
4. **Videos** and **Live performances** — the Theatre film band (dark, sprocketed, 16:9 stills) made stationary: no drift, manual paging, drag, keyboard. Each frame opens its exact video ID.
5. **Similar artists** — small framed near-black record badges with the artist photo as a 64 px label; monogram when no photo.

Rails page the way the worlds' Top 10 does (`TrendingTop10.qml`): no header buttons; a flat serif ‹ › sits on the rail's own edge over a soft dark scrim, appears only when there is more in that direction, turns gold on hover, and is not a keyboard stop. Every resting position (keys, chevron, drag release, end of rail) starts on a whole cover.

Not on the page by design: Album Room, Song Room, Credits, Collaborators, Information, biography, tag chips, dev controls.
The top bar and player bar are shown only for scale; both have native owners. The player stays idle: activating a song shows a small "Play request → native player" note with the exact video ID instead of faking playback.

## Keys

Tab lands on Play. Left/Right move along the hero buttons and along any rail. Up/Down move row by row through Popular Songs, then section to section; each rail remembers its position. Right on a song row reaches its Queue and More actions. Enter or Space activates. Home/End jump to the ends of a rail. Escape returns to the page, where Up/Down scroll 64 px and PageUp/PageDown scroll most of a screen. Mouse wheel, horizontal trackpad, drag and touch all scroll. No scrollbar is visible anywhere.

## Files

- `index.html` — the page (plain HTML, CSS, JavaScript).
- `data/artists.js` — page data generated from live YouTube Music and MusicBrainz pulls.
- `assets/` — artwork downloaded from the providers, the Fraunces font, the current wallpaper and Cold Ripple.
- `catalogue-evidence.json` and `CATALOGUE-EVIDENCE.md` — what was fetched, counts, gaps, image provenance.
- `QA.md` and `qa-results.json` — 93 automated checks, all passing, with notes on what was not tested.
- `screenshots/` — 32 PNGs.
- `ARGON-NOTE.md` — short note for the later QML port.
- `tools/` — the fetch, build and QA scripts that produced all of the above.

## v2 changes (2026-10-14, after review)

- Header ‹ › buttons removed; rails use the native Top 10 edge chevrons (Hemanth).
- Background rebuilt as one layer stack (blurred wash, portrait glow, feathered portrait, legibility shade, scroll-linked darkening); the page's top/bottom content fade, which read as a band, is gone (Preflight 1).
- Portrait leaves the page box and is feathered elliptically, so it has no rectangular edge; the artist name is larger at the same hero height (Preflight 2).
- Popular Songs was already five by default; the earlier screenshots caught it mid-test after Show more. The test now collapses it again before the next captures (Preflight 3).
- Compilations, soundtracks and other releases merged into More releases; rails clip flush and always rest on a whole cover (Preflight 4).
- Similar-artist portraits 44 → 64 px inside the same 132 px widget (Preflight 5).
