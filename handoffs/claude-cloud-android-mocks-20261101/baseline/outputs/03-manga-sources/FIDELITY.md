# 03 — Manga volume torrent sources: fidelity and fitting

**Scope:** Local HTML/CSS/JS fitting study only. No Colosseum QML, native service, project checkout, APK, beta or provider was changed. Publication remains **HELD**.

## Authority and evidence

- Approved design: `reference/desktop/qml/MangaTankobanSourcesPage.qml` at frozen desktop revision `a40f66be06f22cdbe75e97e7dc4f1108ab2f5d41`.
- Transitive visual/control source: `reference/desktop/qml/Theme.qml`, `BackAction.qml`, `Glass.qml`, and the `MangaSeries.qml` overlay mounting point. `Main.qml` was consulted for the surrounding full-screen shell. The original picker is an overlaid **full-screen page**: main-world navigation is obscured, not replaced with a new mobile nav bar.
- Recovery comparison: `reference/alpha/qml/MangaTankobanSourcesPage.qml` at `77844eef0bd3b7fee2c1f884be4a76db6d63321d`. Approved desktop stays authoritative; rejected flat-card Android treatment was **not** used.
- Task-specific guidance: `context/ANDROID-PORT-CLASSIFICATION.md` (item 3; restore this row), `context/VIEWPORTS.json` (provisional sizes).
- Historical style comparison: `reference/historical-captures/T5-sources-before-412x900.png` and `T5-sources-before-1280x700.png`. Their small-viewport overflow is not accepted as a good final fit.

## Original QML → HTML counterpart

| Approved source element / behaviour | Local HTML/CSS/JS mapping | Fidelity |
| --- | --- | --- |
| Full-screen opaque black + half-opacity blurred wallpaper context | `#app > .background-art` and `.picker` | Complete dark stage; uses the local `assets/wallpaper/cold-ripple.jpg` fixture. Dynamic Qt backdrop capture is not simulated. |
| 300px cover/key-art banner and black downward wash | `.hero-art` + `.hero-gradient` + `.lower-veil` | Same image-wash hierarchy; responsive banner height only for phone fitting. |
| `BackAction` variant `plain` with vector round-cap chevron, “Back”, enlarged invisible hit | `#back` with the equivalent SVG path and a 44px control | Same icon grammar and action. Clicking locally closes the picker; no invented underlying MangaSeries is rendered. |
| Gold eyebrow `SOURCES · TANKOBAN VOLUME` | `.eyebrow` | Exact wording, colour, tracked lettering, Segoe UI family. |
| `titleText()` and `identityLine` | `#heroTitle`, `#heroIdentity` | Exact `Vol. 1`, `3 volumes` batch, or `Sources` series-mode title logic. Identity is formed from supplied context, not invented UI. |
| Source `heroSynopsis` including italic empty fallback and maximum four lines | `#heroSynopsis` | Exact empty-state wording; long fixture copy demonstrates original max-four-line elision. |
| `Glass` with 18px radius / 0.18 edge / restrained white tint/scrim | `.glass` | Original single glass table, not per-release cards. CSS `backdrop-filter` approximates Qt `MultiEffect` rather than claiming identical shader output. |
| `tableHead`: count, Nyaa, separator | `#tableHead`, `#sourceCount` | Same display font, typography hierarchy, count grammar and “Nyaa” label. |
| `ListView` / `rowEnabled` / full-row click and keyboard activation | `#resultViewport`, `.source-row`, Enter/Space handlers | Same one-row pick semantics, vertical scrolling, pointer and keyboard reach. Gold circle is an action drawing **inside** the whole-row hit target, not a new standalone control. |
| Uploader initial 54×54 letter tile (radius 12), uploader label | `.source-tile`, `.uploader` | Same letter tile, border, display font and source identity; tile becomes 46–48px only on phone. |
| Confidence `STRONG` gold / `POSSIBLE` blue-grey (tight match, not publisher trust) | `.confidence.strong` / `.possible` | Same labels, semantics and colours, including batch coverage treatment. |
| Release title, `DIGITAL`, `SINGLE VOLUME` or `VOLS N–M`, `TRUSTED` | `.release-title`, `.evidence-chip` | Same content grammar, light glass chips, evidence ordering, 20px chip height/radius 6. |
| `fmtSize` and purple person/seeder glyph | `.source-meta`, `fmtSize()` | Same MB/GB rounding and visible glyph; all fixture seeder counts are known positive values. |
| Gold 56px circular `↓` action | `.source-action.down` | Same colour/glyph and circular form; phone action remains at least 50px. |
| `StatusDisc` resolving arc, live %, check and failure mark | `.source-action.disc` variants | Original **disc**, never a progress bar. Review states simulate resolving, 47%, complete, failed. |
| Picking dims other rows, shows toast, keeps picker open | `.source-row.dimmed`, `#toast`, local `choose()` | Visual and interaction demo only; no torrent or download is started. Back remains available during a simulated pick. |
| Nyaa disabled: empty copy + gold outlined `Open Extensions` | `#emptyState`, `#extensionsRoute` | Exact disabled copy, one route, no new sheet; emits only a mock `openExtensionsRequested()` note outside the app frame. |
| Loading / no-match empty copy | `#emptyText` state changes | Exact “Searching Nyaa releases…” / “No releases matched this volume yet.” texts. |
| Contextual native query (not a text input) | Reviewer-only `Context` selector, `availableRows()` fixture logic | QML calls `searchSources(volumeId)`; series mode calls `searchSeriesSources(volumeId, seriesTitle)`. Batch uses the first-volume query then filters/sorts by coverage. The QML **has no visible query input**, so none was invented. |
| Nyaa-only contract; no WeebCentral build fallback | `fixtures` filtered by `kind === "nyaa"` | No “Build from chapters” option or alternative acquisition UI. |

## Fitting changes, not redesign

1. Desktop reference keeps the 300px hero, 54px page margins, 56px Fraunces title and 150px rows. The 1280×700 capture retains the original vertical composition and one full-width glass result table.
2. Phone portrait reduces page gutters to 16–20px, hero to 258px at standard scale, title to 46px, row tile to 46px, and gold disc to 50px. Controls remain in the **tile → source information → circular action** order. Uploader/confidence and chips may wrap; long release text may occupy two lines instead of overflowing.
3. Phone landscape keeps the same full-screen hero/table arrangement, with a shorter 151px hero, title at 36px, hero text shifted right of the existing Back action, 44px table header and a scrollable list. Nothing is reimplemented as a card or bottom sheet.
4. Tablet and desktop keep the desktop layout because the task's phone-reach classification is `min(width,height) < 600`, not “any touch device”. Rows grow as text demands at 130% font scale.
5. Insets are *illustrative*, editable outside the frame, and create no fake system status bar or navigation chrome. A 354×792 phone and a 792×354 phone with 24px top/bottom insets and 130% text were checked. In the very short landscape stress case, the glass list must be scrolled to expose a whole row and its 50px action.

## Interaction and mock-data disclosure

The user chooses a source by clicking **anywhere on its original row** or pressing Enter/Space while focused. The selected source displays the resolving spinner and original toast; reviewer-only state controls let the viewer inspect a sample 47% download disc, done state and failed state. All state changes are in-memory; there is no real provider, search, download, account operation or Extensions navigation. Terminal done/failed reviewer states are held as inspection fixtures instead of running the original QML's 1400/1600ms automatic transitions; this is **not** a proposal to alter product behaviour. Back hides this picker locally. The next page is deliberately not fabricated.

The reviewer panel **outside** the viewport labels fixture data explicitly. Release names and metrics are illustrative, not verified Nyaa listings. The source context uses “One Piece · Vol. 1”. Because an exact local Volume 1 cover is unavailable in the named assets, the hero uses the real project file `reference/desktop/assets/universes/one-piece/east-blue/banners/romance.png`, which is **One Piece artwork, not a volume cover**. Local Segoe UI family files and bundled Fraunces are referenced at their original paths, not copied into the output folder.

## Verified render procedure and observations

Run from Laptop 1 via Brotherhood Desktop Commander (already executed for this report):

```powershell
node "D:\Colosseum-Android-Phone-Mocks-20261101\outputs\03-manga-sources\verify-layout.js"
```

The script launches a separate headless `C:\Program Files\Google\Chrome\Application\chrome.exe` via DevTools Protocol with an isolated temporary profile, a DPR of **1**, local `file://` assets, and `Page.captureScreenshot`. It does not touch a shared window/tab or use global mouse/keyboard. Exact scenarios and element bounds are in `captures/geometry-audit.json`; PNGs are in `captures/`. Required captures are `portrait.png` (**412×900**), `landscape.png` (**900×412**) and `desktop.png` (**1280×700**).

Additional captures: `small-portrait.png` 354×792, `small-landscape.png` 792×354, `disabled.png` 412×900, `batch.png` 412×900, `series.png` 412×900, `live.png` 412×900, `long-130.png` 354×792 at 130% and 24px vertical insets, `landscape-long-130.png` and `landscape-long-130-scrolled.png` 792×354 with the same stress settings, `tablet.png` 800×1280 and `tablet-landscape.png` 1280×800.

**Checked:** inside-panel action bounds; nonoverlapping tile/text/action columns; no horizontal row overflow; minimum Back hit height; hero text inside banner; table-header fit; no unwanted scrollbar when sources are disabled; selection → resolving disc + dimming + toast; Back closing; keyboard Enter activation; Extensions route signal; extreme-landscape action reach by list scrolling; and external reviewer controls changing viewport, insets, font scale and state. All 14 recorded audit scenarios passed the implemented checks. The screenshots were visually inspected against the historical original treatment. This verifies the **HTML mock only**.

## Alternatives and unresolved boundaries

- **Behaviour proposals: none.** Existing row/Back/Extensions behaviour could be shown without substituting a new interaction. No exception to approved design is proposed.
- OnePlus viewport, DPR, safe-area and keyboard offsets have **not** been measured; presets are provisional from `context/VIEWPORTS.json`.
- Browser CSS blur, font rasterisation, emoji glyph and clipping are not evidence of pixel parity with Qt Quick/Android. Real native rows, native progress, torrent resolution, and navigation were **not** executed or verified.
- The fixture hero art is not a source volume cover; exact artwork and a real backend result list require separate authorised integration verification.
- The 130%-font + 24px inset, 354px-high landscape variant permits source selection by scrolling within a thin result viewport. This is a fit tradeoff to inspect on the actual device, not an approved new interaction.

Open the main page through a local HTTP server rooted at the **batch root**, e.g. `http://localhost:8023/outputs/03-manga-sources/manga-volume-sources.html`. The `../../reference/...` asset paths remain local and require no external network.
