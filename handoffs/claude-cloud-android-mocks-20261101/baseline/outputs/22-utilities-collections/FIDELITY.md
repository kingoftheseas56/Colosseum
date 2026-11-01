# Android utilities and collections — fidelity ledger

Task: **22-utilities-collections** · HTML fitting study only · **not an Android QML implementation, app runtime test, or publication candidate**.

## Authority, provenance and scope

The only product reference is the frozen committed desktop QML snapshot at `reference/desktop`, commit `a40f66be06f22cdbe75e97e7dc4f1108ab2f5d41`, inclusive of the reviewed Connections refinement `05c961914` and collection Back/close refinement. Source paths below are relative to the batch root `D:\Colosseum-Android-Phone-Mocks-20261101`. `context/ANDROID-PORT-CLASSIFICATION.md` and `context/VIEWPORTS.json` provide classification/provisional viewport requirements. The historical captures are descriptive only and were not used as successful mobile proof. `reference/android-drift` is not a design authority. The **original approved desktop**, not the newer-looking rejected Android flat-card styling, is the visual baseline. `reference/alpha` was available as a recovery reference but not required for the faithful parent renderers below.

The local study files are `wallpaper-search.html`, `extensions-store.html`, `update.html`, `collections.html`, `index.html`, `style.css`, `app.js`. They use only local project images/fonts/icon SVGs, CSS and in-memory JavaScript. Local review controls (editable dimensions, presets, insets, font scale, keyboard reservation and state) live **outside the mock**. No OS chrome is drawn as product UI. At narrow sizes, the reference desktop window controls are omitted because they are not Android controls. The original app routes, provider operations, download/update/install/apply commands, account and collection storage remain untouched.

## Mapping — wallpaper-search.html

| Approved source component / action | HTML counterpart and state | Fitting |
|---|---|---|
| `qml/WallpaperSearch.qml` backdrop / dimmed live preview | `.wall-wallpaper` and `.wall-scrim` using actual local project wallpaper art | Full-bleed under original floating panel; selected art changes locally |
| Floating 520px glass picker, radius18, blurred edge, heading “Wallpapers” + close | `.wall-panel`, `.wall-head`, `.wall-close` | Desktop x40/top60; phone near-edge inset12 and scrollable interior, same hierarchy |
| `TextField` + “Search” gold capsule | `#wallSearchInput`, `#wallSearchForm` | Input flexes, button retains capsule; local fixture filtering only, no Wallhaven request |
| Relevance / Top / Random gold selected pills | `[data-act=wallSort]` | Wrap permitted, never converted to tabs/selector sheets |
| Animated / Native / KDE native `ListView` shelves | `.wall-section`, `.wall-shelf.scroll-x`, `.wall-tile` | Original 144×92 tiles, 8px radii and title/artist overlay; horizontal scroll in narrow view |
| `GridView` Wallhaven results + Wallhaven source chips, Load more | `.wall-results`, `.wall-source-chip`, `[data-act=wallMore]` | 144px min tiles wrap to width; local fixture-only “Load more” |
| Selected wallpaper overlay title/spec + scope pills | `.wall-selected`, `.wall-scope` | Desktop anchors to right52/bottom52; phone uses glass-panel lower lane, with scroll above; landscape original right-of-picker arrangement |
| “For All Worlds” / “For Home” apply | `[data-act=wallApply]` | Both visible and focusable but **inert**; no wallpaper writes |

Native shaders (Noir Flow, Aurora Flow, Low Poly) and native QML gradient scenes (Twilight, Ember, Mint) are **not representable as their running QML shader scenes** in this HTML study. Their names, aspect, dimensions, order, captions, border and selection remain true; corresponding tiles use clearly declared project-image substitutes and no animation is claimed. The KDE shelf and Wallhaven grid have no locally bundled provider thumbnails: their sample images are project wallpaper assets substituted strictly for this offline visual study. No remote thumbnails or live search are queried.

## Mapping — extensions-store.html

| Approved source component / action | HTML counterpart and state | Fitting |
|---|---|---|
| `qml/ExtensionsStorePage.qml` fixed Jolly Roger darkened wallpaper | `.ext-background` + source `assets/extensions/store-jolly-roger.jpg` | True full-bleed stage under independently scrolling world board |
| `WorldPage.qml` + `TopBar.qml` back-only “Extensions” + exact subtitle | `.ext-shell-head` + original chevron/title/subtitle | Pinned top Back and heading, no substitute global navigation |
| `ExtensionsHouseHero.qml` banner, pearl/glass tray, **eight** House icon tiles, name chips and source +/- circles | `.ext-hero`, `.house-tray`, `.house-row`, `.house-tile`, `.house-toggle` | **Same tiles, not rejected flat rows**: desktop scales across tray; phone keeps icons and chips in a horizontally scrollable tray, with 44px toggle hit area around original 28px circle |
| Search and “Install from URL” controls | `.hero-actions` | Exact functions represented locally; URL state opens original dark modal, no provider request |
| `ExtensionsInstalledSection.qml`, `ExtensionsInstalledCard.qml` | `.ext-installed-list`, `.installed-card`, “See all ›” | Original heading/subtitle, 320px dark bordered cards, logo, status/capabilities; horizontal rail |
| `ExtensionsStoreRail.qml`, `ExtensionsStoreCard.qml` | `.ext-section`, `.ext-cards`, `.store-card` | Desktop cards use original art stage/logo/title/description; narrow phone stacks first four on each rail, preserving original mobile `phoneLayout` intent, not inventing new cards |
| `ExtensionsInstalledPage.qml` management | `.ext-subpage` in “Your Add-ons” state | Back “Store”, original heading, manage cards with Disable/Enable/Configure/Remove; local only |
| `ExtensionsSeeAllPage.qml` search/browse | `.ext-subpage` in Search/See all states | Underlined serif search, existing descriptive helper, card grid |
| `ExtensionsStorePage.qml` install-from-URL original popup | `.ext-url-modal`, `.url-preview` | Original centered dark radius24 dialog, address field, helper, Cancel/Preview, canned example preview; no fetching or installation |
| Source remove confirmation | `.modal-shade` in remove state | Cancel/Remove affordances; only local fixture is affected |

The store's live stremio-addons.net list, stars and provider manifests are unavailable offline and **not faked as live**. Named fixtures use strings and genuine add-on names from `qml/ExtensionsStoreApi.js`, `qml/ExtensionsCatalog.js`, original installed/House renderer, and project logos. Installed counts and state are illustrative. A hypothetical add-on “Set up” does not open another worker's actual configuration page. The separate `+android/ExtensionsHouseHero.qml` contains a flat rows fork; this study deliberately retains the desktop-approved white icon-tile tray in accordance with this assignment, with a horizontal scroll fit rather than silently replacing it.

## Mapping — update.html

| Approved source component / action | HTML counterpart and state | Fitting |
|---|---|---|
| `qml/UpdatePage.qml` capsule Back / desktop glyph chrome | `.update-back` and `.window-controls` | Top left original capsule Back; desktop-only system glyphs |
| `qml/update/UpdateLivingGallery.qml` opaque monochrome stage | `.update-art` and `.update-scrim` | One project artwork, `filter:grayscale(1)`, directional dark overlays; no invented stock imagery |
| “COLOSSEUM UPDATE” masthead + release version | `.update-headline`, `.update-eyebrow`, `.update-version` | Fraunces display, Inter eyebrow, protected Back lane; phone sizes/wrap |
| Editorial section/title/body | `.update-copy` | Left/bottom chapter lane desktop; phone portrait stacks above nav; landscape splits copy left/status right |
| Page number, section, number selectors, Next | `.update-nav`, `.update-count`, `.chapter-button`, `.chapter-next` | Same circular 44px targets, number underline, original left/right keyboard navigation |
| Independent status/meta/progress/primary CTA | `.update-status`, `.update-progress`, `.update-primary` | Bottom status desktop, below chapters portrait; separate right column landscape. Original phase labels from `UpdatePage.qml` |
| Check again/Download/Pause/Resume/Restart | `[data-act=updatePrimary]` | **Visual state transition only**; no download, check, verifier, restart, or updater call |

The illustrative release scenario (installed 1.1.10, hypothetical available 1.2.0) and three illustrative chapters demonstrate text loading and navigation, **not evidence that 1.2.0 is available**. Chapter titles/body use source UI wording; actual update data is runtime-generated and not part of frozen local QML. The fallback art `assets/wallpaper/captured-motion.jpg` is the actual source fallback. Original monochrome effect is represented by CSS, not claimed pixel-equivalent to Qt Quick MultiEffect.

## Mapping — collections.html

| Approved source component / action | HTML counterpart and state | Fitting |
|---|---|---|
| `qml/collections/CollectionsPanel.qml` wallpaper wash and shell | `.collections-wallpaper`, `.collections-tint`, `.collections-scroll` | Desktop dark Biblio wash and optional closed dock; **no Android taskbar invented** |
| `YourCollectionsPage.qml` top Back action, editorial heading/subtitle | `.collections-top`, `.back-plain`, `.collections-heading` | Reviewed Back remains left aligned; heading remains Fraunces |
| `CollectionSearchField.qml` Find a collection / clear | `.collection-search`, `#collectionSearchInput` | Exact 44px bordered field, original magnifier and clear |
| `CollectionActionButton.qml` “New collection” | `.collection-action.primary` | Original gold tone/radius10/plus glyph; local create modal |
| `CollectionShelfCard.qml` stacked, rotated covers and shelf metadata | `.shelf-card`, `.shelf-art`, three `.shelf-cover` layers | Preserves fanned image composition, tint and unoutlined shelf; responsive 3→2→1 columns depending width |
| `CollectionPage.qml` Back “Your Collections”, lock, title, desc, Edit/trash | `.collection-detail`, `.collection-private`, `.detail-actions` | Original deep detail route kept within same page; phone title/details/buttons wrap and stack without hiding controls |
| `CollectionMediaCard.qml` 1.43-ratio cover/title/kind/note | `.collection-media-grid`, `.media-card`, `.media-cover`, `.media-note` | Responsive 5→3→2 columns, original postery image aspect |
| `CollectionPage.qml` All/Books/Series/etc filters, editing and reorder actions | `.collection-filters`, `.media-edit-controls`, `#collectionNameEdit` and note fields | Existing filters, editable name/desc/note, arrows/remove. Unsaved-edits choice can be demonstrated locally |
| `AddToCollectionsPopup.qml` create and memberships | `.collection-popup`, membership rows, `[data-act=toggleMembership]` | Original centered dark modal radius18, source labels, 48×52 cover, check square, Save/Cancel; no account or collection-store writes |
| `DeleteCollectionPopup.qml` | `.collection-popup` delete state | Original title/description and Cancel/red Delete collection |
| Desktop Taskbar closed state vs Android dummy | `.collections-dock` only desktop | QML `+android/Taskbar.qml` does not render a desktop dock; phone study does not invent one |

The task lists `CollectionGrid.qml` and `CollectionMenu.qml`; **neither exists** as a standalone component in the approved desktop QML root. This is not a defect or authorization to use substitute Android-drift controls: the inline `YourCollectionsPage.qml` shelf grid and `CollectionPage.qml` media grid/header/edit renderer are the real reference. All sample covers are **named existing Colosseum saga wallpaper assets**, not stock or randomly blank posters; the study shows a collection-card/poster arrangement with these valid local art samples, not the actual user's private collection content. Names, counts, membership and notes are fixture-only, as stated **outside** the app frame.

## Fitting choices (not redesigns)

The exact editable review presets are **412×900 portrait**, **900×412 landscape**, **354×792 compact portrait**, **792×354 compact landscape**, **1280×700 desktop**, **800×1280 tablet portrait**, and **1280×800 tablet landscape**, in provisional *logical CSS pixels*. Inset controls reserve top/bottom and an illustrative keyboard lane. Font scaling is selectable from 0.85 to 1.4 (the contract’s 1.3 stress state is included). Dimensions are not claimed to match the currently disconnected OnePlus. The layout classifier uses compact-phone geometry where the short side is below 600px, with larger tablet/desktop width classes. None of this draws Android status/navigation bars as Colosseum UI.

- Rearrange shell lanes and allow wrapping/scrolling while retaining component geometry and palette.
- Keep genuine 144×92 wallpaper tile strips and source labels, rather than transforming them into cards.
- Keep the desktop-approved glass tray with House app icons in a horizontally scrolling lane on phones; enlarge touch hit targets, not icon designs.
- Keep release art, section and version editorial composition and two separately addressable bottom lanes; in narrow landscape the status becomes the right-hand lane.
- Preserve collection shelves with rotated real artwork and media posters; grids reflow 3→2→1 or 5→3→2; Back and delete/close stay available.
- Modal height is bounded by safe inset and keyboard simulation, without introducing sheets.

## Behaviour alternatives vs original and why

No new product interaction or do-or-die UI exception is proposed. **Offline simulation** necessarily differs from the live app: wallpaper search/load-more and scope actions never access Wallhaven or apply to the product; Store URL preview is a canned **source-backed example** (Torrentio or Comet only), Store installed/House toggles are in-memory; configurable provider setup does nothing; update action advances only a local demonstration phase; collections create/delete/save/toggle/reorder modify only in-memory fixtures. These are not proposed replacement runtime behaviours and cannot be used as functional acceptance evidence. Real QML behavior is preserved as the target.

## Evidence boundaries, commands, and uncertainty

Each HTML page is meant to be served from a local HTTP server rooted at **the batch root**, not from the output subfolder. It uses relative URLs such as `../../reference/desktop/assets/...`; no CDN, fetch/XHR, embedded credentials, or external resources are needed. The optional `capture-audit.cjs` runs an **ephemeral loopback-only** HTTP server and installed Puppeteer + Edge, with network requests outside that localhost denied. It opens isolated headless browser pages, writes PNG captures and JSON observations under `captures/`, then closes its browser/server. It imports the already installed BDC host Puppeteer solely as **local test tooling**, not as an HTML dependency; it installs no packages.

Render/verification command on Laptop 1 (run through BDC start_process):

```powershell
node "D:\Colosseum-Android-Phone-Mocks-20261101\outputs\22-utilities-collections\capture-audit.cjs"
```

Alternative standalone isolated Edge command, also used for the initial independent screenshots:

```powershell
& "C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe" --headless=new --disable-gpu --disable-background-networking --no-first-run --disable-extensions --user-data-dir="D:\Colosseum-Android-Phone-Mocks-20261101\outputs\22-utilities-collections\headless-runs\wallpaper-desktop" --screenshot="D:\Colosseum-Android-Phone-Mocks-20261101\outputs\22-utilities-collections\captures\wallpaper-desktop.png" --window-size=1280,700 "file:///D:/Colosseum-Android-Phone-Mocks-20261101/outputs/22-utilities-collections/wallpaper-search.html?capture=1&preset=desktop&state=Selected"
```

The required captures use **412×900 portrait**, **900×412 landscape**, and **1280×700 desktop** at device pixel ratio 1. Additional stress captures exercise **354×792 / 792×354**, font scale 1.3, keyboard reservation, source modal/management and Collection editing. `captures/audit.json` records observed image loading, script/network errors, visible hit targets, overflow, key rectangles, and interaction smoke checks **only if the headless audit completes**. A PNG proves HTML browser paint at its given viewport, not QML paint, Android performance or backend functionality.

Open issues that cannot be settled from these references: exact OnePlus logical dimensions/safe areas while disconnected; live provider search/catalog/manifest output; installed extensions and personal collections; update release data and artwork URLs; rendering equivalence of QML shader/native wallpapers, Qt Quick blur and font metrics; Android runtime/touch/IME behavior. No app source, repository, build, device, upload, publish, or beta was touched.
