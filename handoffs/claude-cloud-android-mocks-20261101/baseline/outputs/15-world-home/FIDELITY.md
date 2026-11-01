# 15 · Theatre / Tankoban / Universes home — fidelity review

**Artifact type:** isolated, browser-rendered HTML/CSS/JS fitting studies, not QML implementation or Android runtime validation. All authored files belong exclusively to `outputs/15-world-home`. Approved sources, app code, repository state and sibling output jobs were not edited.

## Reference authority

- Approved, committed desktop UI: `reference/desktop` at `a40f66be06f22cdbe75e97e7dc4f1108ab2f5d41`. The original QML source and real art/fonts/icons there are the visual authority.
- Recovery comparison: `reference/alpha/qml` at `77844eef0bd3b7fee2c1f884be4a76db6d63321d`, especially `PhoneWorldActions.qml` where its original component has no approved desktop equivalent.
- Rejected drift reference was **not used as a visual target**. No flat-card or substitute-sheet layouts were adopted.
- Read-only task context: `context/ANDROID-PORT-CLASSIFICATION.md` and `context/VIEWPORTS.json`. The provisional phone sizes are **not** measurements of the currently disconnected OnePlus.
- Other transitive, read-only sources used: `Theme.qml`, `AdaptiveLayout.qml`, `Main.qml`, `+android/TopBar.qml`, `PhoneShellHost.qml`, `WideWorldRail.qml`, `TheatreTabBar.qml`, `ContinueRow.qml`, `DiscoverBrowser.qml`, `DiscoverSidebar.qml`, `UniverseBanner.qml`, `RowIndexSidebar.qml`, `Catalog.js`, `CatalogueVisualMetrics.js`, `Universes.js`.

## Per-page artifacts

| Page | Scope and screenshot states |
| --- | --- |
| `theatre-home.html` | Full Theatre shell; Theatre hero (Dune: Part Two, Shogun, The Bear, Fallout); illustrative Next Up and Continue Watching; Discover / Movies / Shows / Anime / Library; local rail, poster wall, rank rail, genre browse. |
| `tankoban-home.html` | Tankoban hero (One Piece, Saga, Berserk, Invincible); illustrative Next Up and Continue Reading; Discover / Manga / Comics / Library; blended comics/manga browse, rank rail and source/row indexes. |
| `universes-home.html` | Tribute carousel, including Cosmere as Tribute-only; Explore Universes heading, total and source-primary facet bar; stacked full-width universe banners and original marks/medium chips. |

An `index.html` links all three pages. `style.css` and `app.js` are shared by the three standalone mocks. An exterior review panel provides editable viewport width/height, four safe insets, font scale, page/state presets and mock action feedback. The review panel disappears entirely for `?capture=1` and is **not** rendered as Colosseum UI.

## Component-to-mock design map

| Source | HTML counterpart | Original design and behavior respected |
| --- | --- | --- |
| `TheatreWorld.qml` | Theatre page's `.featured`, two `.continue-section` regions, `#worldTabs` and subsequent catalogue | Theatre-specific **Watch / Details**, Next Up, Continue Watching and its five existing destinations. Progress is illustrative rather than connected. |
| `TankobanWorld.qml` | Tankoban equivalent | **Read / Details**, manga/comic blend, Continue Reading, four tabs and separate manga/comics rank boards. |
| `UniversesWorld.qml` | `universeSection()` after `.featured` | **Tribute / Enter tribute / Browse universe**, Browse suppressed for Tribute-only Cosmere, Explore Universes and counted full-width ledger. |
| `WorldPage.qml`, `AdaptiveLayout.qml` | `.world-scroll`, `.board`, viewport-specific CSS custom properties | Single world scroller under pinned top bar, original section rhythm, desktop 54px margin, smaller phone margins and 260/300/340px hero sizing. |
| `Theme.qml` | CSS tokens, bundled Fraunces face and Windows Segoe UI | `#f0c44a` gold, original off-white/ink hierarchy, original glass opacity and borders, Fraunces headlines and Segoe UI controls. No font binaries redistributed. |
| `TopBar.qml` / `Main.qml` | `.topbar` / `.world-pills` | Desktop `‹ Home`, large clock/date, centered world pills and right-side native action SVGs plus desktop window buttons; six desktop pills including the original SOON markers. |
| `+android/TopBar.qml` / alpha `PhoneWorldActions.qml` | Compact `.topbar` / `.icon-action` | Mobile clock/AM-PM, right Search/Stremio (Theatre)/Trackers/Account/Wallpaper actions, original project SVGs and outlined account silhouette. No extra phone system chrome, Home or desktop window buttons. |
| `PhoneShellHost.qml`, `PhoneWorldTabBar.qml`, `WideWorldRail.qml` | `.bottom-world-nav` and `.wide-world-nav` | Same Theatre/Tankoban/Biblio/Universes/Profile order, real SVG world glyphs, gold active treatment, opaque `#0a0b10` substrate. Bottom bar on phone and narrow tablet; 92px side rail on wide tablet. Desktop uses original centered pills, not the Android rail. |
| `FeaturedCarousel.qml` | `.featured` plus `.carousel-indicators` | Same four slides per world fixture, 7px inactive and 22px gold active indicator, 150ms width transition, 22px dark backing, padded invisible pointer hit zone; dot-click, horizontal swipe and keyboard arrow changes. |
| `CarouselSlide.qml` | `.hero-art`, `.hero-shade`, `.hero-ghost`, `.hero-copy`, `.hero-action` | Fullbleed artwork with left dark wash, editorial gold uppercase kicker, Fraunces title, source blurb clamp and primary gold/secondary glass buttons; inset poster on wide formats and inline bleed on narrow. |
| `ContinueRow.qml`, `ContinueTile.qml` | `.continue-section`, `.continue-tile` | Serif row heading, **See all ›**, 148×222 poster rails, in-tile title/subline/source badge, bottom gold progress, 46px central resume circle, selected/watched and hover/remove states, original context action group. Empty progress hides rows. |
| `PosterRail.qml`, `CatalogueVisualMetrics.js` | `.poster-rail`, `.poster-card` | Gallery-width covers, two-line titles, 20px horizontal rhythm, horizontal overflow/panning without a visible scrollbar. |
| `TrendingTop10.qml` | `.rank-stage`, `.rank-strip`, `.rank-card`, edge chevrons | Large ghost rank numerals behind covers and original previous/next panning affordances. |
| `WorldTabBar.qml`, `TheatreTabBar.qml` | `.glass-tabs` and `.world-tab` | Single frosted rounded strip, original words/order, gold active plate and in-flow strip becoming compact/sticky on scroll. No replacement sheets/cards. |
| `DiscoverBrowser.qml`, `DiscoverSidebar.qml` | `.discover`, `.source-rail`, `.discover-masthead`, `.filter-row`, `.picker-menu`, `.poster-wall` | Underlined type lenses, **NOW BROWSING** Fraunces catalogue and source byline, original FILTER dropdown family, 52/240px source rail, **WORLD**/world-name brand and 28px round-chevron toggle. Source menu uses a true overlay on phone as the QML does. |
| `RowIndexSidebar.qml` | `.index-rail`, `.row-index-entry`, `.rail-svg` | Closed 52/open 240px column, **WORLD**/world-name brand, 28px edge chevron, copied semantic bookshelf/star/clock/compass glyph paths, active gold rule and row-jump action. |
| `UniverseBanner.qml`, `Universes.js` | `.universe-banner`, `.banner-art`, `.banner-mark`, `.banner-copy`, `.banner-enter` | 178px desktop full-width image banners, left wash and 98px mark column, source Fraunces heading, clamped description, held-medium chips and hover/focus **Explore ›**. The filter uses `Universes.js` `PRIMARY` classifications, **not** every medium in the chips. |

## Fitting-only geometry decisions

- Phone orientation uses its **short side**, so 900×412 / 792×354 retain phone navigation rather than wrongly appearing as desktop. 412×900 and 354×792 use 18px margins; rotated phone uses 24px. Desktop remains 54px. The CSS keeps native rails horizontally scrollable rather than shrinking every cover.
- Original carousel copy stacks inside the hero at narrow widths: 20px left / 22px bottom, 34px (or smaller short-screen) Fraunces title, two-line source blurb and wrapped original primary/secondary actions. The original 260px phone hero remains visible in landscape; longer content scrolls in the world page.
- Theatre and Tankoban's in-flow original tab strip stays in the content after featured/Continue and becomes the same compact glass strip at the scroll top. It is not moved to the Android OS/footer region.
- Discover type lenses and catalogue title stack on narrow pages; source rail expansion covers the wall on phones (already approved in `DiscoverBrowser.qml`), preserving artwork space. Expanded rail labels and source catalogue popup stay intact.
- RowIndexSidebar was inspected **first** with its original push-content geometry: open 240px + 28px causes too little poster space on 354/412px phone widths. The desktop/tablet mock retains the source push-content behavior; the phone fitting state layers the same original 240px rail over content instead. Both presentations can be reviewed by switching desktop/phone viewport presets; the icon, toggle, focus destination, labels and row-request semantics remain unchanged.
- Universes keeps image-bleed banners. Only the mark column/padding shrinks and long titles wrap on phones; no banner becomes a generic card. The source held-medium chips still describe contents, while primary facets classify the universe correctly.
- Controls remain within an app viewport with adjustable left/right/top/bottom insets; the bottom world bar adds the requested bottom inset. Insets belong to the review editor, not invented OS status/navigation UI.

## Data, imagery, and uncertainty

The data shown is a **labelled illustrative local fixture**, not runtime metadata, installed-extension truth, the Progress store or an authenticated account. All shown images come from **real local project asset files** via `../../reference/desktop/assets/...`; no external image URLs, web API calls, stock photos, generated blank posters or redistributed fonts are used. The visible titles/feature blurbs come from `Catalog.js` or `Universes.js`, except for the descriptive per-item progress fixtures. The dynamic wallpaper source is user/config dependent in QML; the mock uses the real local `wallpaper/cold-ripple.jpg` as a wallpaper fixture.

**Art substitutions:** local Dune saga art illustrates `Dune: Part Two` (not an exact film still). Shogun/Oppenheimer/Vinland Saga use `ice-and-fire.jpg`; The Bear/Berserk and some other unavailable titles use `the-witcher.jpg`; Fallout uses `jurassic-park.jpg`; Saga uses `extensions/house/cosmere.jpg`; Invincible uses a DCAU portal. Poster walls reuse existing titled saga/portal art for works without a matching packaged cover. The One Piece art/marks, DC Animated Universe art/mark, Star Wars art/mark and Harry Potter saga art/mark are actual corresponding project franchise assets. Naruto's sample universe banner is **substituted One Piece Wano artwork**, while its `naruto.svg` mark is genuine. Do not construe substitutions as approved cover selections or the live product catalogue.

**Remaining uncertainty:** exact OnePlus display dp, safe insets, gesture/keyboard behavior, native QML font rasterization/animations, connected catalogue/extension state and any true Android runtime layout remain unverified. The local HTML is not an APK, does not compile or invoke QML, and no account/provider routes are exercised. No publication or beta change was attempted.

## States and capture procedure

The exterior state selector opens the original-type source rail, category picker, filter picker, Continue context actions, second carousel slide, long title, empty progress and Books primary facet. In the frame, carousel dots/swipe/arrows, tab changes, source menu expansion, source/type/catalogue/filter selection, Continue resume/remove, ranked chevrons and universe facet selection work locally. Backend-only actions show intent in the exterior log and deliberately perform no I/O.

Headless captures are stored at `captures/{theatre-home,tankoban-home,universes-home}/{portrait,landscape,desktop,compact-portrait,compact-landscape,tablet-portrait,tablet-landscape}.png`, produced by `node capture-check.cjs` in an isolated browser context. Preset viewport dimensions are **412×900, 900×412, 1280×700, 354×792, 792×354, 800×1280 and 1280×800** respectively, at device scale factor 1. The script also records source image-load, top-bar intersection and interaction observations in `captures/verification.json`. The command uses only a locally preinstalled browser and Puppeteer, without global installation or driving shared browser tabs.

**Review boundary:** screenshot evidence validates the static HTML's browser presentation only. It does not prove visual or functional correctness inside Colosseum's QML renderer.

