# Batch 2 — QML porting notes

## Authority and route selection

Use the frozen approved QML at `../../reference/desktop/qml/`, declared commit **a40f66be06f22cdbe75e97e7dc4f1108ab2f5d41**. Individual bytes are recorded in the input/source manifests. These notes describe the chosen HTML fitting work; no QML, repository or app file was changed.

The approved parent defines much of this presentation inline. MediaDetailScaffold.qml, UniverseScaffold.qml, UniverseFeed.qml, UniversePosterRail.qml and UniverseShelfViewport.qml are absent from this approved tree. Do not import similarly named helpers from the rejected Android branch. UniverseExtensionPage.qml is present.

Main.qml's `views.rebuiltTitlePages` default is false (around line 583); its detail route selection (around line 5234) keeps TheatreSeries.qml as the default. TheatreSeriesView.qml is the opt-in comparison. ComicSeries.qml is the active GetComics surface. ComicSeriesPage.qml labels itself PARKED; comic-edition.html studies that existing edition treatment without changing the app route.

### Component map

| Finished page | Original owner and shared components | Principal fitting work |
|---|---|---|
| Biblio | BiblioBook.qml; BackAction.qml; LibraryButton.qml; collections components | Existing jacket/copy layout, intact synopsis and source lists; chooser keyboard navigation; original Collections lifecycle; Android host visibility. |
| Theatre film / series | TheatreSeries.qml by default; TheatreSeriesView.qml when opted in; TheatreFacts.js; CastRow.qml; MoreLikeThisRow.qml; TheatreRemovalDialog.qml | Full handset synopsis, safe popup placement, short-landscape sticky ledger, correct episode jump coordinates, nested action handling and source removal dialog. |
| Comic series | ComicSeries.qml; existing GetComics rows | Growing release text and retained action hit areas, nested keyboard semantics, local status fixtures. |
| Comic editions | Parked ComicSeriesPage.qml; ComicDbLedger.qml; ComicTorrentSourcesPage.qml; ComicTorrentArchivePicker.qml | Edition secondary action, source-page insets/scrolling/focus, original confidence and archive outcome states. |
| Comic archives | ComicArchiveBoard.qml; GenreMosaic.qml; ComicArchiveIndex.qml; ComicSeries.qml for the raw shelf | Retained board/index; archive-scoped raw releases; navigation parameters and scroll restoration. |
| Saga / Shelves | SagaUniversePage.qml; ShelvesUniversePage.qml; UniverseTile.qml; UniverseChromeAction.qml; ContinueTile.qml; inline grids/rails/magazine delegates | Source Continue tile; one keyboard stop per collection; full size text and growing magazine rows; noncompact title size; conditional tab docking pending B2-018. |

## Shared foundation

Retain existing Theme colors: gold #f0c44a, ink #f7f7f5, dim #c9c8d0, dimmer #9a99a5 and the source translucent edge. Preserve Glass, BackAction and source control shapes. Most detail UI uses Theme.ui / local Segoe and Theme.display / Fraunces. Saga and Shelves explicitly choose Switzer for their own UI; their ContinueTile and BackAction still use Theme.ui. Do not substitute Switzer everywhere or replace the existing controls with a new phone vocabulary.

The mock's host classifier is only a review convenience. In QML, derive desktop-window-control visibility from the actual platform/host, not a width threshold. Rule 27 applies to Android phones **and tablets**. Keep the approved desktop glyphs/control recipes; BiblioBook.qml intentionally uses text glyphs —, ⛶ and ⏻ inside 30px controls around lines 685–712.

Keep safe area handling explicit at the containing page. Use the usable rectangle after top/bottom insets for overlays and scrolling. The 24px values in the evidence are illustrative, not measured Android system-bar values. Scale text and recompute implicit heights; do not shrink fonts to force the same rows into the frame.

### Source Back-control shading — B2-021

Retain ChromeScrim.qml below Back/window controls: 120px high, black alpha 0.55 at the top fading to transparent. The corrected HTML extends its containing gradient by the illustrative safe top inset, without altering the source stop colors. Comics uses that source recipe across active series, parked edition, board and index.

TheatreSeries.qml additionally gives narrow Android hosts (window width below 840) an 86px #08090d band after contentY exceeds 32, fading to full opacity across the next 40px. The pinned season chrome extends its fill upward through the Back band: #08090d on the narrow host, otherwise rgba(4,5,9,0.93). The original HTML's unrelated section offset failed this pin test. Compute the header's live position relative to the content viewport, keep shading below controls, and re-evaluate after render and scroll. The opt-in rebuilt title bar retains its own source behavior. Retested Back hit delivery, final episode actions and open season menus at the four stress sizes.

## Existing Collections picker — B2-001

Originals: collections/AddToCollectionsPopup.qml, AddToCollectionsButton.qml, CollectionMembershipRow.qml, CollectionSearchField.qml, CollectionActionButton.qml and CollectionIcon.qml. Finished HTML analogue: shared/collections-picker.js and .css.

Preserve the #171b26 surface, radius 18, Fraunces heading, source search/clear and creation controls. The membership row's source implicitHeight is 70; its cover is 48×52, text has 14px spacing from the cover, and the check is 22px. Keep cover/count/empty-cover fallback instead of replacing the row with a checkbox label. The HTML heading uses 29px, reduced to 25px in the narrow dialog; source padding is 26px and becomes 20px when space is constrained.

On open, copy committed membership IDs into a draft set. Toggle only the draft. Save commits; Cancel, close and Escape discard it. The create substate has its original title/name/validation and Create & add action. Cancel from creation returns to the list; it does not silently commit or close the whole picker. Preserve the caller's media identity: archive All releases must be DC Comics, not the Batman detail fixture.

In QML keep the existing store callbacks; do not port the HTML's local Map as application storage. Put focus into search or the creation name field, contain modal focus, and return it to the original invoker. Recalculate bounds after text wrapping. At the shortest phone landscape, the dialog may scroll internally inside the usable inset rectangle. Verify last memberships and footer actions remain reachable.

Evidence: biblio-shared-final-checks.json, comics-finished-stress.json, final-regression-archive.json and biblio-tablet-popup-retest.json.

## Biblio — B2-002 and B2-007

BiblioBook.qml is the original owner, including the bar, jacket stack, typography and source lists. Keep the full synopsis. The original source does not justify replacing it with a new More/Less interaction in this page.

The corrected HTML retains the 268×402 desktop jacket and the existing gold Read, Library and Collections controls. Portrait stacks the existing columns; landscape keeps a constrained jacket beside copy while the page scrolls. Long source names wrap in the existing row, and editions/audiobook information remains present.

The Read choice around BiblioBook.qml lines 1254–1349 needs ArrowUp/Down, Home/End, Enter and Escape with focus return. The mock initially lacked arrow navigation; that is restored. Seven torrent entries, three editions and two audiobook entries are local fixtures, not provider results. Preserve the source read/acquisition state machine when porting; the HTML only models transitions.

A final custom 1280×800 check caught Biblio's width-only desktop classification. Its finished mock now treats that reviewed size as tablet and suppresses desktop controls while 1280×700 retains them. Apply the native host condition in QML, not the mock's dimension heuristic. Both tablet orientations and the stressed landscape collection dialog were retested.

## Theatre — B2-003 through B2-006

### Synopsis and route branch

TheatreSeries.qml remains the default renderer. Do not switch the feature flag as part of fitting. In the opt-in TheatreSeriesView.qml, the source handset synopsis is complete (around lines 1189–1229) while desktop retains its existing expansion control. The original worker combined a three-line clamp with a hidden handset More button. Remove that handset clamp; allow the full text's implicitHeight. Verified on both film and series at all four 1.3× phone sizes.

Preserve existing fact order, action labels, cast, recommendations, season/absolute ordering, notifications and download intent. Cast uses local initial circles because remote portraits are not in this frozen input.

### Season popup, episode jump and sticky ledger

Keep the existing season picker and Go to episode control. Calculate a popup's available height from the safe top and safe bottom, then choose below or above its invoker and clamp inside the usable rectangle. The 23-option fixture includes Specials at the end. Use scrollable menu content, ArrowUp/Down, Home/End and Enter, Escape, and restore the triggering control's focus. Keep expanded state synchronized with actual visibility.

The original jump used a row offset from a different coordinate parent. In QML map the row into the Flickable content item (for example with mapToItem), then subtract the actual pinned header height before assigning contentY. Clamp to [0, contentHeight-height]. Do not reuse a child-local y as a page scroll coordinate. Preserve the typed jump draft while scroll-driven popup positioning updates.

Short landscape places the existing order/season controls on one row and the original heading/download/jump controls on the next; it does not invent a new compact control. Recompute header height from its content. At 792×354, scale 1.3 with 24px insets, the corrected ledger is about 122px high and episode 15 plus both 44px action targets fit below it. The visual action circle remains the original 38px inside the 44px hit target.

For keyboard row activation, handle Enter/Space only when the row itself owns focus. A focused Download or secondary action must not bubble into Play. Keep context menu dismissal and return focus. Evidence includes the initial failed jump in theatre-finished-interactions.json and the corrected pass in theatre-final-jump-retest.json; the latter is authoritative.

### Library removal

Use TheatreRemovalDialog.qml rather than silently toggling a saved title off. Preserve its 520px maximum width, parent-width minus 48px bound, #171a22 background, radius 20, 30px padding, 28px display heading and 42px action recipe. On narrow/scaled layouts allow its content height to grow, with scrolling when the usable height is shorter than its content.

Show “Remove from Colosseum & Stremio” only from the existing linkedAccount condition. No service call was exercised here. The optional linked fixture was checked at 354×792 and 792×354 with scale/insets, including Escape. Keep actual remove callbacks and app state ownership in the original source.

## Comics — B2-007 through B2-011

### Active series and parked editions

Preserve the separation between ComicSeries.qml's active release shelf and ComicSeriesPage.qml / ComicDbLedger.qml's parked edition treatment. Long release titles need width-constrained wrapping and implicit row height. Keep the original visible 36/38px utility glyph/control within a 44px touch target. When font size grows, the landscape action labels must grow too.

Whole-row keyboard activation should route Read for a ready item. For a focused alternate-source button, accept its event at the child and prevent the parent acquire/read handler from firing. This was a reproducible original worker defect. Source trailing cancel/delete affordances are intentional and remain present.

Keep source search, clear, ordering and empty result handling. The final fixture row is Batman #3. Five parked edition entries retain their bundled metadata excerpts; an ellipsis already present in catalogue data is not evidence of a CSS clamp.

### Source page and archive inspection

Keep ComicTorrentSourcesPage.qml and ComicTorrentArchivePicker.qml's original full-page source flow. Pin Back inside the safe area, then scroll the source-page body and its results as appropriate. Keep the background edition shelf inactive while the source view is open; restore the alternate-source invoker on return.

The Android fitting reserves an 80px band inside the safe rectangle for the existing Back control, then clips the source scroll viewport below it. Reduce the banner's leading padding/minimum height by that reserved space so its initial content position remains stable; let long copy and outcome panels scroll below Back. This is a fitting boundary, not an extra navigation control. On desktop retain the original 300px fixed banner, single-line elision of title/identity, 56px query slot and a table filling the remaining height. Only the result/archive list scrolls under its fixed table header. The query slot remains reserved for outcome states, matching the original anchor geometry even when the query Item is invisible. Verified 34 state/size cases plus possible/strong/weak action paths at both narrow phone orientations and desktop.

Retain provider letter badges, download arrow, FORMAT and RANGE/ISSUES metadata. Confidence uses the source gold for strong, #8ea3c0 for possible and #c98a8a for weak. Do not collapse weak/possible into one color.

Finished deterministic fixtures exercise the existing outcomes: inspecting; incomplete range with missing #5/#6 and Try another/Search manually; combined archive with Go back/Download whole anyway; ambiguous archive list with selectable 92px source rows; weak-source confirmation with Go back/Choose anyway. All are original outcome concepts, not new provider behavior. In the mock a short timer picks a declared result. The QML port must retain real inspection callbacks, cancellation and the existing request identity guard rather than using these timers or fake provider rows.

In native integration, follow the source's asynchronous lifecycle when leaving the source page: a late inspect/download result must not reopen a stale edition or overwrite another request. That backend behavior was not executable in this local HTML pass.

### Archive board/index/raw shelf

Keep ComicArchiveBoard.qml's taxonomy via GenreMosaic.qml and ComicArchiveIndex.qml's poster index. The original worker's All releases linked to comic-series.html, lost viewport/inset parameters and showed the Batman fixture. The corrected route carries archive identity into its own raw release shelf, then Back restores the prior archive index scroll. Forward navigation begins at the top.

Only the Batman series fixture is bundled. Other catalogue cards report the requested route outside the app frame; they do not open misleading Batman content. A native port must pass the actual selected archive/series ID into the original owner. Do not create seven invented detail datasets to fill those unrelated routes in this batch.

## Saga and Shelves — B2-012 through B2-018

### ContinueTile and keyboard ownership

Reuse ContinueTile.qml variant “home”: 340×148, 112px art slot, info inset 15px and radius 14. The resume control is 46px, centered at x=56 within that art slot, so its left is 33px; the original worker incorrectly put its left at 86px over the title. Retain 1.5px white border, dark circle, actual books/manga/play artwork, 21px book/manga icon and 18px video icon. Continue uses Theme.ui, the source one-line title elision with full accessible title, four-pixel progress bar and VIDEO/BOOK/MANGA badges.

The thumbnail opens Details; its center control resumes. A nested Resume keyboard action must not also fire the whole tile. Preserve the source's hover/focus 24px Remove circle and its 190×112 keyboard context menu, with Open details, Resume and Remove, 31px menu rows, arrow selection, Escape and focus restoration. The finished HTML uses a 44px outer hit region around the 24px Remove circle without changing the visual geometry.

The original Saga parent at its ContinueTile delegate (around line 519) and Shelves parent (around line 462) connect only Resume and Details. They do not connect removeRequested. The mock therefore records the Remove signal outside the app and keeps the card; it does not invent persistence. Codex should preserve that boundary unless separate app ownership authorizes wiring it. This is a source integration note, not an approved new removal behavior.

For the saga grid, branches, poster rails and magazine rows keep one Tab stop per collection, a selected child index and the original gold focus treatment. Arrow keys move within the collection; Home/End reach boundaries; Enter activates the selected work. Map book/film rows by story and retain both Deathly Hallows films. Scroll the selected child into its horizontal viewport and below pinned chrome vertically. Avoid making every poster a separate sequential Tab stop.

Bind delegated handlers once. The original HTML wrote five Back events after four rerenders; the corrected version writes one. Native QML Connections should similarly have one owner, not be accumulated by manual reconnection on every model/layout refresh.

### Saga details and unresolved items

Selected copy must derive from the actual selected item: story/part labels, Hogwarts Library versus Fantastic Beasts, progress and Read/Continue reading/Read again/Watch again. An unresolved Upcoming work retains Details pending and a disabled action. Do not navigate to a fabricated native ID. The visible 2026 text is a frozen illustrative fixture, not a verified release-date assertion.

### Shelves and magazine fitting

Keep the original editorial numbered rows and the source down-first two-column order on tablet/desktop. The mock's 40 rows are two sets of 20. At 1.3 text, phone/tablet names use actual scaled 14px text and supporting metadata scales from 12px; rows grow to their content, rather than claiming scale while leaving text at 13/11px. Keep the original medium panels, film actor runs, rail shapes and information hierarchy.

Noncompact Saga/Shelves titles are 72px in the approved source; the worker reduced tablet type to 64px. Restore 72px and let the hero grow. Actual native compact/host conditions remain source-driven. Tests verify 25 Bond films, 14 books, 40 magazine rows, both final magazine items and down-first columns at 800×1280, 1280×800 and 1280×700.

### Tab docking — Hemanth decision B2-018

The original source docks the natural-width bar beside Back at gutter+118. Reproduced at 1.3 text, Saga's x=142 and width≈485.25 yield right≈627.25, beyond both 354 and 412 portrait frames. Shelves ends at ≈448.69. Both landscape widths fit. See DECISION-B2-018.md and comparisons/decision-B2-018.html.

The runnable proposal keeps original source geometry wherever it fits. When it does not, the original 36px tab pills remain in a 46px horizontally scrollable track below Back, with a safe-aware y=74+topInset. Selection and keyboard focus reveal the active tab. Section anchors use the resulting chrome floor; they must not scroll the target beneath Back or the tab bar.

This behavior is **not approved**. Retain the original and proposed variants until Hemanth chooses. Do not globally replace universe navigation, shrink tab text, remove labels or adopt rejected-branch helper components.

## Acceptance checks after a future QML implementation

Use the page matrix in COVERAGE.csv and the linked original/finished comparisons. Recheck all four phone sizes, two tablets, desktop, scale 1.3 and actual device insets. Exercise the final row/item, source outcomes, open menus, keyboard return paths and repeated re-entry. Keep desktop typography, actions and artwork faithful to the approved source.

HTML verification establishes the fitting and interaction intent. Native text metrics, focus delivery, scenegraph clipping, source networking and device/system-bar behavior still require implementation-time QML/device verification. Those operations were outside this assignment; no app acceptance or beta release qualification is claimed.
