# Tankoban cover-flow — fidelity and review ledger

**Task:** 05-volume-flow. **Design authority:** frozen approved desktop source at `a40f66be06f22cdbe75e97e7dc4f1108ab2f5d41`. **Implementation status:** HTML/CSS/JS only; no app implementation or beta change.

## Comparison decision

The original centred, enlarged-cover experience is the baseline. Its focus remains visually central and enlarges to **1.10×**; adjacent covers are **1.00×**, distance two **0.95×**, distance three **0.90×**, farther **0.86×**. A neighbour tap centres it; a second tap at focus requests the existing Read action. The model contains 48 illustrative volumes to test a long continuum; keyboard arrows/10-step Page Up/Down, Home/End and wheel movements model the source control contract.

**Measured obstacle:** the approved QML's `bookHeight` formula clamps to a **190px minimum**, even when a 792×354 viewport has only a short library surface after the full series masthead, 44px lane head, 52px action bar and 54px captions. In the HTML baseline, retaining the original centre/focus hierarchy but allowing the book height below the hard minimum yields tiny covers in short landscape. The headless render `captures/original/landscape-small.png` makes the disproportion obvious. At 900×412 the original fitted treatment remains viable without another behaviour.

**Conditional proposal:** `volume-flow-minimal-alternative.html` changes **only** short-landscape space allocation (height ≤400 CSS logical px): a minimum-height `volume-library` retains the focused book, neighbours, captions, header and actions, and the already-present series page scroll supplies access to it. The initial screen and the after-scroll gallery are shown in `captures/alternative/landscape-small.png` and `captures/alternative/landscape-small-gallery.png`. No flat strip, new sheet, extra action, chevron or navigation is proposed. This is **not approved for implementation**; the visible extra scroll is the explicit cost. In all other studied sizes both variants use the same fitted flow.

## Latest approved source → mock fidelity map

| Approved source / control | HTML counterpart | Fidelity / fitting treatment |
| --- | --- | --- |
| `MangaReadingRoom.qml`: `MangaSeriesSharedHeader` followed by `MangaTankobanLibrary` | `.hero` followed by `.volume-library` in `.series-scroll` | Full series context retained around the flow; no isolated control demo |
| `MangaSeriesSharedHeader.qml`: 58px `chrome`, `BackAction`, centered “Tankoban” | `.chrome`, `#backAction`, `.chrome-title` | Canonical drawn chevron proportions, title, no invented OS toolbar |
| Desktop chrome min / fullscreen / power | `.window-actions` with actual local SVGs | Shown in desktop preview, hidden on phone/tablet per approved source |
| Hero banner crop + wash, poster on phone | `.hero-background`, `.hero-wash`, `.phone-cover` | Actual local One Piece art, not a blank poster; poster sizes fitted in phone orientations |
| `publicationLabel` + Fraunces series title | `.publication`, `.title-line h2` | “MANGA”, sparse gold, local Fraunces; wrapping on portrait |
| Bookmark add/remove and named collections | `#bookmark`, `#collections` | Original icons/paths, shape and copy, local toggling/log only; no real collection operations |
| Author/status/year/rating and synopsis | `.desktop-facts`, `.phone-facts`, `.synopsis` | Reflowed by orientation; **no** obsolete in-frame “more” control from historical thumbnail source |
| Tankoban / Chapter Mode segmented control | `.mode-switch` | Original gold active segment and inactive treatment; Chapter logs original route intent |
| Tankoban locked English language selector | `.language-selector` | Original disabled selector, not a new language sheet or menu |
| `MangaTankobanLibrary.qml`: whisper VOLUMES, dynamic count | `.lane-head`, `.lane-label` | No border or redundant section card |
| Select / Done, Download next 10 | `#selectToggle`, `#downloadNext` | Same header actions, no new dropdown; selection toggles and logs local batch intent |
| Horizontal ListView, current focus, neighbour scales, centring | `#flowViewport`, `#flowTrack`, `.volume-card` | Focused 1.10×, 1/.95/.90/.86 neighbours, centred by index and keyboard/touch |
| 2px focused gold outline; owned gold bottom line; state progress | `.cover-frame`, `.owned`, `.loading` | Gold only for state/focus; 73% and failed examples are illustrative state fixtures |
| Cover from volume row / local pages / “NO COVER” fallback | `.cover-frame img` | Local titled project artwork used **as substitute** because no actual manga-volume cover files were present; fallback is source-accurate glass only in QML, not simulated here |
| `VOL N`, optional catalogue title and state line | `.volume-caption`, `.volume-num`, `.volume-name`, `.volume-state` | No invented chapter ranges; long optional names ellipsize |
| Action row `Vol. N` / Download / Read, Retry, Continue | `.action-bar`, `#currentVolume`, `#downloadAction`, `#readAction` | Portrait stacks the same identity above the unchanged two actions; no second acquisition path |
| Floating selected bar `N selected` / Download selected | `#selectionFloat`, `#selectedCount`, `#downloadSelected` | Same selection bar, only local click log |
| `Theme.qml` tokens | `style.css` local font faces, `--gold`, `--ink`, `--dim`, `--edge` | Segoe UI and Fraunces, `#f0c44a` gold, dark backdrop and glass edges |
| `MangaTankobanLibraryThumbnailMock.qml`, `MangaReadingRoomThumbnailMock.qml` | Comparison only | Historical grid/older masthead are not used as current active UI; newer `MangaReadingRoom.qml` owns composition |

## Allowed fitting changes

The viewport determines book size from measured library height, caption budget and focused scale instead of keeping QML's large-screen 190px floor. Portrait reflows hero elements vertically and the existing action-bar identity above its buttons; landscape places art, identity and mode/language controls in parallel. Section headers retain the original vocabulary. Existing page scrolling is enabled for long content and the conditional alternative. Phone chrome avoids OS chrome and hides desktop window actions. The editable viewport, top/bottom insets, 1.0/1.3 font-scale and stress-state controls are **outside** the Colosseum frame.

Font-scale 1.30 and 24px sample insets are illustrative stress tests, not measured OnePlus physical sizes. The true OnePlus logical size, DPR, system bar/IME insets and Qt target performance remain unknown.

## Behaviours and states

Open/closed and stress cases: select/done with selected float, Download next 10, currently focused Read/Retry/Continue or in-progress read intent, failed and downloading captions, 48-volume End jump, short/long synopsis stress, 24px insets, and 1.30 font. The source's current shared header provides plain synopsis text, **not** the “more” action in the superseded thumbnail; the long-copy stress state is chosen by the reviewer panel only.

Mock interactions send no backend calls. `sourcesRequested`, `readVolumeRequested`, `batchRequested`, add/remove library, named collections picker, mode switch and back/minimize/fullscreen/close have only local state or a reviewer-side action log. No other source picker, reader or window UI has been invented.

## Verification and limitations

Headless Chrome under isolated Puppeteer user-data dir, local `file:` source and network-request blocking:

```powershell
node 'D:\Colosseum-Android-Phone-Mocks-20261101\outputs\05-volume-flow\run-review-captures.cjs'
```

This saves a real 1×PNG for each of ten presets/states **per variant** and `captures/geometry-review.json`, including computed focused-card clipping, visible assets, in-screen positions, screen scroll distance and browser exceptions. An additional `alternative/landscape-small-gallery.png` scroll capture shows the entire conditional flow. See that JSON for checks rather than inferring implementation success from static imagery. Only HTML behaviour has been exercised; **native QML and physical Android runtime are not validated**.

The project asset tree contains One Piece posters/illustrations but no exact titled manga volume jackets, so these illustrated covers are deliberately not passed off as genuine volume covers. The 48 entries, image-to-volume associations, download percentages, failure and ownership states are illustrative. Live catalogue, sources, layout performance on device, true touch ergonomics, source data accuracy and desktop pixel-perfect comparison remain unverified.

Do not adopt the alternative without explicit Hemanth review/approval; do not treat these mocks as patches to Colosseum, Android qualification, a build, a release or a publication green light.
