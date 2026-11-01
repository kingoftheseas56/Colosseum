# Fidelity ledger — 04 Manga series / chapter selectors

**Status:** standalone, local HTML/CSS/JS fitting studies. This is **not** a QML implementation, APK, device test, restored application state, or publication approval.

## Authority and source interpretation

- Frozen approved desktop: `reference/desktop` at `a40f66be06f22cdbe75e97e7dc4f1108ab2f5d41`, including reviewed Connections change `05c961914`.
- Alpha recovery: `reference/alpha/qml` at `77844eef0bd3b7fee2c1f884be4a76db6d63321d`; **not** authority over newer approved desktop. Rejected `reference/android-drift/qml` at `9e896bfd5dac40ef3ceb0e26e166e85ed8beecd9` was **not** used as a visual target.
- `reference/desktop/qml/Main.qml` selects `MangaSeries.qml` normally, with `TankobanSeriesView.qml` behind the optional `views.ini` `rebuiltTitlePages` setting. The two approved source paths have different title-bar and masthead treatments. This mock studies common component geometry, using the older/current `MangaSeries` reading-room flow plus approved elements visible in the later optional rebuild; it **does not claim** the union is an exact screenshot of either QML runtime path.
- The requested `COLOSSEUM · TANKOBAN` chrome wordmark, `ON THIS DEVICE` progress and `More` synopsis treatment are preserved explicitly. The wordmark/progress/one-line synopsis precedent is in `MangaReadingRoomThumbnailMock.qml`; the shared header in `MangaReadingRoom.qml` currently centres the word `Tankoban` instead. This intentional source distinction requires Hemanth's visual review, not a silent product decision.
- Visual tokens from `Theme.qml`: dark `#050608` background, sparing `#f0c44a` gold, ink shades, translucent glass/borders, Segoe UI and Fraunces. Fonts loaded only from the local frozen references, never bundled for distribution.

## Mapping: source to mock

| Actual source/control | HTML counterpart | Preservation and fitting |
| --- | --- | --- |
| `MangaSeries.qml` / `MangaReadingRoom.qml` / `MangaChapterSeriesView.qml` | The two complete files `manga-series.html`, `manga-chapters.html` | Two modes, one shell, masthead and content; same user-visible mode actions, with local-only navigation. |
| `MangaSeriesSharedHeader.qml` + `BackAction.qml` | `.chrome` / `.back-action`, `.chrome-brand`, desktop `.chrome-window` | Canonical stroked chevron, Back, 58px dark toolbar, edition label; desktop window icons only. At 354px + 1.3× font, the same wordmark moves to a second toolbar line rather than colliding with Back. |
| Shared header hero + `TankobanSeriesView.qml` optional masthead | `.hero`, `.poster-frame`, `.identity`, `.series-title`, `.facts`, `.genre-row` | One Piece, Eiichiro Oda, publishing status, year, score, volumes; gold publication label, editorial serif, cover with dark art wash. Columns become a responsive reflow with unchanged information. |
| `mangaSeriesLibraryButton` | `#bookmark` | Bookmark plus/check local toggle; same rounded, gold-selected design; no account mutations. |
| `collections/AddToCollectionsButton.qml` | `.collection-btn` | Original button text, icon language, glass edge and 42px geometry (fitted to narrower widths). Action remains visible; actual picker is outside the local mock. |
| `tankobanSynopsisMore` in `TankobanSeriesView.qml`; synopsis in older thumbnail | `#synopsisWrap` / `#synopsisMore` | Original `More`/`Less` text expansion. Collapsed long synopsis is readable and tappable; expanded text flows vertically without truncating other blocks. |
| `mangaSeriesModeSwitch`, `mangaModeTankoban`, `mangaModeChapter` | `.mode-switch a` | Two original choices with single gold active pill; reflowed, not replaced with cards. No new mode/action invented. |
| `mangaLanguageSelector` + `mangaLanguageCombo` | `.language-box`, `#languageSelector`, `#menuPopup` | Original compact ComboBox appearance, disabled in Tankoban Mode, with `Use preferred language`, English, Portuguese (Brazil) and Spanish representative choices in Chapter Mode. One bounded anchored popup; **no sheet**. |
| `mangaLanguageSettingsShortcut` | `.settings-btn` | Same text/setting icon; local action only, no account/provider call. |
| `MangaReadingRoomThumbnailMock.qml` device progress + requested restoration | `.device-progress`, `.progress-track` | `ON THIS DEVICE` label, serif count and 2px gold fractional line. The 3/107 sample is illustrative, not app progress. |
| `MangaTankobanLibrary.qml` v2.3 | `.volume-head`, `.volume-flow`, `.volume-card`, `.volume-caption` | `VOLUMES` count, horizontal centred covers, enlarged gold-focused book, near-book falloff, gold owned edge, real title captions and single flow (not generic flat tiles). Original zoom/centring was tried and retained for both phone orientations; **no behavioural flattening proposal**. |
| `volumeSelectToggle`, batch controls | `#selectToggle`, `#next10`, `#selectionTray` | Select/Done, selected count, Download selected, Download next 10. Toggle/check interactions are mock-only. |
| `MangaTankobanLibrary.qml` current action bar | `.volume-action`, `#focusVolumeLabel`, `.btn-outline`, `#readVolume` | Current Vol. label, Download, Read/Continue on one action row; reflows vertically on portrait phone and remains reachable by scrolling in short landscape. |
| `MangaContinueButton.qml` | `.chapter-continuation` | Existing gold Continue reading with original dark 36px square icon and metadata chip. No reader implementation. |
| `TankobanChapterList.qml` / `MangaChapterSeriesView.qml` | `.chapters-section`, `.chapter-toolbar` | Chapter count/range, ten representative rows per page, dedicated scrollable body, no generic card substitutions. |
| `mangaChapterPageSelector`, `mangaChapterPagePopup` | `#pager`, `#pageSelector`, `#menuPopup` | Original `‹ Page N of M ›` with both arrows and a bounded floating list; 44px popup rows and contextual `Page N · range`; anchored above if opening below would clip. No replacement sheet. |
| `mangaChapterGoTo`, `mangaChapterDownloadPage` | `#goTo`, `.download-page` | Original Go to chapter and Download page affordances. Enter navigates to the correct ten-chapter window locally; download has no provider effect. |
| `TankobanChapterList.qml` delegate | `.chapter-row`, `.chapter-num`, `.chapter-thumb`, `.chapter-open`, `.chapter-download` | Number in Fraunces, 52×74 thumbnail position, chapter name/status, round trailing download/check/cancel. Compact phone rows wrap labels and keep at least 44px download targets. |

## Fitting-only changes

1. The approved title, cover, facts, genres, synopsis, mode and language controls are rearranged into a portrait grid; landscape phones use cover/title, controls on the right, facts/synopsis underneath. No information is removed. Desktop keeps full-margin columns. Tablet presets stay distinct from phone and desktop.
2. Tall single-axis scrolling keeps header, flow, chapter list and bottom action reachable without cropping controls. Cover-flow scroll remains horizontal, with the selected cover enlarged and centred; it is **not** a stack of flat mobile cards.
3. Floating language/page popups are clamped to the usable app frame and may open upward in a short viewport; they remain the original dropdown class, not full-screen sheets. Popup rows use 44px invisible/visible hit geometry. A delayed scroll-to-anchor event no longer closes an open popup: its anchored position is recomputed on scroll.
4. Page/chapter controls wrap on narrow portrait; chapter title text wraps without shifting or hiding the numeric index or round action. Original page navigation and Go to remain separate.
5. All viewport/inset/font-scale inputs are placed **outside** the app. The illustrative 24px top/bottom case adds blank safe area, never simulated OS chrome. At narrow 354px and 1.3× type, the wordmark reflows beneath Back within the same toolbar; that fixes an observed overlap.
6. At 1280×700, desktop is correctly classified as a desktop rather than as a tablet. The tablet 800×1280 and 1280×800 layouts have their own fitting rules.
7. The visual language stays dark/transparent glass, gold focus/progress, serif title and numerals, rounded action buttons, actual project icons and unchanged visible action labels.

## Illustrative data and asset substitutions (outside app UI)

- Only local frozen project assets are used: primarily `assets/universes/one-piece/east-blue/canon/romance-dawn.png`, `orange-town.png`, `syrup-village.png`, `baratie.png`, `arlong-park.png`, `loguetown.png`, and `assets/universes/one-piece/paradise/badges/{arabasta,skypiea,water-7,enies-lobby}.png`. Hero art wash uses `east-blue/banners/romance.png`.
- **No exact manga-volume cover files or chapter thumbnail files were found in the frozen product assets.** These named authentic One Piece project illustrations are reused as a clearly declared stand-in. They must **not** be represented as specific volume covers or exact manga page imagery.
- All download, progress, chapters, count and language availability are populated **representative data only**. `One Piece`, `Eiichiro Oda`, named arcs and known early chapter/volume names are used instead of blank posters or synthetic product labels. For chapters without a verified titled name, only `Chapter N` is shown.
- The mock synopsis is illustrative representative text for exercising truncation/expansion, not a new app copy recommendation. No external requests.

## Interaction states, proof and boundary

- Both pages: editable 412×900 portrait, 900×412 landscape, 354×792 narrow, 792×354 short landscape, 1280×700 desktop, 800×1280 / 1280×800 tablets; safe-area inputs and font scale 1.0/1.3. All accessible from the index.
- Cover-flow: selected, Select mode, no-selection, local bookmark, expanded/collapsed synopsis, scrolled action bar. Chapter list: language dropdown open/closed/selected, page dropdown open/closed/selected, Go to chapter, 44px download, scroll to last row. Buttons referencing app/provider features only show a status **outside the app**. Mode switching uses local HTML navigation, so temporary local interaction state resets across files; this is a mock limitation, not a proposed product behaviour change.
- Rendering: `node 'D:\Colosseum-Android-Phone-Mocks-20261101\outputs\04-manga-series\capture.cjs'` (isolated headless Microsoft Edge via existing Puppeteer; no global installs, browser tab interaction or shared server), source `file:///D:/Colosseum-Android-Phone-Mocks-20261101/outputs/04-manga-series/manga-series.html?capture=1` or chapter analogue. Browser viewport and DPR 1 are recorded in `captures/checks.json`. Initial BDC `render_html` returned `HTML_RENDER_OUTPUT_MISSING`; the direct isolated headless fallback succeeded.
- Interaction checks: `node 'D:\Colosseum-Android-Phone-Mocks-20261101\outputs\04-manga-series\verify.cjs'` covering popup bounds, select/batch, synopsis, bookmark, chapter range/page, Go to, scroll-reachability, and large-text header collision. Results and any errors in `captures/interaction-checks.json`.
- No automated DOM or headless result is evidence of compiled, tested or runtime-verified QML. The provisional OnePlus sizes, DPR and safe-area insets are **not physically measured**. No build, device interaction, product edit, provider/account operation, upload, publication, or source mutation took place.

## Behaviour alternatives and unresolved review items

**Behaviour alternatives:** none. Original centred cover-flow and original anchored dropdowns were retained. No do-or-die new UI exception is proposed.

**Unresolved:** Confirm exact OnePlus logical viewport, DPR, insets and font scale when the device is available; replace illustrative universe artwork with genuine per-volume covers and chapter thumbnails only when suitable originals exist; visually compare the chrome/masthead union against the user-approved runtime route (the frozen `Main.qml` default and optional rebuilt route differ). The user still needs to approve any future product implementation. Publication remains **HELD**.

