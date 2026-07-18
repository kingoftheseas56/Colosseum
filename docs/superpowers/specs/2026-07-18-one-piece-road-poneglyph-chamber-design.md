# One Piece Universe Page — Road Poneglyph Chamber

**Date:** 2026-07-18
**Status:** Approved direction — Hemanth granted full creative control
**Owner:** Agent 5 (Codex)
**Target:** Replace `qml/OnePieceUniversePage.qml` completely while preserving its provider-pinned content and routing contracts.

## 1. Product intent

The One Piece universe page becomes a chamber organized around four Road Poneglyphs. The stones are not decoration: they are the page's primary navigation and divide the catalog into **WATCH**, **READ**, **FILMS**, and **ADAPTATIONS**.

The page communicates through scale, position, art, labels, and motion. It does not explain its own metaphor. The previous sea chart, compass, island route, ship's log, wanted posters, nautical-blue palette, and all associated copy are retired.

## 2. Design principles

- **One signature:** four monumental crimson stones.
- **Minimal prose:** the sourced Wikipedia lead appears only in the hero and is capped at two lines. Below it, copy is limited to section labels, titles, years, counts, and action labels.
- **Spatial hierarchy:** the chamber hub comes before the catalog. Selecting a stone scrolls to its matching room.
- **Canon over search:** all works continue to use the curated provider identities in `Universes.js`.
- **No fake lore:** the four stones organize product destinations; the interface does not claim that the canonical Poneglyphs represent those media categories.

## 3. Visual system

### Palette

- **Abyss** `#08070A` — page ground
- **Basalt** `#17131A` — chamber planes and inactive surfaces
- **Poneglyph red** `#8E1826` — the four stones
- **Carved light** `#FFB35B` — glyphs, focus, and active edges
- **Bone** `#F2E6CF` — primary type
- **Sea glass** `#5CB8B2` — sparse secondary signal for watch/read routing

No blue nautical gradient, parchment, rope, compass, map line, or wanted-poster treatment survives.

### Typography

- **Display:** Fraunces, used only for “ONE PIECE” and room titles.
- **Interface:** Switzer when available through the app shell; Segoe UI fallback through `Theme.ui`.
- **Marks/data:** uppercase utility labels with controlled tracking; no paragraph-style captions below the hero.

### Aesthetic risk

The first viewport is deliberately dark and architectural. The four stones occupy more visual area than the title or buttons. A universe page usually leads with media art; this one leads with a physical idea and lets the art appear after the user chooses a route.

## 4. Page structure

```text
┌──────────────────────────────────────────────────────────────┐
│ ONE PIECE                                      WATCH  READ   │
│ sourced lead · two lines maximum                            │
│                                                              │
│       ┌────────────┐              ┌────────────┐             │
│       │   WATCH    │              │    READ    │             │
│       │  1 SERIES  │              │  8 WORKS   │             │
│       └────────────┘              └────────────┘             │
│                                                              │
│       ┌────────────┐              ┌────────────┐             │
│       │   FILMS    │              │ADAPTATIONS │             │
│       │ 17 TITLES  │              │  2 WORLDS  │             │
│       └────────────┘              └────────────┘             │
├──────────────────────────────────────────────────────────────┤
│ WATCH — one dominant anime panel                             │
├──────────────────────────────────────────────────────────────┤
│ READ — compact asymmetric cover field                       │
├──────────────────────────────────────────────────────────────┤
│ FILMS — two dense chronological ribbons                     │
├──────────────────────────────────────────────────────────────┤
│ ADAPTATIONS — two opposing widescreen portals                │
└──────────────────────────────────────────────────────────────┘
```

### 4.1 Hero and chamber hub

- “ONE PIECE” is the only large headline.
- The sourced Wikipedia lead remains beneath it, limited to two lines.
- WATCH and READ are small direct actions in the hero edge, not large promotional buttons.
- Four stones form a responsive 2×2 chamber at desktop width and a vertical stack at narrow width.
- Each stone shows only its route label and a count.
- Fine carved marks are drawn by `Canvas`; they are abstract geometry, not invented readable glyphs.
- Hover/focus sends a single light sweep through the selected stone and reveals a directional chevron.
- Click scrolls the main `Flickable` to the corresponding room.

### 4.2 Watch room

- One landscape anime panel using the pinned `tt0388629` identity.
- The art carries the room. Visible text: title, year, episode-scale fact, and **WATCH**.
- No saga chart and no arc descriptions.

### 4.3 Read room

- Eight curated manga identities appear as a tight, staggered cover field rather than a horizontal carousel.
- The first manga is dominant; the remaining works step down in size.
- Visible text: title and **READ** on hover/focus. No descriptive notes.

### 4.4 Films room

- Preserve the two curated film-era arrays and all 17 Cinemeta pins.
- Present them as two chronological ribbons, with years acting as the structural index.
- Poster, short title, and year only. No “ship's log,” sailing vocabulary, or film descriptions.

### 4.5 Adaptations room

- The live-action series and announced WIT remake occupy opposing widescreen portals.
- Visible text: title, year, and `UPCOMING` when applicable.
- Each opens Theatre with `{id, type:"series", title}`.

## 5. Interaction and motion

- One orchestrated entrance: title resolves first, then the four stones rise by a few pixels and illuminate in sequence.
- Section rooms reveal through opacity and short vertical movement when first reached.
- Hover motion stays local to the selected object; no ambient floating particles.
- Keyboard focus mirrors hover and remains clearly visible.
- Reduced-motion mode disables translation and sequential delays while retaining opacity/focus state.

## 6. Data and routing contracts

- `Universes.js` remains the only curation source.
- Preserve the current One Piece entry, including anime, first-read, 11 sagas, two adaptations, two film eras/17 films, and eight manga pins. The 11 sagas may remain in data for compatibility but are not rendered by this page.
- `watchSeries`, `watchMovie`, `watchRequested`, and `seriesRequested` keep their current payload contracts.
- Remote poster/backdrop URLs continue through the existing IPv4-safe providers.
- Missing art leaves a quiet basalt slot with the title; it never inserts a substitute work.

## 7. Component boundaries

The rewrite stays in `OnePieceUniversePage.qml`, following the bespoke-page house pattern, with focused inline components:

- `RoadStone` — route label, count, focus/hover, carved canvas.
- `MediaPortal` — anime/adaptation landscape destination.
- `MangaGate` — cover-first Biblio destination.
- `FilmFrame` — compact poster/year Theatre destination.

The root owns data resolution, scrolling, and route signals. Components receive provider-pinned records and emit only user intent.

## 8. Verification

- Extend `tests/onepiece_page_load_harness.qml` to retain all data and routing assertions.
- Add a One Piece P0 shape test that requires the four route labels and bans retired vocabulary/components: Captain's Chart, Log Pose, Ship's Log, Bounty Board, `SagaIsland`, and `WantedPoster`.
- Headless QML load must exit 0 at 1280×720.
- Verify empty/missing-art behavior without network substitution.
- Run the existing universe expansion/provider-pin gate.
- Final acceptance is Hemanth's eyes-on pass in the real Colosseum shell.

## 9. Definition of done

- The prior page is structurally and visually gone.
- The four Road Poneglyphs organize the entire experience.
- Below the hero, no prose sentence is needed to understand or use the page.
- Every interactive work keeps its verified provider identity and correct Theatre/Biblio payload.
- Page-specific and shared universe gates pass.
- Hemanth accepts the page visually.
