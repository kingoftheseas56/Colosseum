# Your Collection + AF2 Theatre detail — the larger arc (design, ratified)

**Date:** 2026-07-19 · **Owner:** Agent 4 (player/Theatre lane; Hemanth-ordered all-world sweep) ·
**Ratified:** Hemanth, this session, section by section · **Parents:**
`2026-07-19-your-collection-library-arc.md` (A0's capture),
`2026-07-19-theatre-detail-af2-audit.md` (A0's audit, surface #1 of the AF2 map),
`2026-07-19-arctic-fuse-2-reference-map.md` (the anchor) ·
**Mock:** `agents/theatre-detail-af2-audit-mock.html` (ratified as-is, with the rulings below).

## Name it plain
Two stacked sub-arcs, store first: (A) the app gets its third personal shelf — **Your
Collection**, the things you *chose* to save via ＋ Library — working end-to-end in every
world; then (B) the Theatre detail page adopts Arctic Fuse 2's composition (fact block,
cast, more-like-this, dominant Watch) below an **untouched episode list**.

## Hemanth's rulings (this session)
1. **Mock ratified as-is** — with the explicit guarantee that the episode list (season
   pills, Absolute/Seasons toggle, jump box, download flow) stays exactly as shipped.
2. **New rows sit BELOW the episode list** on series: hero+facts → episodes → Cast →
   More Like This. Movie detail (no episodes) uses the mock's order directly.
3. **All-world sweep now** (his call over the lane-respecting option): A4 wires
   ＋ Library on every detail surface this arc — Theatre, Biblio, Manga, Comics.
   Mitigation: one surgical commit per surface, announce on the haven chat wire.
4. **Row order on world pages: Continue → Next Up → Your Collection.**
5. **Trailer button OUT of scope** — a future arc; no YouTube-resolution lane here.

## Sub-arc A — Your Collection, complete

### A1. CollectionStore (native)
ProgressStore-shaped, registered as context property `Collection` beside `Progress`:
- `add(world, entry)` / `remove(world, id)` / `has(world, id)` / `items(world)` —
  worlds `theatre | tankoban | biblio`; entries `{id, type, title, cover, addedAt}`,
  newest-first from `items()`.
- `revision` int + `changed()` — name `Collection.revision` in bindings for reactivity
  (house pattern).
- Persistence: same mechanism as ProgressStore (QSettings; A0's spec said "json beside
  progress.json" loosely — Progress actually persists via QSettings, and the store
  follows the REAL house pattern). Hermetic INI-path test constructor, mirroring
  ProgressStore/SearchHistoryStore.
- Doctrine: every entry **carries `type`** (universe-tile lesson: tiles without type
  open series as movies and die). Manual save ≠ Continue: never conflated with
  Progress; an entry can exist unstarted and survives finishing.

### A2. ＋ Library toggle, every detail surface
`＋ Library` (not saved) ↔ `✓ In Library` (saved); tap toggles. Ghost-button treatment
per the mock. Surfaces: TheatreSeries.qml (series AND movie — same page), BiblioBook.qml
(replaces the dead button at ~:329), MangaSeries, comics detail (ComicSeries/GcdSeries).
The saved entry snapshots exactly what that world's detail opener needs to route back.

### A3. "Your Collection" row, every world page
REUSE ContinueRow. Placement: below Next Up (Continue → Next Up → Your Collection).
Reads `Collection.items(world)`; hides when empty; tap opens the **detail page**, never
player/reader (saved ≠ downloaded ≠ started — reading is download-fed doctrine).
**Tankoban tabs ruling:** with A1's Manga|Comics split, the row filters per tab by entry
`type` — manga saves on the Manga tab, comic saves on the Comics tab, no mixed row.

## Sub-arc B — AF2 Theatre detail (tiers as A0 cut them)

### T1. Fact block + dominant Watch (no new network)
Two-column labeled fact rows beside the plot (Director / Studio / Network / Aired /
Source), each row hide-when-blank (AF2's `<visible>` discipline; Cinemeta's
director/network are null for some titles). Meta strip stays. Watch gets the AF2
"backing" primary treatment; ＋ Library and siblings stay ghost. Movies too.

### T2. Cast row (below episodes)
Anime → AniList characters/VAs with real face art. Live-action → Cinemeta names as
initialed monogram circles (TMDB/fanart.tv banned — no-login law). "All cast ›" tail
opens the full list. Row hides when the source has no cast.

### T3. More Like This + choreography (below Cast)
Same-genre titles from OUR catalogs (Cinemeta catalog / MAL genre catalog) — never a
recommendations API. Tap opens that title's detail. AF2 slide-in motion for the
below-episode rows.

## Testing
- Pure logic → headless harnesses (house pattern, INI-backed store): collection
  add/remove/toggle/per-world+per-type filtering; fact-row assembly + hide-when-blank;
  cast-source selection anime-vs-live.
- Wiring → grep contracts: every detail surface's button bound to `Collection`; every
  world page carries the row; episode-list machinery untouched.
- Feel → Hemanth eyes-on: save from three worlds, watch each shelf fill, toggle off,
  watch it leave; then the AF2 page against the mock.

## Out of scope
Trailer playback · JustWatch/"where to watch" · any episode-list change · TMDbHelper/
fanart.tv anything · smart lists/tags/sorting beyond newest-first.

## Build order
A1 store → A2 buttons (one commit per surface) → A3 rows → T1 → T2 → T3. Every commit
leaves the app whole; if the wake is cut short, whatever shipped works.
