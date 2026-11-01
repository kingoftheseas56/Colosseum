# QML porting notes for the combined candidate

Line numbers refer to the fixed source pin `a40f66be06f22cdbe75e97e7dc4f1108ab2f5d41` (identical in this branch's `qml/`). These notes describe what native work would make the app match the candidate. No QML was changed here. HTML timers, fixture stores and the mock's viewport classifier are not native state owners.

Each batch's existing porting notes stay valid for pages carried unchanged:
`baseline/review-batches/01-sources-manga/QML-PORTING.md`, `02-detail-pages/QML-PORTING-NOTES.md`, `03-browsing-libraries/QML-PORTING.md`, `04-accounts-configuration/QML-PORTING-NOTES.md` and `05-tracker-utilities/QML-PORTING.md`. The sections below cover only behaviour this candidate added or changed.

## Batch 1 · rebuilt manga title page (Hemanth-selected)

**N1. Route selection.** `Main.qml:583` defaults `viewSettings.rebuiltTitlePages` to `false`. `Main.qml:4991` uses it to pick `TankobanSeriesView.qml`. The same flag also picks `TheatreSeriesView.qml` at `Main.qml:5234`. Hemanth chose the rebuilt route for manga only; Theatre is still undecided. Before turning on the manga route by default, split the flag (for example `rebuiltMangaTitlePages`) or Theatre will switch too. This is an open product/implementation choice (C2).

**N2. TitlePageBar on Android: no system menu.** Owner: `qml/TitlePageBar.qml`.
- Add `readonly property bool androidHost: !!(bar.Window.window && bar.Window.window.androidHost)`. This follows the existing `TheatreSeriesView.qml:25` pattern; `Main.qml:229` aliases `androidHost` from `PhoneShellHost.qml:50`.
- Set `visible: !bar.androidHost` on the second `Item` in `rightCluster` (the 40×40 item whose `KeyboardAction` has `objectName: "titleBarSystemMenu"`, line 158).
- Guard `openMenu()` (line 35) with `if (bar.androidHost) return`, and close the menu when `androidHost` becomes true.
- An invisible Qt Quick item drops out of the focus chain and the accessibility tree, so there is no keyboard or accessibility path. Back (`BackAction`) and Search (`titleBarSearch`) are unchanged. No replacement mobile menu.
- The `minimizeRequested` / `fullscreenRequested` / `quitRequested` signals and the host wiring (`TankobanSeriesView.qml:1637–1639`, `TheatreSeriesView.qml:2176–2178`) stay as they are for desktop.
- Precedent: `qml/+android/TopBar.qml:38` (`desktopWindowControlsVisible: !bar.androidHost`) and `HouseWindowControls.qml:11`.
- This applies to phones and tablets, because `androidHost` is platform-based, not width-based.

**N3. Back label.** `TankobanSeriesView.qml:45` defaults `backLabel` to "Tankoban". `Main.qml` binds it to `worldStack.current` (or "Home"). The candidate shows "Tankoban". No change is needed.

**N4. Declared COLOSSEUM · TANKOBAN wordmark (conflict C1).** `TitlePageBar.qml` has no wordmark, and neither does the rebuilt page. The declared wordmark comes from `MangaReadingRoomThumbnailMock.qml:95`: centred, `theme.ui` 11 px, letter-spacing 3, `inkDimmer`. Classification item 7 requires keeping it.
- The candidate keeps it as non-interactive centred text in the bar. When it would collide with Back or the right cluster, it moves to a second row and the bar grows from 76 to 98 px. The masthead offsets already follow `titleBar.height` (`TankobanSeriesView.qml:1183, 1217, 1252, 1446`).
- Port option: add `property string wordmark` to `TitlePageBar` with a centred `Text` that has no input handling, plus the collision rule. Alternatively, put the `Text` in `TankobanSeriesView` at `z: 51`.
- Hemanth may instead drop it. That is recorded as an open choice.

**N5. Device progress (“ON THIS DEVICE 3 OF 107”).** Neither `TankobanSeriesView.qml` nor `MangaTankobanLibrary.qml` renders it. Precedent: `MangaReadingRoomThumbnailMock.qml:218`. Add it to the body above the library lane header, fed by `tankLib.ownedCount` and `tankLib.volumeRows.length`. Show it in Tankoban Mode only.

**N6. Synopsis More/Less.** Already in the source (`tankobanSynopsisMore`, `TankobanSeriesView.qml:1362–1386`). No change.

**N7. Language selector in Tankoban Mode (open choice).** The rebuilt source hides it there (`languageSelector visible: page.chapterMode`, line 1499). The preview Hemanth chose shows a disabled “Language · English” box, as the older header did. The candidate keeps the preview. Native should follow whichever Hemanth confirms.

**N8. Phone fitting of the masthead.** These are reflow changes only:
- Poster 105 px wide in portrait.
- Facts, genres and synopsis span the full width under the poster/identity row.
- The mode switch and language box stack full-width in portrait and form a right column in landscape.
- Chapter Mode keeps its continue button, pager, go-to field and download page in the body.
- Make the masthead plus body one vertical `Flickable` on Android phones, with the bar pinned above it.
- Bind `solid` to `flickable.contentY > 4` on that path. On desktop it stays `page.loading`.

**N9. Cover flow.** `MangaTankobanLibrary.qml:548–600` is unchanged in both allocations. Allocation B gives the flow a fixed viewport on phones (the mock uses 294 px, so `bookHeight` is 203). Allocation A needs no change but does not fit phones. See `evidence/batch1/FLOW-REEVALUATION.md`. This waits on Hemanth's decision.

**N10. Older reading-room route on Android.** `MangaSeriesSharedHeader.qml` window icons should get the same platform gate as N2 if that route is kept for Android at all.

## Batch 2 · Theatre detail

**N11.** Sol's correction (SOL-B2-001) is covered by N2 for the rebuilt `TheatreSeriesView` (TitlePageBar at line 2164). The legacy `TheatreSeries.qml:979` already gates its window row with `visible: Qt.platform.os !== "android" && !page.phoneNarrow`. The Theatre legacy-versus-rebuilt choice is not settled by the manga choice. B2-018 (Universe tabs below Back) also stays a visual decision. Both are listed in the report.

## Batch 3 · browsing and libraries

**N12. Genre and see-all window rows (native gap).** These pages render Minimize / Toggle fullscreen / Close with no platform gate:
- `GenreIndex.qml:247–261`
- `TheatreGenreIndex.qml:254–262`
- `BiblioGenreIndex.qml:246–254`
- `GenrePage.qml:393–401`
- `TheatreGenrePage.qml:412–420`
- `BiblioGenrePage.qml:388–396`
- `ContinueSeeAllPage.qml:285–330`

The candidate's HTML now omits them on Android phones and tablets. Port: add `visible: Qt.platform.os !== "android"` to the three window `Image`s (not the Search icon) on each page.

**N13. Search corrections (Sol, integrated unchanged).** Follow `SearchSurface.qml:218,280` and `BiblioSearch.qml:190,202`:
- Enter commits the query and opens the top match.
- Query and caret survive the debounce.
- A deliberate focus move is not stolen back.
- After navigation, native must not force focus back into a hidden Search field. Keeping focus in the field is fixture-only behaviour.

## Batch 4 · accounts and configuration

**N14. No native change.** The tablet window-control leak was a mock fidelity bug:
- Account Centre uses `HouseWindowControls` (`account/AccountCenter.qml:265`), which already has `visible: Qt.platform.os !== "android"` (`HouseWindowControls.qml:11`).
- The flyout's desktop underlay corresponds to `+android/TopBar.qml:561–572`, which already hides them.

Pro's B4-001…015 notes remain the porting reference for the rest of Batch 4.

**N15. Keyboard fitting decision (B4-009).** Unchanged and still open. The question is whether the original account navigation may scroll with content while the IME is visible at 792×354.

## Batch 5 · tracker and utilities

**N16. Tracker focus (Sol, integrated unchanged).** Follow `TrackerSyncCenterPage.qml`:
- `closeDossier` (1381–1401) returns focus to the exact connected-card or catalogue-card opener, or to Preferences when that opener is gone.
- `takeKeyboardFocus` (1439–1441) is the focus helper.
- `closeImportReview` (433–451) and `closeTitleMatchFinder` (518–546) return to their own invokers.
- The Escape ordering is at 1410–1435.
- Connections refinement `05c961914` is already in the pin.
