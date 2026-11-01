# Libraries (16) — fidelity and boundaries

This is a **standalone, local HTML/CSS/JS fitting study**, not a Colosseum implementation, QML patch, Android build, device verification, or publication. No live library state is read or changed. Each page opens at the existing world's Library tab after scrolling past earlier feature/continue sections; the surrounding shell, top bar, world tabs, and navigation remain present. The other world tabs and provider/account glyphs retain their visual presence but cannot open a real app route in an isolated mock.

## Reference precedence and scope

- Authority: reference/desktop/qml at a40f66be06f22cdbe75e97e7dc4f1108ab2f5d41. Latest approved Theatre/Tankoban/Biblio LibraryPage.qml, TankobanLibraryTab.qml, BiblioLibraryPage.qml and their world parents were inspected. Theme.qml, Main.qml, AdaptiveLayout.qml, WorldPage.qml, TopBar.qml, +android/TopBar.qml, PhoneShellHost.qml, PhoneWorldTabBar.qml, WideWorldRail.qml, TheatreTabBar.qml, WorldTabBar.qml and CataloguePosterCard.qml supply the surrounding shell and shared visual rules.
- Phone selection recovery: reference/alpha/qml at 77844eef0bd3b7fee2c1f884be4a76db6d63321d. Its three library pages, LibrarySelectionState.qml, LibrarySelectionHost.qml and LibrarySelectionMark.qml describe the approved pre-drift phone affordance, card menu Select entry, selection footer, batch actions and confirmation. These helpers do not exist in the approved desktop tree, so their absence is not an implementation blocker.
- Contrast only: reference/android-drift/qml at 9e896bfd5dac40ef3ceb0e26e166e85ed8beecd9. The substitute CollectionCardMenu.qml and CollectionMenu.qml do **not** define the desired style and were not transplanted.
- Context read: context/ANDROID-PORT-CLASSIFICATION.md and context/VIEWPORTS.json only. OnePlus measurements remain provisional. No unrelated agent, governance, or repository control files were accessed.

## Shared composition and control mapping

| Frozen QML owner | Standalone HTML equivalent | Preserved |
| --- | --- | --- |
| Theme.qml | style.css: @font-face, --gold, --ink, --dim, --muted, --edge | Local Segoe UI for UI, Fraunces for clock/editorial headings, warm gold #f0c44a, glass film, quiet borders, dark wallpaper wash. Font files remain references in the frozen tree and are not redistributed. |
| AdaptiveLayout.qml | app.js fitViewport and CSS --margin/--top-inset | 18px compact phone, 24px wider phone, 32px tablet, 54px wide. Android classification uses min(width,height) below 600, independent of rotation. |
| WorldPage.qml | .app-main, .library-viewport, .world-tabs | Single vertical library/world scrolling owner; tabs remain in context and sticky when content scrolls. |
| TopBar.qml and +android/TopBar.qml | .topbar, .clock-area, .world-pills, .system-actions | Existing clock/date, desktop world pills, local search/provider/tracker/account marks; Android world pills/Home omitted per current Android shell. No app-level navigation was invented. |
| PhoneShellHost, PhoneWorldTabBar, WideWorldRail | .phone-bottom / .world-rail | Existing five-tab bottom navigation under 840px and 92px left rail at/above 840px on Android; desktop keeps its regular topbar chrome. Safe-area bottom inset is simulated outside the frame. |
| TheatreTabBar / WorldTabBar | .world-tabs .tabs-glass and .tab-pill | Original five Theatre, four Tankoban and three Biblio labels, same gold active tab and rounded glass/ghost pills. |
| LibrarySelectionState.qml | app.js state.selected Set and selected-row handlers | Select from a phone card menu, then tap-to-toggle; selection cleared when leaving phone gate or changing the visible data view. |
| LibrarySelectionMark.qml | .selection-mark | 28px round outline, gold selected fill/check at the same card corner. |
| LibrarySelectionHost.qml | .selection-bar, .selection-summary, .selection-actions, .confirm-scrim | 128px portrait and 72px short-landscape bottom selection host, 48px action surfaces, Cancel/Select all/Invert/Remove and Theatre watched batch controls; original confirmation heading/copy/Keep/Remove. |
| Rejected CollectionCardMenu / CollectionMenu | **Not used** | Menus reproduced from original inline parent renderers, not flat-card/new-sheet replacements. |

All SVGs and artwork are loaded by relative paths from reference/desktop/assets. No third-party network, custom poster placeholder, component CDN, or custom UI font is introduced. Local review controls (preset, dimensions, safe-area, font scale and demo-state) are outside the Colosseum frame; they are not product controls.

## Theatre Library — theatre-library.html

| Approved component/control | Mock counterpart | Layout treatment |
| --- | --- | --- |
| LibraryPage.qml shelf ledger/counts | .theatre-header .ledger, .ledger-frag and .ledger-dot | **Saved / In progress / Unwatched / Watched / New episodes / Downloaded**, active gold underline and actual illustrative counts. At phone widths search stacks above ledger; fragments wrap instead of clipping. |
| Search your library | .search-box | 250x38 rounded desktop field; full-width 48px phone field, preserving exact prompt. |
| Sort/type/airing groups | .filter-bar, .filter-pill, .pill-divider | Last watched / Recently added / A–Z / Year; All / Movies / Series; All / Ongoing / Ended. Single horizontally scrollable 48px phone hit rail, all original choices remain reachable. |
| Library grid card | .gallery-item, .poster, .card-title, .card-meta | 148x222 desktop poster plus dark depth plates, new-episode gold chip, watched dim/check, download mark, progress hairline and year/type/airing metadata. At 412x900, three poster columns cap at 94x141 with the same image, border and metadata. |
| Original per-card ⋮ menu in LibraryPage | .dots, .card-menu, .menu-item | 200px dark rounded popup, local Lucide glyphs, Play/Resume, Details, Dismiss progress when applicable, Mark watched/unwatched, Remove from Library. Phone adds the alpha-approved Select entry; 48px phone menu rows, 38px desktop rows. |
| Phone batch watched handling | .selection-actions | Mark watched / Mark unwatched included only for Theatre. No simulated command changes actual watch records. |

## Tankoban Library — tankoban-library.html

| Approved component/control | Mock counterpart | Layout treatment |
| --- | --- | --- |
| TankobanLibraryTab.qml search | .tanko-toolbar .search-box | Desktop search at left 220x38; phone full-width 48px search. |
| Filter and sort groups | .tanko-toolbar .pill-scroll | Original All / In Progress / Downloaded and Last Read / Recently Added / A-Z pills. Desktop remains side-by-side; on phone the existing controls scroll horizontally after the search field, with no invented dropdown/sheet. |
| CataloguePosterCard classic profile | .tanko-gallery .poster, .classic-hover, .classic-play | Original rounded poster, focused border, classic hover-only gold play ring and title. The alpha phone portrait used the gallery profile; its portrait image emphasis/two-line title is retained without importing the rejected drift helper. |
| Per-card ⋮ popup | .tanko-menu | Original root-level 200px dark menu with **Remove from Library**, and original alpha phone-only **Select**. No additional items. |
| Saved mixed manga/comics wall | .tanko-gallery | Responsive grid: smaller phone 94px art cap, landscape phone reflows to more columns, tablet/desktop retain larger dynamic cells. Card press chooses original Resume-when-in-progress or Details; simulated only. |

## Biblio Library — biblio-library.html

| Approved component/control | Mock counterpart | Layout treatment |
| --- | --- | --- |
| BiblioLibraryPage.qml search | .biblio-header .search-box | Search by title or author, desktop right aligned 250x38, phone full width 48px. |
| Sort/state bar | .biblio-header + .filter-bar | Recently added / Last read / A–Z plus All / In Progress, same gold/ghost pill treatment. No invented Downloaded gate (the approved Biblio source intentionally omitted it). |
| Book wall | .gallery-item, .poster, .card-title, .card-author | Poster depth, gold reading progress, authors always below titles; phone titles wrap to two lines, authors remain subordinate. |
| Original inline popup | .biblio-menu | Conditional Resume, Details and Remove from Library; phone-only Select after Details; label-only item rows consistent with approved inline menu. |
| Card action | app.js actionCard | Resume where a reliable progress fixture is present, otherwise Details; recorded outside frame rather than opening a fictitious app screen. |

## Fitting changes (not a redesign)

The phone uses one full-height app viewport and faithful shell navigation, with only the layout transformations allowed by the assignment: search above pills/ledger, wrap/scroll already-existing filters, 3-column portrait images (94px cap), adaptive landscape columns, stable 48px hit surfaces for original touch controls, visible selection marks, and bottom-space reservation for phone navigation and selection. The OS status/navigation bars are not drawn as Colosseum UI. Menu placement clamps inside the visible app area above its reserved bottom region; long menus scroll rather than overflowing. The original card menu itself is used first; no behaviour alternative or newly drawn substitute is proposed.

Search, filter, sort, original tabs, original card menu and selection buttons have local mock actions. The full interactive library may be scrolled vertically; no second card-grid vertical scroll was invented. The menu long press uses the alpha 500ms threshold and cancels on movement. Tab switching, detail opening, resume, remove, watched state and shell buttons do not perform real operations; feedback appears only outside the frame. Removing from a selection confirmation does not change even the fixture data.

## Artwork fixtures and uncertainty

The frozen assets directory does not contain local copies of the remote saved-library poster URLs. Rather than create blank posters, fabricate covers, or fetch from a provider, these representative entries use existing **titled in-project artwork**: Dune, The Witcher, Jurassic Park, Harry Potter, The Lord of the Rings and Percy Jackson saga wallpapers; One Piece Romance Dawn, Baratie, Arlong Park, Dressrosa, Wano, Skypiea, Water 7, Marineford and Egghead art; Star Wars house/location imagery; and Gotham artwork. For Biblio these are saga wallpapers (not exact edition cover scans); for Theatre and Tankoban some are universe/stage banners (not original released posters). All titles and authors are illustrative content, **clearly declared above the preview frame**. Live Collection ordering/counts, exact sources, opening of actual Details/Resume, downloads, keyboard D-pad/controller parity, OS safe areas, keyboard occlusion and device runtime remain unverified.

## Review and reproducibility

Presets outside each mock: Android 412x900, 354x792, 900x412, 792x354; tablet 800x1280 and 1280x800; desktop 1280x700, plus editable dimensions, top/bottom insets, and font scale (100/115/130%). At widths above or equal to 840 Android shell uses the existing 92px wide rail, but selection still follows the shortest-side-below-600 gate. Tablet layouts show no phone selection action.

For an isolated HTML review server, root it at D:\Colosseum-Android-Phone-Mocks-20261101 and open /outputs/16-libraries/index.html. Relative ../../reference/desktop/assets and ../../reference/local-fonts URLs stay local. A headless Chrome file-URL capture method is included in capture.ps1. Its exact launcher, viewport and output filenames are self-documenting in that file; it uses its own temporary browser profile and no shared GUI tabs. See REPORT.json and actual captures/ files for what was verified: a PNG's requested filename alone is **not** proof. Visual captures are browser-rendered mock checks, not Colosseum runtime checks.

## Behaviour proposals

None. Faithfulness takes precedence; no new app controls, gestures, sheet styles, or missing pieces were silently introduced.
