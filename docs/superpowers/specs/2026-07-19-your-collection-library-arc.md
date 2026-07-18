# "Your Collection" — the saved-items library (A4's next arc)

**Date:** 2026-07-19 · **Owner:** Agent 4 (Hemanth's call: "this is the next arc for agent 4") · **Surfaced by:** the Theatre AF2 audit mock — its `+ Library` button had nothing behind it, and Hemanth caught that the whole app invites collecting with no shelf to collect onto.

## Name it plain (reduction reflex)
It's the **third shelf**, and the app is missing it:
- **Continue** — what you started (auto, from Progress). Exists.
- **Next Up** — what's next (auto, derived). Exists (2026-07-18).
- **Your Collection** — what you *chose to save* (MANUAL, via `+ Library`). **Missing — this arc.**
- (Downloads — local files. Separate, exists.)

A saved-items list. Nothing more exotic than that — don't reach for a "smart list" engine.

## The gap (verified 2026-07-19)
- NO collection / watchlist / library store exists in native/ or qml/ (ProgressStore is history, not saved items).
- The ONLY `+ Library` button in the tree is a dead one in `BiblioBook.qml:329`. Every other detail surface has none — the AF2 mock introduced it for Theatre, which is what exposed the hole.

## Scope
1. **Store (native, Progress-store-shaped):** `CollectionStore` — `add(world, {id,type,title,cover})` / `remove(world,id)` / `has(world,id)` / `items(world)`, persisted to disk (its own json beside progress.json), a `revision` for reactive QML. Worlds: `theatre | tankoban | biblio`.
2. **The button, everywhere, as a TOGGLE:** `+ Library` when not saved → `✓ In Library` (filled) when saved. Wire it on every detail surface: TheatreSeries, BiblioBook, ComicSeries/GcdSeries, MangaSeries — and ideally the genre/browse cards' hover action.
3. **"Your Collection" row per world:** a `ContinueRow`-shaped shelf (REUSE the component) reading `Collection.items(world)`, placed with the other personal rows (above Continue, below Next Up — or Hemanth's call on order). Tapping a tile opens the detail; the row hides when empty (ContinueRow already does).

## Doctrine to hold
- Manual save is distinct from Continue (auto) — a title can be in Collection without ever being started, and stays after finishing. Don't conflate with Progress.
- Reading is download-fed: a saved manga/comic/book in Collection is a bookmark to the SERIES, not a promise it's downloaded — opening it lands on the detail page (go download it there), same as Next Up's not-downloaded card.
- Identity shape must match what each world's detail opener expects ({id,type,title,cover}) so a Collection tile routes exactly like a Continue tile (the universe-tile lesson: carry type or series open as movies and die).

## Testing
Pure logic (add/remove/has/toggle, per-world filtering, dedupe) → headless harness. Wiring → grep contract (every detail surface's button bound to the store; each world page carries the row). Feel → Hemanth eyes-on: save from three worlds, see each Collection row fill, toggle off, watch it leave.

## Relationship to the AF2 Theatre arc
Bundled: the AF2 Theatre detail redesign (`2026-07-19-theatre-detail-af2-audit.md`) puts a prominent `+ Library` on the hero — it only means something once this store exists. Build the store first (or alongside), so the redesign's button is live on arrival.
