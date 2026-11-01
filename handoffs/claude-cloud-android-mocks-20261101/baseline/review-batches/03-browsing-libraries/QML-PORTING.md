# Batch 3 — QML porting notes

These notes describe the finished HTML fitting choices and the original QML seams Codex should retain. They are implementation guidance, not a claim that the Qt application was changed or tested.

## Authority and boundaries

The approved desktop source is `D:\Colosseum-Android-Phone-Mocks-20261101\reference\desktop\qml\`, pinned at **a40f66be06f22cdbe75e97e7dc4f1108ab2f5d41**. The alpha recovery source is `D:\Colosseum-Android-Phone-Mocks-20261101\reference\alpha\qml\`, pinned at **77844eef0bd3b7fee2c1f884be4a76db6d63321d**. The rejected Android snapshot **9e896bfd5dac40ef3ceb0e26e166e85ed8beecd9** is not the design authority. In particular, do not substitute PhoneFilterSheet or PhoneChoiceHost for the original picker/sidebar presentation merely because those helper names occur in the input manifest.

Each filename below resolves under the desktop QML directory unless explicitly marked alpha. The original worker HTML remains under `outputs/`; the finished HTML is under this batch’s `finished-mocks/`. The live comparison links compare those two HTML versions. They do not depict a running approved desktop application.

## Page-to-component map

| Finished page | Original composition to retain | Important implementation seams |
|---|---|---|
| 07/biblio-home.html | BiblioWorld, WorldPage, FeaturedCarousel, CarouselSlide, ContinueRow, BiblioDiscoverPage, BiblioExplorePage, BiblioBookRail; ReadingDesk for the Home study | Keep Discover as entry; keep Explore alive across tab switches; See All pins Discover and returns to retained Explore state. |
| 08/theatre-discover.html | TheatreWorld surrounding spine; DiscoverBrowser, DiscoverSidebar, DiscoverPicker | Preserve Movie/Series/Anime source types, provider identity, category/filter grouping, original popups, and full carousel/Next Up/Continue context above the browser. |
| 09/theatre-catalogue.html | TheatreWorld, TheatreCatalogPage, TheatreCatalogRow, TheatreCatalogRules, RowIndexSidebar | Keep Movies/Shows/Anime ownership and live row ordering; pass original See All pins rather than inventing a new results surface. |
| 09/manga-catalogue.html | TankobanWorld, TankobanMangaTab, ContinueRow, TrendingTop10, PosterRail, RowIndexSidebar | Your Collection has no See All; `primaryMangaResumeEnabled: false` changes activation, not the visible manga icon. |
| 09/comics-catalogue.html | TankobanWorld, TankobanComicsTab, GenreMosaic, RowIndexSidebar | Preserve non-pinnable shelf rules; `navigable: false` suppresses the mosaic header link, not genre-tile activation. |
| 15/theatre-home.html | TheatreWorld, WorldPage, FeaturedCarousel, ContinueRow, DiscoverBrowser | Next Up precedes Continue Watching; both See All doors reach the scoped Continue page. Watch and Details retain their source owner’s detail signal. |
| 15/tankoban-home.html | TankobanWorld, FeaturedCarousel, ContinueRow, TankobanDiscoverPage | Keep Manga/Comics/Library routes and shared progress cards; preserve banner versus poster art kinds. |
| 15/universes-home.html | UniversesWorld, FeaturedCarousel, UniverseBanner, WorldTabBar | Preserve alphabetical universe banners, their marks/forms, and the source’s single Cosmere tribute action. |
| 16/theatre-library.html | LibraryPage; alpha LibrarySelectionState, LibrarySelectionHost, LibrarySelectionMark | Resume, detail, dismiss progress, mark watched/unwatched, remove, and phone selection remain separate signals. |
| 16/tankoban-library.html | TankobanLibraryTab; alpha selection components | Floating root-level card menu; membership removal does not become progress deletion. |
| 16/biblio-library.html | BiblioLibraryPage; alpha selection components | Preserve title/author search and original Resume/Details/Remove menu; use identity-based selection. |
| 20/search.html | SearchSurface, BiblioSearch, BackAction | Keep separate book/audio result truth, stale-request guards, query history, provider errors, and source-specific browse genres when porting the local fixture behavior. |
| 20/biblio-explore.html | BiblioExplorePage, BiblioExploreRules, BiblioBookRail, RowIndexSidebar | `moveRowBy`, `beginDrag`, `updateDragDelta`, `endDrag`, `restoreReorderFocus`; fixed mosaics stay outside customization. |
| 20/genre-index.html | GenreIndex, TheatreGenreIndex, BiblioGenreIndex | Preserve original grouped directory/editorial copy, explicit-content preference gating, and selected lane. |
| 20/genre-results.html | GenrePage, TheatreGenrePage, BiblioGenrePage | Keep original Detailed/Covers presentation and lane-specific metadata/sort rules; Biblio’s Readers sort is fixed. |
| 20/continue-see-all.html | ContinueSeeAllPage, ContinueSeeAll.js, ContinueTile | Preserve scopes `home/video/tankoban/book`; medium chips belong only to home; empty collection and empty filtered view use different copy. |

## Shared geometry and fitting choices

| Concern | Source value / rule | Chosen fitting treatment |
|---|---|---|
| Android world navigation | PhoneShellHost: width < 840 uses 72px bottom navigation; width >= 840 uses a 92px left rail | Apply this to every owned world. A 900×412 phone has the left rail. An 800×1280 tablet has bottom navigation. |
| Library phone selection | Alpha owner phone gate uses the short side < 600 | Keep this separate from navigation. A 900×412 window remains selection-eligible; an 800×1280 tablet does not. |
| World width | WorldPage / AdaptiveLayout use the width remaining after shell navigation | At 900px wide, use 808px for world metrics. Do not compute margins or hero height from the full window. |
| World margins | 18 at width <= 430; 24 below 600; 32 below 840; 54 otherwise | Apply on both sides. Standalone Explore now reserves the same board margins as its world owner. |
| World rhythm | Hero heights 260/300/340; section gaps 22/28/36; top spacing 12/30 | Preserve source branches and let the world scroll in short landscape. |
| Carousel text | CarouselSlide narrow when its own width < 600; compact copy when height < 360 or narrow | 34px narrow title; 42px compact wide title; 13px two-line compact blurb. Preserve title line clamping and original action labels. |
| Row index | 52px collapsed, 240px expanded; content left = rail width + 28 | Keep expansion in place. On narrow phones, fit expanded width to clamp(140px, 44% of available width, 196px), with a 16px content gap. |
| Discover sidebar | Original 52/240 source rail; DiscoverBrowser already has an Android overlay branch below 840 | Preserve that existing overlay behavior. Do not turn the independent RowIndexSidebar into an overlay. |
| Picker | DiscoverPicker: 150×40 trigger, 36px internal clear target; original 280px popup, maximum 380px height | Bound width by viewport minus 16px; choose above/below from available space; scroll the original menu. |
| Book rail | BiblioBookRail: 130×182 art, radius 8; 20px heading, 12px title, 11px author | Retain these at 1×. Ranked rails use 26px gaps and 30px leading space; unranked use 16px gaps. |
| Progress tile | ContinueTile world geometry 148×222, 46px central resume circle | Preserve actual play.svg, books.svg, manga.svg. Collection’s altered activation does not warrant changing its icon to a chevron. |
| Card menu | Root-level 200px floating menu; desktop dots 26px | Alpha LibraryPage explicitly uses 48px visible dots on phones. This was confirmed in source and retained. |
| Selection host | Alpha LibrarySelectionHost: 128px portrait; 72px short landscape; add SafeArea bottom | Reserve space instead of covering the final gallery row. Keep the horizontal action scroller in short landscape. |
| Text scale | VIEWPORTS.json requests 1 and 1.3 | Fixed text declarations now honor the harness scale. Compute line height from already-scaled text once; library title slots are exactly two lines. |

Most fixes are corrections to the HTML translation. The QML already has the canonical icons, proper ARGB colors, anchored chevrons, identity-based selection and source signals. Keep those originals; do not reproduce a worker defect in QML and then add a compensating layer.

## Sidebars, shelves and retained state

Use RowIndexSidebar’s original current-row highlight and independently scrollable list. Feed it the *visible customized order*, including live rename/hide/reorder state. A jump moves the content to the row and keeps the index open. Expanded labels wrap without pushing the toggle out of its edge anchor. Up/Down/Home/End move keyboard focus; activation keeps the owner’s row identity.

BiblioExplorePage already defines the correct reorder lifecycle: `beginDrag` creates a temporary order, `updateDragDelta` updates it, and `endDrag` commits it. `moveRowBy(key, delta, restoreKeyboardFocus)` implements the accessible Ctrl+Shift+Up/Down path. Reuse these methods and `restoreReorderFocus`; do not convert a drag handle into a generic “move down” action. The Fiction/Nonfiction/Audience mosaics remain fixed after the customizable shelves.

BiblioWorld retains both Discover and Explore rather than destroying them on tab changes. Preserve that ownership and their independent scroll state. The HTML cross-file route encodes Explore’s order, hidden rows, expanded index and scroll position in the return URL because separate documents do not share QML object lifetime. That URL mechanism is a fixture implementation detail, not a recommended QML persistence system.

The native Fiction mosaic maps to the controlled literary-fiction facet, Nonfiction to nonfiction, and Audience to young-adult. Pass the original pin objects through the real catalogue resolver. The local fixtures use simplified matching and can legitimately return an empty shelf; their labels and counts are not provider truth.

For BiblioBookRail, ranking suppresses the competing star rating only. The source label still reveals on pointer hover or keyboard focus. Keep the numeral behind the cover and above the caption region, and keep source/rating overlays from changing the card’s measured height.

## Pickers and focus

Retain DiscoverPicker’s grouped menu and its internal clear affordance. Clicking a selected pill body reopens the menu; clicking its clear target resets only that filter. Keep the original group headings, row styling and 380px maximum. Fitting may move the popup above its trigger and reduce its scroll height; it must not replace the popup with a new phone sheet.

Clamp popups in the root viewport coordinate system after accounting for safe insets and the current bottom/left shell. Preserve keyboard Up/Down/Home/End, activation, Escape and focus return. The HTML needed a guard against a pending no-op scroll event immediately closing a newly focused popup. This is a browser translation issue; QML’s actual content-position signals should be tested with focus/ensure-visible behavior, rather than copying that workaround mechanically.

The library menu must stay at its owner root, outside the clipped gallery delegate. Preserve `openMenu(row, x, y, invoker)` and `closeMenu(restoreFocus)`. Long labels and short landscape make the menu scroll; they do not justify a different menu design. ContinueTile’s keyboard context keeps Open details / Resume / Remove, with first-item focus and return to the originating tile.

## Library membership and selection

Use each page’s existing signal contract: Theatre exposes resumeRequested, detailRequested, dismissRequested, markWatchedRequested and removeRequested; Tankoban/Biblio keep their own original equivalents. Dismiss progress is distinct from removing a collection entry. Confirmation removal must update the displayed membership, clear/prune selection, and show the original empty state. Downloaded files and progress remain intact, as the confirmation explains.

Keep alpha LibrarySelectionState’s `toggle(row)`, `selectAll()`, `invert()`, `clear()` and `prune()`. Keys identify entries, not transient row positions. Re-evaluate selection against visible rows after filtering/removal. Preserve the 28px selection mark, gold selected outline, existing action names, and the original Keep/Remove confirmation. The alpha host’s failure text remains “Some items could not be removed. Try again.” Real partial-failure behavior is not simulated against a provider in these mocks.

The confirmation backdrop is QML ARGB `#99000000` (60% black). Its CSS equivalent is `rgba(0,0,0,.6)`; copying the QML literal into CSS made it transparent. No QML color change is needed. Similarly, fix the mock’s double-scaled title height without changing the QML’s intended two-line title.

## Search, genres and progress backlog

SearchSurface and BiblioSearch use **Android width < 840**, not the short-side selection rule. Preserve their plain, labelled BackAction and the original desktop search/chrome reservation. Phone results remain full-width rows, and desktop results use the original multi-column composition. At short landscape with the illustrative keyboard reservation, the finished HTML places Back and the field on one row so a scrollable result region remains available. This is layout fitting of existing controls.

The keyboard reservation is explicitly illustrative. At 792×354 with 24px top/bottom insets, the harness reserves 100px and moves the search region upward. It is not a measured Android IME size. Codex must bind the actual available window/input-method geometry and verify adjust-resize/pan behavior when device work is authorized.

Keep top match separate from restResults; a one-book search may legitimately display a Books count with no remaining book cards. Hide the audiobook section when that lane has no results. Preserve independent provider-error truth and stale-query cancellation; the finished HTML only demonstrates deterministic local success/empty states. Do not convert a provider failure into “No books found.”

BiblioWorld has retired its legacy genre index/results entry points in favor of native Explore mosaics and Discover pins. The compatibility pages remain individually reviewable here because they are explicitly assigned pages. BiblioSearch’s Top 10/Genres jump actions return to the owner; they must not silently reintroduce a retired BiblioWorld genre route. General Home, details, readers/players, accounts and provider actions remain original intents outside this batch.

For genre results, retain the original editorial standfirst and its source line limits; do not replace it with short invented copy. Manga may switch Readers/Score. Biblio stays Readers. Theatre uses watcher wording and its lean movie metadata, avoiding manga “Completed” labels. Detailed/Covers changes presentation of the same filtered set. The empty genre copy is “Nothing in this genre yet — try another.”

ContinueSeeAllPage scopes the same progress records and same ContinueTile, rather than making separate visual card families. Preserve Last Watched/A–Z/Z–A/Watched/Not Watched and home-only medium filters. “Nothing to continue.” means the scoped collection is empty; “Nothing here yet.” means a filter has no matching entries. The local fixture’s Resume/Details endpoints stop at the batch boundary; they do not launch a player or reader.

## P1 — narrow poster artwork decision

[decisions.html](decisions.html) shows the source-faithful narrow attempt, the preserved worker attempt, and an optional phone bleed alternative. BiblioWorld emits `artKind: "poster"`. CarouselSlide hides the inset below a 600px slide width, while its bleed source condition only includes non-inset art or narrow square art. Consequently, the approved poster branch has no artwork on narrow cards. The finished default reproduces that branch; it is not a missing asset/load error.

The optional `posterPhone=bleed` fixture shows the existing artwork layer on narrow poster cards. Hemanth must choose before Codex changes that source condition. It is not silently selected. Desktop remains gradient plus native-ratio inset. Actual portrait cover assets are still required to judge a live-cover crop; the bundled saga/universe artwork is representative and is explicitly declared as such.

## What Codex should verify after an authorized port

Use the same seven logical viewport presets and 1.3×/24px stress combinations listed in REVIEW.md. Check the original QML surfaces with real font metrics, root popup coordinates, focus return, actual IME geometry, and SafeArea values. Reuse the existing native models and signals; the HTML’s small arrays, sort values, URL return state, manual clock fixtures and intent status messages are not application architecture.

The decisive fitting checks are: no bottom-nav/left-rail gate confusion; no overlay RowIndex substitution; no popup clipped by a delegate or keyboard; no hidden final gallery row behind selection; no scale applied twice to line height; correct poster/banner branch; and retention of Explore customization/scroll state on See All return. Confirm provider cancellation/error truth and actual mutation outcomes in the application separately. No application build, installation, device operation or live service call was performed in this assignment.

## Final enlarged-text fitting refinements

TheatreTabBar/WorldTabBar keep their original equal-width gold/ghost pills. At 354px and 1.3×, constrain the Text width to the pill’s inner width and use `Text.ElideRight`; keep the full label in the accessible name. The finished HTML applies `overflow:hidden; text-overflow:ellipsis` to the same control. No new tab family or scrolling tab interaction was introduced.

For GenrePage/TheatreGenrePage/BiblioGenrePage, reserve the existing fixed Back/Search row before measuring the phone hero’s text. The finished phone column starts 60px below its hero top, keeps a 22px bottom inset, and allows intrinsic text height above the 270px portrait / 230px landscape minimums. In QML, use `height: Math.max(minimumHeroHeight, heroText.implicitHeight + 60 + 22)` for the fitted phone branch; preserve the source’s title/standfirst line limits and scroll the resulting page. Desktop retains its 300px hero; only the kicker’s start is moved clear of the Back capsule. Recheck localized long genre labels with actual Qt font metrics.
