# Tankoban Top Comics Explore Design

**Date:** 2026-07-14
**Owner:** [Agent 1 (Codex), comics]
**Status:** Approved for planning

## Outcome

The `Explore` action beside **Top in Tankoban — Comics** opens a dedicated cover-first directory for the complete 688-series catalog. The Tankoban home widget becomes a true Top 10, showing only the first ten resolved series. The full page presents every resolved series in the same canonical order, renumbered cleanly from 1 through 688.

This page is the catalog door. It does not replace or duplicate **Explore Comics**, which remains the GetComics publisher/franchise archive taxonomy farther down the Tankoban home page.

## Visual direction

The approved direction is **A — Ranked library wall**.

The page stays inside Colosseum's existing wallpaper, glass, ink, and gold language. Its single visual signature is ranking at cover scale: each card carries a large, low-opacity ordinal behind or across the upper cover edge. Ranking is therefore structural information, not decoration.

The rest of the page is deliberately quiet:

- display typography uses the existing house display face for `Top Comics` and card titles;
- utility copy, search, counts, and filters use the existing UI face;
- `theme.ink`, `theme.inkDim`, `theme.gold`, `theme.edge`, and the injected wallpaper remain the palette;
- no new generic dashboard panels, gradients, or ornamental badges are introduced;
- the only compact status mark is a restrained gold acquisition indicator when at least one edition has a verified GetComics source.

The cover wall, not a hero banner, is the thesis. The first screen immediately shows ranked art and makes the size of the canon tangible.

## Page structure

### Fixed chrome

The page mirrors other full-screen Tankoban layers:

- Back capsule at upper left;
- search, minimize, and power controls at upper right where applicable;
- live shell wallpaper mirrored through `ShaderEffectSource`;
- a legibility wash consistent with `GenreIndex.qml` and `ComicArchiveBoard.qml`.

### Catalog header

The fixed upper content block contains:

- eyebrow: `TANKOBAN · COMICS`;
- title: `Top Comics`;
- live summary: `688 ranked series`;
- search field with placeholder `Search comics`;
- two filter pills: `All` and `Downloadable`.

The header remains visible while the wall scrolls. There are no alternate sort controls: canonical rank is the page's identity and never changes.

### Ranked wall

The body is a virtualized `GridView`, not a `Repeater` containing all 688 covers at once. Column count adapts to available width while keeping portrait proportions and desktop-scale density. Each delegate contains:

- clean display rank (`index + 1`), independent of gaps in the original RCO rank numbers;
- series cover, asynchronously loaded;
- titled gradient fallback when the cover URL is empty or fails;
- series title, capped to two lines;
- optional publisher when present;
- subtle gold availability mark only when at least one edition is genuinely downloadable.

The whole card is one action. Clicking it opens the existing `ComicSeriesPage.qml` route with the same `locg:<id>` identity already used by the home row.

## Interaction contract

### Home Top 10

`TankobanWorld.qml` retains the complete `comicRows` array after lazy catalog ingest but binds the home `TrendingTop10` to `comicRows.slice(0, 10)`. Card clicks resolve against that same sliced model. This guarantees exactly ten resolved series even though the original RCO rank field has gaps.

The widget's existing `Explore` label becomes live through an additive `exploreClicked` signal on `TrendingTop10.qml`. Tankoban forwards it as `comicCatalogRequested`.

### Search and filters

Search is case-insensitive and matches title first, plus publisher when available. It filters locally with no network activity.

`All` shows the complete 1–688 ranking. `Downloadable` retains only series with at least one edition whose `available` flag and `getcomics_post` are both present. Results preserve canonical relative order and retain their original clean display rank; filtering does not renumber the canon.

When search or `Downloadable` is activated from the unfiltered wall, the page saves the catalog scroll position. Clearing both returns to the prior unfiltered position rather than jumping to the top. Opening a series and returning also preserves the page because its Loader remains active until the catalog layer is explicitly closed.

### Navigation

The route is:

```text
TankobanWorld Top Comics Explore
  → ComicCatalogPage
      → existing ComicSeriesPage
```

`ComicCatalogPage` sits below the existing comic-series layer, so selecting a card opens the series over the wall and Back returns to the same search, filter, and scroll state. Closing the wall returns to the still-alive Tankoban home page.

## Components and data flow

### New `ComicCatalogPage.qml`

Owns presentation and local interaction state:

- input `rows`: complete ranked shelf rows from `ComicsDb.rankedSeries()`;
- derived `catalogRows`: rows annotated with `displayRank` and `downloadable`;
- derived `visibleRows`: title/publisher search and availability filter;
- signals for Back, system controls, and series selection.

The page does not import `comics_db.gen.js`. The multi-megabyte payload remains owned by the lazy `TankobanWorld.qml`; the full ranked row array is passed through the navigation signal into the page Loader.

### `ComicsDb.js`

Adds a pure query that determines whether a series has at least one honest acquisition source. The query checks the already-ingested edition records and never performs network work. Existing `rankedSeries`, `series`, `editions`, and `downloadPost` contracts remain intact.

### `TankobanWorld.qml`

- keeps all 688 rows in `comicRows`;
- exposes only `comicRows.slice(0, 10)` through the home widget;
- adds `comicCatalogRequested(var rows)`;
- forwards the Top Comics `Explore` action with the complete array.

### `Main.qml`

Adds one lazy catalog Loader and open/close functions. The Loader receives the passed rows, mirrors the wallpaper, connects Back/system signals, and routes card selection to the existing `openComicSeries` function.

### `TrendingTop10.qml`

Adds an `exploreClicked` signal and connects its existing `WidgetHeader.onMoreClicked` to that signal. This is additive and makes the already-rendered `Explore` label usable; existing consumers that do not connect the signal are unchanged.

## Empty, loading, and failure states

- Initial page creation uses the rows already held by Tankoban, so it has no network loading state.
- An empty source array renders `Catalog unavailable` and a direct `Back to Tankoban` action.
- A search/filter miss renders `No comics match this view` plus `Clear search and filters`.
- Cover failures use the existing titled gradient treatment; they never collapse card geometry.
- If the generated catalog failed earlier, the Tankoban home still uses its curated fallback and Explore receives that same smaller array rather than crashing.

## Performance

- The generated catalog stays behind the existing Tankoban lazy-load gate.
- The Explore page receives lightweight shelf rows; it does not parse or duplicate the 2.61 MB generated object.
- `GridView` delegates are virtualized and covers remain asynchronous.
- Filtering is local over at most 688 lightweight rows.
- Availability is annotated once when the page receives rows, not recomputed during every delegate binding.

## Verification contract

Pure/headless tests prove:

- Tankoban home binds exactly ten resolved comics;
- Explore receives all 688 rows;
- display ranks are sequential 1–688 even when source `rank` values contain gaps;
- search is case-insensitive and preserves canonical order;
- Downloadable includes only series with an honest edition source;
- clearing search/filter restores the saved unfiltered scroll position;
- all 688 visible-card identities resolve through `ComicsDb.series()`;
- `TrendingTop10` exposes and emits its additive Explore signal;
- root `Main.qml` still does not import or ingest `comics_db.gen.js`.

Runtime verification proves:

- root boot remains catalog-free;
- Tankoban logs 688 on first activation;
- the home Top Comics strip contains ten delegates;
- Explore opens the catalog wall, and a card opens/returns from the existing series page;
- no QML creation or invalid-property errors occur;
- `native/build-msvc.bat` ends with `BUILD_OK`, exit 0.

Hemanth's eyes-on check covers final density, cover cropping, rank legibility, sticky controls, hover behavior, and return-position continuity.

## Scope boundaries

- No publisher/franchise redesign; **Explore Comics** remains the GetComics Archives door.
- No torrent search or new download source.
- No SQLite runtime bundling.
- No full 24,341-edition browse.
- No changes to Comic reader/Biblio/A5 universe work.
- No arbitrary sorting, recommendations, ratings, pagination, or remote search in v1.

## Definition of done

- Tankoban home shows exactly ten resolved top-comics cards.
- Its Explore action opens a lazy full-catalog page containing all 688 resolved series.
- The wall is cover-first, virtualized, sequentially numbered 1–688, searchable, and filterable by honest availability.
- Every card opens the existing series/edition ledger and returns to preserved catalog state.
- GetComics Archives remains a separate taxonomy surface.
- Root startup stays free of the generated-catalog import.
- Deterministic tests, full MSVC build, runtime smokes, scoped diff review, and Hemanth eyes-on handoff are complete.
