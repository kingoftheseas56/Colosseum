# Generic Saga and Shelves universe — fidelity and fitting record

Scope: local HTML/CSS/JS fitting studies only for 19-generic-universes. Not a Colosseum source change, implemented app, APK/device test, real provider lookup, upload or publication. Publication remains HELD.

Authority: approved committed desktop reference at a40f66be06f22cdbe75e97e7dc4f1108ab2f5d41. Inspected the read-only context/ANDROID-PORT-CLASSIFICATION.md, context/VIEWPORTS.json, QML page sources, Theme.qml, Main.qml (template routing at line 5628), BackAction.qml, Glass.qml, ContinueTile.qml, UniverseChromeAction.qml, UniverseTile.qml, UniverseExtensionPage.qml, SagaApi.js, Universes.js, and necessary original assets. The named UniverseScaffold.qml, UniverseFeed.qml, UniversePosterRail.qml, UniverseShelfViewport.qml are absent in approved desktop; source has its own inline renderers. Rejected Android-drift helpers were not borrowed. The source original takes precedence over dated screenshots.

## Saga universe — saga-universe.html

| Original source | HTML counterpart and state |
| --- | --- |
| SagaUniversePage wallpaper, world scrim and theme | .wallpaper with the real local Harry Potter saga wallpaper; directional wash; approved white/dim/dimmer, sparse gold, glass border/tint; Fraunces editorial face and Switzer UI from local assets |
| Original 400px desktop / 420px compact hero, SAGA, title, fact chips, Read/Watch | .hero, .hero-name, .fact-flow, .primary-action; source pill rhythm; wrap instead of shrinking content |
| BackAction outlined chevron and UniverseChromeAction SVGs | .back-action vector arrow, .window-actions source icons; desktop actions only, hidden on phone/tablet Android |
| ContinueTile home 340×148, 112 art, gold progress, 46px resume | .continue-card, .continue-art, .continue-bar, .continue-resume; populated sample progress with local-only details/resume logs |
| SagaGrid: 7 stories, eight effective columns due Deathly Hallows, BOOKS/FILMS rows | .matrix-panel, .story-heads, .matrix-row, .matrix-window, .work-tile; horizontal touch scrolling, edge chevrons, 124×186 tiles, progress and selected gold edge |
| SagaTile pointer/keyboard selection and under-grid detail line | selectSaga on hover/focus, .saga-detail title, medium, year/author, action and original owning-world hint; clicking opens locally simulated route, no backend |
| Beyond the saga, Upcoming, Gallery, Creators, Information, More universes | .branch-grid and original titled Hogwarts Library / Fantastic Beasts groups, UPCOMING plate, .gallery-row, .creators initials, .information-panel, .more-row |
| Source glass tab switcher riding hero then docking | .tabbar/.tabs-track with exact original The saga / Beyond the saga / Upcoming / About, active contrast, local section scrolling |

The saga representative fixture is Harry Potter, using approved curation in Universes.js: seven story pairings, seven main books, eight original saga films, companion books, three Fantastic Beasts films and the future television work. The template has other short-saga and side-detail variants that this single representative fixture does not prove. Live provider synopsis, rating and individual image data are not locally available and are not fabricated.

## Shelves universe — shelves-universe.html

| Original source | HTML counterpart and state |
| --- | --- |
| Shelves hero, category, facts, Read/Watch | Same source .hero, original UNIVERSE kicker and glass facts; both action pills on James Bond fixture, Read only on manga-only Weekly Shonen Jump |
| activeShelves / panelLayout medium packing | .shelves-layout with content-width short panels sharing lines on desktop/tablet and long panels clamped to full content width; one panel per line on phone |
| MediumSection and row runs | .medium-panel (radius 18 glass), .medium-heading/count, .run-label 26px, .run gap 28px, .rail and .shelf-chev; original 164×246 tile and 16px spacing |
| ShelfTile metadata/progress | .shelf-card, .work-tile, caption/year, UPCOMING and LIVE ACTION plate where relevant, watched mark, partial-progress stripe |
| TableSection magazine type | Optional source-fixture selector outside app frame switches to Weekly Shonen Jump; .magazine-panel and .magazine-row preserve 20 Now + 20 Classics records, numbering, source order/as-of, author, year and progress; desktop/tablet two columns down-first, phone one column |
| Glass tabs, Continue, Gallery, Information, More universes | .tabbar with medium counts and About; .continue-card, .gallery-row, .information-panel, .more-row in the same information hierarchy |
| BackAction and desktop window glyphs | The same shared source-faithful .back-action and .window-actions; no Android OS chrome invented |

James Bond example: approved 25 film titles split into actor runs (Sean Connery through Daniel Craig), 14 books, original metadata from Universes.js. Table example: Weekly Shonen Jump approved two 20-entry panels, source order and dated fixture (October 2026). These are illustrative static fixtures, not a live current catalogue.

## Fitting and decisions

Phone presets: 412×900, 900×412, 354×792, 792×354. Preserve compact 24px gutter, 40px Fraunces hero title, original 48px white round actions, 46px glass switcher, swipable story matrix and poster rails, wrapping chips, 2-column gallery/more panels, and stacked detail/branch/information areas. A 354px-high landscape view shows the upper hero and scrolls vertically to all later sections; nothing has been cropped from the DOM or redesigned into mobile cards.

Tablet presets: 800×1280 and 1280×800. Preserve medium grouping, labeled horizontal rails and two-column numbered magazine with narrow text reflow rather than phone-card substitutions. The Android window glyphs remain hidden; the visual layout remains rich like desktop. Desktop 1280×700: 64px gutter, 72px serif hero title, original row/runs/glass, Back and all three window actions. On compact magazine rows, index, cover, title, author and year are stacked within the *original 78px numbered row* to prevent text overlap; no fields removed.

Editable width, height, top/right/bottom/left safe-area insets and 1.0–1.6x type scale exist in the lab ABOVE/OUTSIDE the app frame. 24px top/bottom is an example only: OnePlus dimensions and OS insets are provisional, not measured. No invented status bar or keyboard chrome.

**Behavior alternative for review, not silently approved:** The original wide-screen switcher floats under hero then docks at y22 beside Back (desktop reference mode shows original). Tried keeping that desktop position conceptually on 354–412px, but its 4 original 36px pill tabs cannot all fit beside Back without overlap, clipping or forbidden micro-tabs. Phone mode retains the original tab shape, labels and active styling but docks below Back (y74 plus safe top) and allows horizontal pill-track swipe. Both options are displayed in the desktop/phone presets. This is a small proposed phone fitting behavior and requires Hemanth's review before changing QML, not a product approval. No new-sheet/new-control exception proposed.

The original QML manages single keyboard collection stops with custom arrow navigation. HTML exposes semantic keyboard-focusable controls but does NOT claim full Qt focus-ring and keyboard controller parity. Touch scroll and existing local action semantics are demonstrated. Source callbacks route to mock-only audit text outside the frame, not real Theatre/Biblio/Tankoban or OS operations.

## Real local art, source font and disclosure

The frozen source has no locally bundled individual Harry Potter Apple Books/Cinemeta covers, Bond movie/book posters or all magazine manga jackets. No network requests are used to fill them. Instead these **review-only titled asset stand-ins** are declared here:
- Harry Potter book/film/still proxy: real assets/wallpaper/sagas/harry-potter.jpg. Not an item-specific poster.
- James Bond film/book/still proxy: real assets/universes/marks/james-bond.png on a dark plate. Not the corresponding poster.
- Weekly Shonen Jump manga proxy: real assets/universes/marks/shonen-jump.png. Not distinct manga jackets.
- Bond/Jump stage fallback: real assets/wallpaper/cold-ripple.jpg (no remote franchise hero); sibling universe cards use real titled saga wallpaper and universe marks.
- Fonts load from real desktop assets/fonts and are not copied/distributed. Qt Glass ShaderEffectSource and blur are approximated by layered CSS backdrop-filter.

The lab header labels illustrative data OUTSIDE the mock frame. All in-frame stories, media, controls and text are curated representative information from named source; synthetic covers and product copy were not created.

## Verification method and boundary

Source and output files are on Laptop 1 only. JavaScript syntax check:
    node --check "D:\Colosseum-Android-Phone-Mocks-20261101\outputs\19-generic-universes\app.js"

Isolated headless browser capture command:
    node "D:\Colosseum-Android-Phone-Mocks-20261101\outputs\19-generic-universes\captures\render-captures.cjs"

Capture script uses already-present BDC Puppeteer and headless local Chrome, no global dependency install and no shared Windows tabs/mouse/keyboard. It opens the local file URL with ?capture=1&v=portrait (or landscape/desktop/narrow/tablet); viewport sizes are exact provisional CSS pixel dimensions listed above. It also records the Weekly Shonen Jump alternate fixture. Files are in captures/, including saga-portrait.png, saga-landscape.png, saga-desktop.png, shelves-portrait.png, shelves-landscape.png, shelves-desktop.png and source-fixture comparisons.

The successful full audit (captures/layout-audit.json) recorded all local images ready, no page-level horizontal overflow, and no JS page errors for the seven Saga, seven Bond and three Jump mode combinations. A later browser pass showed the corrected Bond Watch control and the stacked narrow magazine metadata without overlap. The browser tool intermittently hung on subsequent reruns, so older captures must not be described as a fresh full pass after every tiny static edit. Rail children outside the initial frame are intentionally horizontally scrollable; raw clipped-button counts include those reachable content rails and are not evidence of document overflow. No app source compiled, executed in Qt, installed or ADB-tested. Real device measurements, OS keyboard avoidance, native focus semantics, local provider data, live routes, and Qt GPU blur fidelity remain unverified.

No external project, reference tree, other output directory, or beta was changed. The HTML assets are referenced by relative URL, compatible with an HTTP server rooted at the batch root. The actual app implementation is left to its owners.

