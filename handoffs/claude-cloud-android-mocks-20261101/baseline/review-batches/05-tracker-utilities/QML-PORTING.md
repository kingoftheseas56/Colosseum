# Batch 5 — QML porting notes

TASK: Finish tracker and utility phone mocks from the approved desktop design.
STATUS: review-ready implementation guidance; no QML or application files changed.

## Source basis and how to use this handoff

Use the frozen desktop at **a40f66be06f22cdbe75e97e7dc4f1108ab2f5d41**, under `../../reference/desktop/`. It contains the reviewed Connections refinement and collection close behavior. The finished HTML demonstrates fitting and local fixture interactions; it is not a replacement state store or a QML patch. Keep the existing backend contracts, action signals, focus helpers and persistence mechanisms when implementing the chosen layouts.

The source files below are the actual components inspected. `SettingsScaffold.qml`, `CollectionGrid.qml` and `CollectionMenu.qml` named by the input manifest are absent from this approved snapshot. That is expected: use the approved parent’s inline presentation and the real `collections/` components. Do not import a replacement helper from the rejected Android branch merely to satisfy a filename.

Compare each finished page through `finished-mocks/index.html`. Its original-worker link is an unchanged HTML input, not an independently approved application screenshot. The per-page contact sheets show the original and corrected HTML at 412×900, 900×412 and 1280×700. Open-state, compact stress and keyboard captures have separate receipts.

## Shared fitting contract

| Concern | Concrete implementation direction |
| --- | --- |
| Phone reach | Android and `min(window.width, window.height) < 600`, as required by ANDROID-PORT-CLASSIFICATION.md. Do not copy the broad `width < 840` gate still present in some frozen utility code. Landscape phones retain phone controls; 800×1280 and 1280×800 tablets do not become phones. |
| Desktop chrome | Preserve desktop window controls and the existing Taskbar context. Hide these on every Android size, including tablets. Platform identity must be independent of dimensions. |
| Insets and keyboard | Derive one available rectangle from the surface, safe insets and mapped keyboard overlap. Reserve each once. The mock uses illustrative 24px top/bottom and a synthetic keyboard reservation; those numbers are not measured device constants. |
| Short height | Retain the original panel, header, copy and actions. At very short available heights, scroll the whole original panel instead of pinning a tall header and footer around a zero-height list. Do not replace it with a sheet. |
| Focus | Reuse existing QML focus and keyboard helpers. Keep focus inside the active modal, restore its opener on close, and reveal the focused input or moved item in its Flickable/ListView. Disable background interaction while a modal is open. |
| Text | Preserve Theme families and weights. Increase wrapping and implicit heights at 130% text. Avoid fixed row heights that shrink content or clip the final action. The HTML uses the supplied Segoe UI, Fraunces and Inter assets; fonts have not been replaced. |
| Touch | Preserve existing icon paths and silhouettes. The mocks use 44px phone hit regions where needed; the House toggle keeps its original 28px visible circle inside a larger transparent region. Wallpaper sort capsules similarly retain their visible shape. |
| Color notation | QML eight-digit colors are ARGB; CSS eight-digit colors are RGBA. Translate when comparing browser evidence. Do **not** change correct QML literals such as `#cc141820` merely because the original HTML misread them. |

No new phone surface or conditional behavior exception is proposed in this pass. The changes use existing elements, scrolling, wrapping, capability gates and local correctness repairs. If a future implementation needs a different surface or interaction, keep the original-attempt evidence and obtain Hemanth’s decision separately.

## 1. Connections

**Source:** `qml/TrackerSyncCenterPage.qml`, especially `trackerSyncCenterScroll`, `trackerSyncPageTitle`, `trackerPreferencesButton`, `trackerSyncAllButton`, `trackerRelayMap`, `trackerConnectedGrid`, `trackerCatalogueGrid` and `trackerStremioCard`. **HTML:** `tracker-connections.html`, `tracker.css`, `tracker.js`.

Preserve the latest Connections page hierarchy: Back; title and actions; relay/attention information; connected trackers; provider catalogue; the separate Stremio integration. Keep source icons, capability tags, status labels and the existing gold/neutral emphasis.

The finished grid uses one column when narrow, two catalogue columns from 760 logical pixels, two connected columns from 900, and three catalogue columns from 1380. The required desktop at 1280 therefore retains two-column groups. Use the available content width in QML, including page margins; do not apply browser viewport breakpoints directly to a nested item.

The original HTML had two unclosed `min()/calc()` expressions in its import/export rules. The CSS parser then consumed later responsive/capture rules and every tracker page clipped. That is an HTML defect, not evidence that the approved QML grid needs redesign. Only the finished CSS was corrected.

Preserve source read-only/deferred capabilities and distinguish the representative connected, disconnected and attention fixtures. The mock’s Connect and Sync actions update in-memory demonstration state only. Do not port that shortcut into account logic; retain the existing connector and approval lifecycle, including `trackerConnectionApprovalLayer` and `TrackerApprovalWebView.qml` when integrating the real app.

**Evidence:** `after-tracker-connections-portrait/landscape/desktop.png`; `state-tracker-connections-disconnected-*.png`; `state-tracker-connections-attention-*.png`; `scroll-tracker-connections-*.png`.

## 2. Provider dossier and review dialogs

**Source:** the same parent, `trackerProviderDossier`, `trackerDossierScroll`, `trackerImportReviewSection`, `trackerDeliveryQueueSection`, `trackerAutomaticSyncSummary`, `trackerExportReviewDialog`, `trackerImportReviewDialog`, `trackerTitleMatchDialog`, `trackerDisconnectDialog`, `trackerImportBulkReviewDialog` and `trackerRemoveImportedDialog`. **HTML:** `tracker-dossier.html`.

Keep the original right-side dossier at comfortable widths. The finished study caps it at 610px with 18px edge clearance; its header does not shrink and its body scrolls. At compact width, the same dossier fills the available width with smaller surrounding spacing, rather than becoming a new bottom sheet.

Import and export retain their own original dialogs. The finished caps are 860px wide for import, with a 760px preferred height, and 650px preferred export height. Safe and keyboard space is reserved by the outer veil; the panel then clamps to that space. In short landscape or raised-keyboard cases the entire dialog scrolls, including its original confirmation actions. This is the important fitting change to reproduce; a second subtraction of keyboard height inside the dialog made the intermediate title matcher only about 50px high.

Put outside-click handling on the veil and check the actual event target. Clicking a label or blank dossier body must not dismiss the dossier. Preserve `KeyboardSpatialNavigator` and the original accessible names. Tab/Backtab stay within the active dialog; Escape follows the existing close/cancel route.

Title matching and progress choice are separate decisions. Selecting an “Attack on Titan” candidate returns to the import review, where the user still chooses the progress decision. Confirmation requires all required decisions. Bulk selection and export selection have their own gates. After a local confirmed import, the mock disables repeated confirmation; after export, its local export review affordance clears. In QML, use the actual review/batch lifecycle rather than copied arrays.

Keep provider capability bindings on `trackerSendProgressToggle`, `trackerLivePlaybackToggle` and `trackerExportReviewButton`. The MAL fixture is read-only: send and live playback disabled, no export review or delivery queue. Provider-specific choices and switches must not leak into another provider when switching dossiers.

Disconnect Cancel leaves the connection intact; confirmation changes only the demonstrated local connection. Removing imported data leaves unrelated dossier sections intact. The application must continue to use its existing safeguards for local history, remote permissions, pending delivery and destructive operations; the mock does not validate these backend invariants.

**Evidence:** `tracker-actions.json`, `tracker-capability-gates.json`, `after-tracker-match-separate-decision.png`, `after-tracker-import-compactland-stress-bottom.png`, and both `after-tracker-keyboard-match-*.png`.

## 3. Tracker preferences

**Source:** the global-preferences branch in `trackerProviderDossier`, `trackerGlobalPreferencesSummary`, `trackerSetting_trackerSyncEnabled`, `trackerSetting_checkOnLaunch`, `trackerSetting_backgroundDelivery` and `trackerSetting_completionMessages`. **HTML:** `tracker-preferences.html`.

Keep the same dossier shell and the original switch control: 42×23px track and 17px thumb. The source positions include the track border; the HTML compensates for its CSS border box. Keep switch text wrapping, disable semantics and original dim text without dimming unrelated artwork.

There is no independent long data list on this page. Coverage includes all four preference rows with enlarged text, short landscape, scrolling as needed, Tab containment and Escape close. Store preferences through the existing application settings, not the mock’s per-page memory.

## 4. Wallpaper picker

**Source:** `qml/WallpaperSearch.qml`, `WallpaperApi.js`, `wallpapers/NoirFlow.qml`, `LowPoly.qml`, `AuroraFlow.qml`, `MeshGradient.qml`, `MeshTwilight.qml`, `MeshEmber.qml`, `MeshMint.qml`, and the two GLSL shader sources. **HTML:** `wallpaper-search.html`, `native-wallpapers.js`, shared utility files.

Preserve the original search panel, sort capsules, native shelves, KDE shelf, result tiles, full background preview, selection information and the “For All Worlds” / world Apply actions. Do not label unrelated saga pictures “Noir Flow” or “Aurora Flow.” The finished HTML adapts the original shader math and QML mesh palettes into browser rendering. QML should keep its existing native scene loaders and shaders; do not port the WebGL adapter back into the application.

All six native scenes render in the picker. Captures freeze animation time at zero for repeatable review; interactive previews animate unless reduced motion is requested. This verifies browser rendering of the source parameters, not pixel equivalence to Qt’s renderer.

The full 13-pick KDE shelf is now present. Cold Ripple uses the frozen project file. The other 12 images were copied unchanged from the exact upstream paths named by the frozen `kdePicks()`; see `evidence/kde-assets.json` for URLs, bytes, ETags and SHA-256. Their upstream branch is master, so asset hashes identify the actual retrieved bytes independently of the QML commit. Runtime mocks have no external image dependency. Credits are in `finished-mocks/assets/ATTRIBUTION.md`.

At compact portrait width the original selection panel sits within the scrollable picker flow. Landscape keeps it to the right when space permits. If remaining height is below 330px, the existing left panel becomes one vertical scroll surface; the right selection panel independently clamps and scrolls. Its actions remain reachable with the synthetic keyboard and 130% text. The horizontal native/KDE strips remain horizontal, including the last KDE tile.

The eight search fixtures are accurately titled local Colosseum saga artworks. They demonstrate search, zero results, sorting, random reorder, paging and exhausted Load more without claiming live Wallhaven results. Keep the actual `WallpaperApi.freshState/fetchPage/hasMore` model and random seed handling in QML. Applying a mock pick changes only the mock background; no application setting was written.

**Evidence:** `after-wallpaper-native-selected.png`, `after-wallpaper-kde-last-landscape.png`, `after-wallpaper-load-more-bottom.png`, both `after-wallpaper-keyboard-apply-*.png`, and `utility-actions.json`.

## 5. Extensions Store, installed management and original menus

**Source:** `ExtensionsStorePage.qml`, `ExtensionsInstalledPage.qml`, `ExtensionsStoreCard.qml`, `ExtensionsHouseHero.qml`, `ExtensionsInstalledCard.qml`, `ExtensionsActionButton.qml`, `ExtensionsTextMenu.qml`, `ExtensionsSetupSheet.qml`, `ExtensionsSetupSheetAndroid.qml` and `ExtensionsStoreApi.js`. **HTML:** `extensions-store.html`.

Keep the original store artwork, House tiles and horizontal tray, add-on logo washes, quiet installed badge, per-card action semantics, typography and installed-card layout. The House row itself must be constrained to the tray width and horizontally scrollable; a `width: max-content` row inside a clipped tray made later sources unreachable in the input.

For `ExtensionsStoreCard`, retain the source hue algorithm; 34%/16% and 32%/10% gradient stops; the square logo wash sized `art.width × 1.7 × 0.56 × 1.35`; 0.74 wash opacity; source overlay; 112px desktop / 82px phone logo stage; and the 180ms hover/focus lift of −4.5px with scale 1.045. The source phone card already has the separate Install/Set up control and expanded description. This is not a new phone control invented for the mock. The nonconfigurable installed action is disabled while keeping its original gray visible surface.

Use equivalent dark fills when reviewing CSS: source `#cc141820` means rgba(20,24,32,0.8), not the reddish CSS interpretation. Likewise preserve enabled/core status and House illustration opacity. The mock corrections are a guide to faithful reproduction, not a reason to alter correct QML colors.

For URL installation, keep `extensionsInstallUrlDialog`, `extensionsInstallUrlField`, `extensionsInstallPreviewLogo` and `extensionsInstallUrlConfirm`. Editing the address clears a prior preview/error/completion, enables Preview when text is present, validates the address and then shows the original manifest preview. Preview, Install/Set up, Already installed, error and Done are distinct states. Return/Enter in the address field previews rather than silently installing. Keep existing artwork-resolved and in-flight guards in the real app.

`ExtensionsTextMenu` remains the original 244px-wide menu with Cut, Copy, Paste and Select all, 40px rows and 14px radius. Clamp it within the safe/keyboard rectangle; scroll when that rectangle is shorter than the menu. After dismissing or using it, restore the editor’s selection/focus. The HTML has an isolated mock clipboard so it never reads the computer clipboard; QML should use its existing clipboard contract.

Use the original centered `ExtensionsSetupSheetAndroid` panel and its source browser-handoff copy and controls. It is not a bottom-sheet replacement. Desktop retains the original embedded setup panel; the mock demonstrates that component’s blocked-page fallback with Try again and the footer’s link controls. Actual provider web content, external-browser opening and installation are deliberately not performed. Keep platform-specific WebEngine loading and the existing installer/configuration safeguards when porting.

**Evidence:** URL/Setup/Your Add-ons/Text menu captures in `state-matrix.json`; `after-extensions-house-end.png`; `after-extensions-url-complete.png`; `after-extensions-desktop-hover.png`; `after-extensions-keyboard-preview-*.png`; `after-extensions-keyboard-setup-*.png`; `utility-actions.json`.

## 6. Update

**Source:** `UpdatePage.qml` and `update/UpdateLivingGallery.qml`; named objects `colosseumUpdateStatusStrip`, `colosseumUpdateStatusText`, `colosseumUpdateProgressText`, `colosseumUpdateProgressTrack`, `colosseumUpdatePrimaryAction` and the Back/window actions. **HTML:** `update.html`.

Preserve the original artwork, monochrome treatment, scrims, Inter/Fraunces type, chapter navigation and status strip. In portrait, copy occupies a scrollable region above the existing navigation and status area. In short landscape, retain the copy on the left and the status on the right; each area can scroll without covering the other’s actions. Do not replace the update page with a generic progress card.

The mock includes every `stateName/statusCopy` branch: Idle, Checking, UpToDate, Available, Downloading, Paused, Verifying, Ready, Installing, RecoverableError, VerificationFailure and ManualUpdateRequired. The Installing copy is “Colosseum is updating.” Preserve the real update controller signals and operation guards; local button transitions are demonstrations, not an installer.

Keep `formatProgress` semantics. Unknown total bytes show the downloaded amount and “size unknown,” with no invented percentage or filled determinate bar. Next wraps through gallery chapters, while Left/Right keyboard navigation clamps at the ends. Verify this separately from button navigation.

**Evidence:** all 24 update-state portrait/landscape captures in `state-matrix.json`; `after-update-unknown-size.png`; `scroll-update-portrait/landscape.png`; `utility-actions.json`.

## 7. Collections

**Source:** `collections/CollectionsPanel.qml`, `YourCollectionsPage.qml`, `CollectionPage.qml`, `CollectionShelfCard.qml`, `CollectionMediaCard.qml`, `CollectionActionButton.qml`, `CollectionMembershipRow.qml`, `CollectionIcon.qml`, `AddToCollectionsPopup.qml`, and the Collections/Taskbar integration in `Main.qml`. **HTML:** `collections.html`.

Keep the original wallpaper under the 0.66 wash, source display/UI families, overlapping cover composition, collection counters, original icon paths, note field, filtering and edit controls. The input reduced wallpaper opacity before applying the wash, making the background substantially darker than the source. The finished version removes that extra dimming.

Use the original `CollectionsPanel.bottomInset` from the parent shell. The desktop demonstration reserves 96px to clear the closed Taskbar, consistent with its 64px height plus margins. Android omits the desktop Taskbar and uses the safe/keyboard reservation. Do not let a shared desktop Taskbar cover the final row or action.

The finished overview uses one column in portrait phone, two in landscape phone, two in portrait tablet and three in landscape tablet. Media cards use two columns in portrait phone and three in landscape phone/tablet; desktop retains its denser source composition. At 130% text, landscape headings/search/actions wrap into the available width. Keep the original card and action components; change layout counts and wrapping rather than introducing generic mobile cards.

Use `CollectionPage.beginEdit/cancelEdit/saveEdit` and its actual store contract. The mock isolates a draft so Remove followed by Cancel restores the original six titles. Save commits title order, notes, name and description together; empty description is valid. Undo restores a removed item at its former position. Reordering keeps focus on the moved item; at the edge, disabled arrow focus is redirected to a valid control for that same item. Notes are limited to 500 characters, description to 300 and name to 100 as in the source. Keep backend atomicity and error handling, which HTML fixtures do not qualify.

Preserve `backToCollections`, `stayEditingButton`, `discardCollectionEdits`, `undoRemoveTitle`, the source delete confirmation and the existing close behavior. Cancel delete does not lose the current collection. Escape from an edit confirmation keeps editing; Escape while editing requests discard confirmation.

Retain `AddToCollectionsPopup` at width min(560, available−32), radius 18 and source 20/26px padding depending on panel width. The title context remains visible when creating from memberships: cover, title, creator/year and type icon. That action says “Create & add”; create-only says “Create collection.” Cancel nested creation returns to memberships and focuses New collection. Escape follows Popup.CloseOnEscape and closes the popup. Enter in the name field creates only when composition is not active.

Membership rows keep the source cover placeholder, count, check box and empty-search text. Save uses the actual current membership set, not a fixed seed. Creating from memberships preserves selected memberships and adds the new collection. The source 36-item overview pagination and 4200ms completion message are restored. The long fixture tests 44 collections (36 then 44) and a 46-title collection, plus a full 500-character note.

**Evidence:** `before-cancel-regression.json`, `after-collections-cancel-preserves-six.png`, `after-collections-saved-note-and-order.png`, `after-collections-empty-membership.png`, `after-collections-long-overview-end.png`, both keyboard creation/note captures, and `final-keyboard-and-source-actions.json`.

## Application verification boundary

Use these mocks to select and implement fitting changes in the original QML components. Then perform native checks separately for actual logical size/DPR/insets, keyboard coordinates, touch focus, Qt font metrics, shader output, real data, persistence and service workflows. No app build, repository change, installation, device operation or live provider action was performed for this batch. Desktop fidelity here is source-based inspection plus browser rendering; there was no directly applicable current approved native screenshot in the historical capture set.

The exact finished code and asset hashes, original input preservation check, screenshot receipts and remaining evidence limitations are indexed by `REPORT.json`. No mock repair should be applied to the original worker directories or frozen references.
