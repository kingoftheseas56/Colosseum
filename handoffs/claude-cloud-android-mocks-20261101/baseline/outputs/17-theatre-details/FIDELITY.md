# Theatre film and series details — fidelity ledger

**Task:** `17-theatre-details` · standalone, local HTML fitting studies, not an implementation or approved design change.

**Frozen authority:** `reference/desktop` at `a40f66be06f22cdbe75e97e7dc4f1108ab2f5d41`. The page actually loaded in `Main.qml` by default is `TheatreSeries.qml` (`viewSettings.rebuiltTitlePages=false`); `TheatreSeriesView.qml` is an explicitly opt-in rebuilt alternative. The normal review opens the **default legacy renderer**; the reviewer-facing renderer select changes only the mock. Original runtime view settings are not modified. The read-only alpha QML was consulted only for historical context. `MediaDetailScaffold.qml` is absent from approved desktop/alpha: the like-named component exists in rejected `android-drift`, and its flat-card substitutions are not used.

## Source-to-mock map

| Source/control | HTML counterpart / retained appearance | Interaction boundary |
| --- | --- | --- |
| `TheatreSeries.qml` hero `detailHero`, `bannerImg` and gradients | `.hero`, `.hero-art`, `.hero-wash`: original dark art treatment, bottom fade, dark ground, no new card container | Uses existing named local art, not fetched provider banners |
| QML kicker/title and `Theme.qml` | `.type-kicker`, `.hero-title`; original gold/white, letter spacing, Fraunces display and Segoe UI through bundled local font paths | Long titles wrap; `Part 2` is kept together typographically |
| Year, genres, IMDb gold plaque, rating, runtime | `.meta` and `.imdb-badge` in original flow | Static representative metadata explicitly labeled outside the app frame |
| `TheatreFacts.js` / `factRows` | `.desktop-facts .facts`, `.mobile-facts` with original muted keys and white values | Uses the actual source field names; populated data is illustrative |
| Legacy desktop synopsis below hero | `.desktop-facts .standalone-synopsis` | Full wrapping paragraph as in legacy default |
| Legacy Android phone full synopsis | `.hero-copy .synopsis` | Complete text above actions, no invented More control |
| `TheatreSeriesView.qml` rebuilt synopsis `More`/`Less` | `.synopsis.clamped`, `button.more` when **rebuilt** renderer is selected | Working local More/Less toggle; no toggle fabricated on legacy |
| Hero `Watch` `PlayerIcon` | Gold `.action.primary`; original vendored Lucide play glyph geometry via local CSS mask | Status outside frame describes approved SourcesSheet; **no playback or providers** |
| `LibraryButton.qml` | Ghost `.action.library` with + / gold ✓ | Local `Library` ↔ `In Library` only; no saved record mutation |
| `collections/AddToCollectionsButton.qml` | Ghost `.action` and original inline collection path | Preserves button/label; collection picker is not connected |
| Original saved-series notification toggle | `.action[data-action=notifications]` | Local on/off caption only, present only for mock-saved series |
| `BackAction.qml` | Fixed `.back`, canonical stroked chevron and original Back label | Local status only; invisible clearance for touch |
| Legacy minimize, fullscreen, close | `.window-controls` using real local icons, desktop only | Local status only; hidden in phone/Android tablet presentations |
| Opt-in `TitlePageBar.qml` | `.bar-chrome`: Back to Theatre, search, 3-dot menu; solid on scroll | Working open/close, Quit confirmation captions; no window operations |
| `TheatreSeries.qml` `seasonChromeFloat` / `chromeCol` | Sticky `.season-chrome`, legacy underlined Absolute/Seasons, >10-season pill dropdown, divider, ledger heading/count/actions, column captions | Switch order, season, open/close menu; dark upward scrim stops scrolled actions colliding with Back |
| Legacy `seasonMenuList` | `.popup-season` 236px wide, dark glass/border, 36px rows, active gold, scroll | 23-item illustrative season list, floating menu; constrained height only where necessary in 354px landscape |
| Legacy `episodeJumpPanel` / `jumpRanges` | `.popup-jump` number input, Jump and range chip; original Go to episode trigger | Local jump scrolls to selected episode, no source request |
| Opt-in rebuilt `ledgerBarFloat` | `.rebuilt-ledger`: source segmented Absolute/Seasons, original season dropdown, Download season, Next · E2, inline Go to # field | Local selection/scroll; **not** the legacy overlay Go-to |
| `episodeWindowRepeater`, `episodeVirtualSpace` | `.episode-list` and `.episode-row`: number rail, actual project arc stills, title, status, summary, gold next-up rail, progress, watched check | Complete 15-row illustrative season; a bounded virtual slice is not claimed/implemented |
| `episodeRow` per-episode Play and Download | Two 38px visual round `.circle-visible` controls in 44px interactive `.circle-action` bounds | No stream search/download; actions are local demonstrations only |
| `episodeContextMenu` | `.context-menu` with original Play/Download items, right-click an episode | Original context menu labels and local behavior |
| `CastRow.qml` | `.cast-section`: 78px initialed faces, original 96px columns, 8 plus All cast | Local expansion; no actor art invented |
| `MoreLikeThisRow.qml` | `.recommend-section`: 120 × 172 titled local art in horizontal scroller | Original title-open action reported locally, no navigation/provider request |

## Layout-only fitting choices

Portrait **412 × 900 and 354 × 792**: 216px banner over the flowing hero copy; source typography, chips, facts, synopsis and actions retain order. The actions wrap at their natural widths. The full title is visible and wraps without horizontal clipping. `Part 2` stays together rather than leaving a single orphan digit. Episode row retains number rail, still, round icons and text; those pieces **stack** within the row without replacing it by a new mobile card. The episode ledger keeps one vertical scroll owner. The app mock deliberately contains **no rendered Android status/navigation bars**.

Landscape **900 × 412 and 792 × 354**: same source art wash and complete details, smaller display title as necessary, vertically scrollable at shallow heights. Episodes use a horizontal ledger arrangement when width permits. The season menu still uses the same dark popup and 36px entries; its **visible vertical extent** contracts under the trigger at short heights rather than covering Back. The full list remains independently scrollable. Popup placement remains clamped inside the app frame.

Desktop **1280 × 700**: legacy hero returns to the source's **360px bottom-anchored** composition, synopsis/facts below, no Android controls. The opt-in rebuilt source uses its separate shared 76px TitlePageBar, not a mixture of old and new chrome. Tablet **800 × 1280** uses the approved QML `androidHost && width < 840` phone-narrow branch with wider room and no native desktop window controls; landscape tablet **1280 × 800** preserves the wider branch. Reviewer insets and font-scale fields change only sizing, not visual identity.

Outside the app frame are only the **review workbench**: preset, editable dimensions, inset and font scale, renderer choice, and shortcuts to long-list/popup states. These are not proposed Colosseum controls.

## Representative art and content

- Film hero: `reference/desktop/assets/wallpaper/sagas/harry-potter.jpg` (existing titled saga asset); not asserted to be an exact provider backdrop for *Deathly Hallows: Part 2*.
- Series hero: `reference/desktop/assets/universes/one-piece/new-world/banners/wano-country.png` (existing One Piece Wano artwork); a demonstration title backdrop, not provider art identity.
- Episode stills: real `reference/desktop/assets/universes/one-piece/east-blue/banners/{romance,orange,syrup}.png` and named Wano banner. Arc banners are **substitutions**, not episode-accurate screenshots. Never use blank or stock art.
- More Like This: real local `wallpaper/sagas/dune.jpg`, `the-witcher.jpg`, `lord-of-the-rings.jpg`, `harry-potter.jpg`. The recommendation associations and illustrative actors are not claims about any catalogue output.
- Existing font files are **referenced in place only**; none were copied to output. No CDN, stock-image API, external JS/CSS or live provider.
- A 22-numbered-season-plus-Specials listing is an intentional long-menu stress fixture; it is **not** a fetched One Piece season count. Episode data, metadata, cast and recommendations are illustrative. The workbench labels this outside the frame.

## Original behavior vs small alternatives

No new UI or behavior is proposed. The original dropdown was attempted and retained; only popup vertical clipping and scroll capacity are adjusted for shallow landscape. `Watch`, per-episode `Play`/`Download`, `Download season`, collection picker, global search and window controls have **no effects on account, file library, source provider or player**; the off-frame status communicates their existing intended destination. This is a declared mock boundary, not a replacement flow. The alternate rebuilt renderer and test shortcuts exist only in the off-frame workbench.

## Verification and limits

Headless captures use installed Google Chrome through BDC/Puppeteer with an isolated temporary profile rooted inside this output directory and removed on completion. Original QML and the live APK were **not** compiled, launched, changed or tested. A screenshot can establish rendered mock geometry, not app runtime behavior. `capture.js` is optional author-side verification infrastructure; the two HTML pages themselves require only local file serving from the batch root.

The viewport contract calls OnePlus sizes **provisional logical pixels**. No ADB/device check has been performed. Screenshot observations, popup bounds, horizontal overflow results, missing-local-image checks, keyboard action checks and their exact pass/fail boundaries are recorded separately in `captures/validation.json` and the corrected short pass `captures/validation-quick.json`, subject to the latter's actual results.

Exact commands used:

```powershell
node "D:\Colosseum-Android-Phone-Mocks-20261101\outputs\17-theatre-details\capture.js"
node "D:\Colosseum-Android-Phone-Mocks-20261101\outputs\17-theatre-details\capture.js" quick
```

Both commands are executed through **Brotherhood Desktop Commander on Laptop 1**. `capture.js` launches headless Chrome, not the shared browser UI. The snapshots are local PNG files under `captures/`; no publication or upload occurred.
