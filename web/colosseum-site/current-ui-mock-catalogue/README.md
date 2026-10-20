# Catalogue tabs for the current-UI mock

Claude, 2026-10-08. A change set for Dots' "Colosseum current UI browser mock" (the folder delivered as `Colosseum-current-UI-mock-2026-10-08`, intended branch `dots/current-colosseum-ui-mock`, not yet committed anywhere when this was written).

## What it adds

Hemanth's report on the mock: "still doesn't show the catalogue or the sidebar icons." The world page had the hero, Continue row and the Discover wall, but its Manga and Comics tabs opened a generic search grid on another page. In the app those tabs switch in place and show the catalogue shelves with the row index beside them.

This change set builds the catalogue tabs of all three worlds from the QML at Colosseum master `2916fe85`:

- **Tankoban, Manga** (`TankobanMangaTab.qml`): Your Collection, Top in Tankoban — Manga, Explore by Genre — Manga.
- **Tankoban, Comics** (`TankobanComicsTab.qml`, shelf list in `TankobanWorld.qml`): Your Collection, Top in Tankoban — Comics, fifteen shelves (Recently Available … Fan-Made Shelf), Explore Comics.
- **Theatre, Movies / Shows / Anime** (`TheatreCatalogPage.qml`, row lists in `TheatreCatalogRules.js`, genre names in `TheatreGenreApi.js`): the "Customize rows" line, the ranked Top 10, then 17, 14 and 21 house rows, then the genre box.
- **Biblio, Explore** (`BiblioExplorePage.qml`, `BiblioBookRail.qml`, row order in `BiblioExploreRules.js`): the Explore heading with "Customize shelves", the ranked Top 10, six rails (Popular … Classics) with title and author under each cover, then the Fiction, Nonfiction and Audience tiles.
- **The row index** (`RowIndexSidebar.qml`) beside every one of them: 52px retracted by default and 240px open, pinned while scrolling, one row per shelf, the current row marked in gold, each row's icon chosen from its title by the same rules as the QML.
- `TrendingTop10.qml`, `PortraitTile.qml`, `PosterRail.qml`, `CataloguePosterCard.qml`, `GenreMosaic.qml`, `WidgetHeader.qml` and `CatalogueVisualMetrics.js` for the shelf, tile and genre-grid measurements.

## Files

Copy these over the same paths in the mock:

| File | Change |
|---|---|
| `shared/world-catalogue.js` | new: builds the catalogue tabs and the row index |
| `shared/world-catalogue.css` | new: their styles; also removes the dark band behind the pinned tab bar |
| `shared/world.js` | tabs switch in place when a catalogue exists, and the active tab is marked; `?tab=manga&y=600` opens a tab at a scroll position for checking |
| `demos/world.html` | loads the two new files |

## Checked

Rendered in Chrome on Windows at 1280x720: Tankoban Manga and Comics at two scroll positions each, Theatre Movies and Anime, Biblio Explore at its top and its end, and the Discover tab unchanged. Not clicked through by hand: the index toggle, row jumps and tab switching were checked by rendering states, not by interaction.

## Not done

- The top bar shows only the current world's name in the capsule; `TopBar.qml` shows all five world pills with the current one in gold.
- "Customize rows" and "Customize shelves" are drawn but do nothing.
- Staged content. The mock holds six manga, six comic, six film, three show and three anime covers, so shelves reuse them in rotation and genre tiles reuse them as backgrounds; publisher and decade shelves therefore show the wrong titles. Comic genre names and counts are invented. Biblio has two local covers; the other eight load from the Apple Books addresses the app itself uses, so they need a connection.
