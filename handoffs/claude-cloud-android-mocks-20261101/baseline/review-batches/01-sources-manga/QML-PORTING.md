# Batch 1 — QML implementation handoff

## Scope and authority

The deliverable is the corrected HTML under [finished-mocks/](finished-mocks/index.html). No QML, repository, build, installation, device or service was changed. These notes identify where Codex should fit the existing components after product review. The HTML's local fixtures demonstrate layout and action state; they do not implement the backend.

The frozen approved desktop source is **a40f66be06f22cdbe75e97e7dc4f1108ab2f5d41**. Alpha **77844eef0bd3b7fee2c1f884be4a76db6d63321d** is recovery evidence. Android drift **9e896bfd5dac40ef3ceb0e26e166e85ed8beecd9** is a rejected comparison. [INPUTS.json](INPUTS.json), [input byte inventory](evidence/input-versions-start.json) and the generated [source symbol map](evidence/source-map.json) identify the exact local files. Line anchors below refer to this frozen desktop snapshot; use the component and symbol as the durable locator.

The classification brief explicitly restores **COLOSSEUM · TANKOBAN**, **ON THIS DEVICE** progress and synopsis **more/less**. These are declared restorations from [MangaReadingRoomThumbnailMock.qml](../../reference/desktop/qml/MangaReadingRoomThumbnailMock.qml), notably lines 79–234, and classification item 7. Current MangaSeriesSharedHeader.qml instead says plain “Tankoban” and uses a shortened synopsis. The corrected mocks deliberately follow the brief for these elements; they do not claim those restored elements are the current shared-header runtime.

## Common fitting and presentation contract

Keep the actual Theme, Fraunces/Segoe typography, artwork treatment, gold primary actions, glass borders, letter tiles, evidence chips and semantic confidence labels. The corrected pages load the project's existing files through relative paths. The source's round play/copy/download grammar remains visible; compact fitting must not replace it with generic row buttons, generic cards or a new phone sheet.

Use the actual logical viewport and safe-area budget. The phone rule is **min(width, height) < 600**; an 800 × 1280 tablet is not a phone merely because its width is below 840. Hide desktop window controls on Android tablets as well as phones. The HTML inset fields are illustrative, not evidence of real Android cutouts, keyboard or system-bar behavior.

Wrap existing text and controls; keep the source table/list as a scrollable region. Where short landscape plus long content leaves too little list height, let the existing hero-and-results column scroll. Do not shrink readable actions or invent another surface to absorb the content. Avoid a fixed header plus nested list combination that leaves only one clipped action visible.

Keep a stable item identifier through updates. Preserve collection focus, selected row, filter, scroll offset and popup invoker when a delegate rerenders. A transient glyph change must not move keyboard focus to the page. Clamp anchored menus to the usable viewport; close only the active menu on Escape and return focus to its invoker. Transparent outside-click catchers must consume the event so a row behind the menu is not activated.

HTML measurements and screenshots verify the chosen fitting in a browser. They are not native font-raster, accessibility, input-method, GPU, device or runtime qualification. Preserve the component's native signals and services; do not port the fixture timers that merely simulate a download.

## 01 — Theatre film and episode source pickers

**Original component:** [SourcesSheet.qml](../../reference/desktop/qml/SourcesSheet.qml), 1,119 lines. The approved source uses inline delegates. SourcePickerList.qml and SourceCard.qml named by the worker manifest are not present in the desktop/alpha snapshots; their presence in the rejected branch does not make them design authority.

| Source locus | Corrected behavior to carry forward |
|---|---|
| activateSource, copySource, downloadSource, lines 91–105; signals 125–129 | Preserve play versus download-pick semantics, whole-row activation, copy feedback and per-title queued state. Keep the actual playRequested payload, downloadRequested(row) and seasonNoPacks signal. |
| openExtMenu / closeExtMenu / activateExtOption, lines 137–151 | Keep the anchored provider chooser. Outside press dismisses only; Escape does not also close the SourcesSheet. Restore provider-trigger focus. |
| Inline source presentation, roughly 608–815 | Keep the 54 px letter tile, source/provider text, language and evidence chips, 56 px gold primary disc and 40 px secondary discs on desktop. Phone fitting may enlarge the secondary hit area without changing the control grammar. |
| Desktop action row, roughly 742–815 | When download/season pick mode omits the separate per-title Download control, remove its grid column as well. The primary action returns to the original right gutter. |
| Provider menu / context overlay, roughly 1024–1057 | Retain a transparent input-catching layer and scoped keys. Arrow keys move options; Tab remains within the open chooser; closing restores the invoker. |

The finished film and episode fixtures retain quality/provider filtering, source counts, full season packs, context choices, round actions, and copy/check feedback. Copying a row is not play activation. The original fixture's scroll already survived a copy; the fix specifically preserves the focused action as well as the scroll. Queueing a title disables its relevant download actions across the list.

**Verified evidence:** [film checks](evidence/finished-film-checks.json), [episode checks](evidence/finished-episode-checks.json), [phone comparison](contact-sheets/01-film-phone.png), and the before/after desktop-download captures. Native service calls were not performed.

## 02 — Comic sources and confirmation

**Original components:** [ComicTorrentSourcesPage.qml](../../reference/desktop/qml/ComicTorrentSourcesPage.qml), [ComicTorrentArchivePicker.qml](../../reference/desktop/qml/ComicTorrentArchivePicker.qml), and [BackAction.qml](../../reference/desktop/qml/BackAction.qml).

Carry the current selection state machine through fitting: manual query → returned sources → weak confirmation if required → inspection → an archive choice, incomplete-set recovery, or combined-archive confirmation. Keep the distinction between these states and their source wording. Do not turn semantic Strong / Possible / Weak confidence into an unlabeled decoration.

The source's cancelWeakSelection at line 126 only clears confirmingWeak and pendingRow. It must not regenerate an unfiltered default list. In the HTML, cancel now returns to the same query result and selected row, with scroll restored. The modal makes its background inert, cycles keyboard focus inside, and returns focus on cancel. Map that to the existing QML focus scope and popup modality without changing the surface.

ComicTorrentArchivePicker.nameOf/baseName, lines 25–42, supplies a basename label. The finished choice shows that basename while retaining the full file path in the accessible name. Preserve archiveChosen(fileIndex) identity; do not derive the chosen archive from its shortened label.

Restore the desktop query anchor in ComicTorrentSourcesPage.queryBar (line 308) and the following result-panel anchor at 375. The worker was 10 px high. The phone layout may wrap identity/query information, and the existing results column may scroll under long stress; the source header, rounded glass panel and row controls remain.

Use the source BackAction geometry: 24-unit chevron from (14.5, 5.5) through (8, 12) to (14.5, 18.5), with stroke 2.5 × displayed-size / 24 and rounded caps. Do not substitute a different back icon.

**Verified evidence:** [sources checks](evidence/finished-comic-checks.json), [confirmation checks](evidence/finished-comic-confirm-checks.json), [filtered-cancel symptom](evidence/comic-filter-cancel-before.json). Weak, ambiguous, incomplete, combined, inspecting, loading and empty states have local fixtures and fresh captures.

## 03 — Manga volume source selection

**Original component:** [MangaTankobanSourcesPage.qml](../../reference/desktop/qml/MangaTankobanSourcesPage.qml), especially rowsForBatch/coversBatch (121–148), _refreshLive/statusText/startLive (246–354), pickNyaa (379), confidence/evidence helpers (413–434), timers (465–487), StatusDisc (505 onward), and the source delegate around 753–896.

Retain the source's letter tile and confidence/evidence hierarchy. The selected action is the existing **StatusDisc**: normal gold download, resolving spinner, progress percentage, completed check, failed indication. Other source rows remain identifiable while one operation is live. The corrected mock chooses an eligible batch row and names the actual batch/series context in the status and toast. It no longer incorrectly reports every operation as Vol. 1.

The native presentation beats are **1,600 ms failed-state release**, **1,400 ms completed-state close**, and **3,800 ms toast lifetime**. These are source UI timers, distinct from the mock-only chapter download simulation. Preserve consumeReady(volumeId, intentGeneration), consumeAbandoned(volumeId, intentGeneration), closed() and openExtensionsRequested(), along with the source service event wiring. Repeated completion or stale service events must not resurrect a closed picker.

The narrowest long-content case uses the same hero/result column with conditional outer scrolling when the remaining result viewport would be below **180 px**. The table then retains a usable inner viewport. The toast is positioned against the usable app overlay, not clipped inside that scroller. Check both the first and last result actions after scrolling, and retain the full release title in the accessible text/title when visual text wraps or elides.

**Metadata correction required by classification item 28:** the current frozen delegate at line 848 converts a missing seeder field through Number(seed || 0). Do not copy that default into the port. Render seeder metadata only when the raw value is non-null, present, finite and nonnegative; a real reported **0** remains visible. The added fixture exposed NaN, -1 and fabricated zero in the copied renderer; the final fixture omits those unavailable values. Keep size/confidence/evidence information that is actually present.

**Verified evidence:** [main checks](evidence/finished-manga-sources-checks.json), [unknown/zero metadata checks](evidence/manga-metadata-checks.json), [long landscape final row](captures/finished-manga-sources-long-landscape-last.png), and the resolving/downloading/failed/done captures. The disabled-source control emits a local Extensions request only.

## 04 — Manga series and chapters: choose the source composition explicitly

The frozen Main.qml sets **rebuiltTitlePages: false** at line 583 and selects MangaSeries.qml versus TankobanSeriesView.qml at line 4991. The default MangaSeries.qml composes MangaReadingRoom at 1167 and MangaChapterSeriesView later in the same view. MangaReadingRoom.qml instantiates the shared header at 87 and MangaTankobanLibrary at 113.

The corrected **default** follows that composition, plus the three declared brief restorations. It removes the worker's silent mixture of the default shell, rebuilt masthead and compact rebuilt chapter controls. The optional ?route=rebuilt fixture remains a **component comparison**. It includes the rebuilt masthead/compact-list concepts but retains the reading-room chrome; it does **not** reproduce TitlePageBar, which is instantiated by TankobanSeriesView at 1624. Do not treat that comparison as a finished replacement route or approval to switch the Main.qml feature flag.

### Shared header

Use [MangaSeriesSharedHeader.qml](../../reference/desktop/qml/MangaSeriesSharedHeader.qml), not a new header. Desktop chrome is 58 px; the source hero is 205 px, title begins at the 54 px content margin, and the desktop composition does not show the phone poster. Preserve the 34 px Fraunces title, facts, collection button and source backdrop treatment.

The current mode group is **258 × 36, radius 10**, with **125 × 28, radius 7** active segments. The language group is **258 × 36**, its ComboBox **166 × 28**. Keep that desktop geometry and fit the same elements into phone rows. Tankoban mode displays disabled English; chapter mode permits selection and the existing Language settings action. Preserve tankobanRequested, chapterRequested, chapterLanguageRequested, chapterDefaultLanguageRequested and languageSettingsRequested signals.

Reuse the native [collections/CollectionIcon.qml](../../reference/desktop/qml/collections/CollectionIcon.qml) collections path, not a generic plus icon. The bookmark SVG is already white; inverting it makes it black and must be removed. Enlarging a phone hit target must not change the icon's visible drawing.

Phone header and content form the existing vertical scrolling column. Desktop keeps the header composition and independently scrollable chapter content. The HTML exposes the full local fixture; it does not open an outside reader, collection manager or language settings screen.

### Volume content on the series page

The declared 107-volume fixture now has 107 reachable items. Keep count and model cardinality consistent. Preserve the library's focused 1.10 scale and distance falloff **1.00 / .95 / .90 / .86**, centered endpoints, arrow/Home/End navigation, ten-item PageUp/PageDown steps and wheel selection. Keyboard focus remains on the stable flow container through re-rendering.

Use MangaTankobanLibrary.stateLineFor (283) for the caption's third line: acquisition state only, blank when ready. Resume belongs in the action bar/owned state. The extra worker “Continue · p. 78” caption was removed; Continue Vol. 3 in the actual action remains. Do not add invented per-cover checkbox chrome: the current library provides its selection count/bar instead.

Reserve the measured action-bar and scaled-caption space before sizing covers, and keep the source 190 px minimum. Phone content can scroll to show the full cover and then actions. The separately reviewed flow behavior decision below governs the tightest scroll allocation; a usable HTML proposal does not approve a native behavior change.

### Chapter ledger and menus

Use [MangaChapterSeriesView.qml](../../reference/desktop/qml/MangaChapterSeriesView.qml), not TankobanChapterList for the default route. Preserve its **CHAPTERS count, Continue reading, Page N selector**, rounded ledger, range header and Download page action. The default does not have the optional rebuilt prev/next/Go to chapter toolbar.

Desktop ledger uses the 54 px page margins, radius 18, 64 px header, 20 px Fraunces range, and 156 px rows with a 52 px number slot and 100 × 140 thumbnail. The phone fit wraps existing controls and uses a 32 px number slot, 52 × 76 thumbnail and 44 px action hit region; long titles wrap inside the existing row rather than behind the action.

The source Page popup at 482–544 is **150 px wide**, **min(pageCount × 36 + 12, 300) high**, with **36 px desktop rows**, radius 7 and source glassHi/glassTint option states. The final correction restores this desktop geometry. Phone rows are 44 px for touch fitting and the anchored popup is clamped above/below the trigger as space permits. The popup remains an anchored list, not a new full-screen selector. Opening focuses the current page; arrows, Home/End and scoped Tab navigate; Escape/outside dismissal restore Page-selector focus.

Keep queued, downloading, done, error, cancel and delete states attached to the chapter ID. The real component refreshes from downloader events (353–357); the HTML's **350 ms / 1,200 ms local progression is demonstration only and must not be ported**. Preserve readChapterRequested(chapterId, label), continueChapterRequested and openExtensionsRequested. A saved-but-unavailable chapter is disclosed rather than silently redirected to a different read.

**Verified evidence:** [series checks](evidence/finished-series-checks.json), [chapter checks](evidence/finished-chapters-checks.json), [desktop menu check](evidence/chapter-desktop-menu-check.json). Loading, empty, source error, disabled/Extensions, long titles, saved-unavailable, local download/cancel/delete, language/page popup and final-page reachability are represented.

## 05 — Volume flow comparison and the open decision

**Original authority:** [MangaTankobanLibrary.qml](../../reference/desktop/qml/MangaTankobanLibrary.qml), especially 540–615 (sizing, scaling and centering), 700–905 (delegates/input), and 1000–1029 (actions/selection); the two thumbnail mocks provide historical restoration context only.

The constrained original attempt and the minimal alternative are both runnable. Their shared source-facing corrections include the proper white bookmark, restored header elements, stable keyboard focus through virtual-card updates, native collection icon, acquisition-only captions, outlined pending Read action, retry, no-cover fallback, selection count/action state and accurate batch identities.

Keep the source's minimum/maximum book height **190/276**, aspect ratio **2:3**, focused scale **1.10**, caption allocation **54** and scale falloff. The entire card—including its caption—scales around the source origin. A fitting calculation must therefore budget the scaled caption as well as the cover; do not subtract an unscaled caption and then scale the whole card beyond its viewport.

The HTML proposal reserves approximately **ceil((190 + ceil(54 × textScale)) × 1.10 + 16)** for the flow, plus the measured count lane and action bar. It calculates from the available area rather than a new hardcoded phone screen. When that area is insufficient, the existing page gains vertical content. At **792 × 354, text 1.3, 24 px top/bottom insets**, even the fixed 58 px chrome leaves too little room for the full enlarged card. The proposal includes that chrome in the vertical scroller only at this tight case. Full-cover and action positions are separate, saved and reachable.

The original constrained attempt remains visibly inadequate at six cases; it is retained as evidence, not recommended for adoption. [DECISIONS.md](DECISIONS.md) contains measured values and before/proposed links. Hemanth must choose whether to adopt the scroll allocation. Do not flatten the selected cover or substitute a grid without a separate decision.

Neighbor tap should center the item; a second tap on the focused item requests its source action. Horizontal touch movement changes selection without a spurious Read; vertical movement remains page scrolling. Preserve one keyboard focus stop for the collection, arrow keys, ten-item jumps, endpoints and selection actions. The mock's touch check used browser-emulated touch, not a physical device.

**Source-code inference to check during native implementation:** pressVolume(index) distinguishes a nonfocused card, but the delegate's onPressed also assigns focus before onClicked calls pressVolume. Depending on the actual input sequence, that could cause a first neighbor tap to act rather than only center. This was not run natively. Preserve pre-press identity or separate focus-on-press from activation so the intended two-step contract is unambiguous; verify it when native runtime work is authorized.

## Artwork and future native verification

All loaded imagery/fonts/icons are supplied project files. The local path inventory found One Piece illustrations, banners and film posters, not a complete set of corresponding manga volume jackets or chapter thumbnails. These are honest artwork stand-ins; the fixtures' title/volume labels do not assert that a film poster is the real jacket. The no-cover source fallback is also demonstrated. Replacing the stand-ins requires actual matching assets from an approved project source; no network assets were acquired in this pass.

Codex can implement the chosen layouts from these components and the finished HTML without repeating source discovery. After Hemanth's flow decision and separate native-work authorization, remaining validation is the actual QML runtime at the stated viewports: font metrics, safe-area/input-method behavior, real focus traversal and accessibility, the neighbor-tap event sequence, and service-driven download/cancel/consume lifecycle. These are outside the completed HTML assignment and are not claimed as passed.
