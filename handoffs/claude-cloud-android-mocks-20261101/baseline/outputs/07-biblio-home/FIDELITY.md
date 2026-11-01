# 07 — Biblio Discover, home and book rails: fidelity review

**Authority:** Frozen approved desktop reference at a40f66be06f22cdbe75e97e7dc4f1108ab2f5d41, including reviewed Connections change 05c961914. Read-only source root: D:\Colosseum-Android-Phone-Mocks-20261101\reference\desktop. Alpha and rejected Android drift were not used as preferred design. This is an HTML/CSS/JS fitting mock, NOT a native QML implementation.

**Crucial source distinction:** BiblioWorld's default **Discover** tab is a catalogue wall. The book-rail **Explore** tab is separate. ReadingDesk is a widget on the global Colosseum Home, not inside BiblioWorld. All three surfaces are kept distinct in the external review controls. The third Biblio world tab, Library, is represented for shell context only.

## Source-to-HTML component map

| Approved source QML | HTML representation and preserved treatment |
| --- | --- |
| Theme.qml, Main.qml, WorldPage.qml | Dark Biblio wash over real project wallpaper, source gold #f0c44a, ink colours, Segoe UI, Fraunces; WorldPage horizontal margins, section spacing and world-level vertical scrolling. |
| BiblioWorld.qml | Featured carousel followed by Continue Reading, glass Discover / Explore / Library tab capsule and retained tab body. Discover is default, not silently replaced by shelves. |
| TopBar.qml | Desktop Home/clock, original six world capsules and SOON markers, Search/Trackers/Account/Wallpaper, window buttons. |
| +android/TopBar.qml | Android clock and the four original search/tracker/account/wallpaper controls; no desktop Home or window controls. Visible 22px glyphs retain their styling; transparent 44px mobile touch boxes. |
| PhoneShellHost.qml / PhoneWorldTabBar.qml | Original 72px plus bottom-inset, opaque dark bottom navigation for Theatre, Tankoban, Biblio, Universes and Profile; Biblio active gold. No invented OS bars. |
| WideWorldRail.qml | The exact same worlds presented on the existing 92px left rail at Android widths of at least 840 logical px; no phone bottom bar at that breakpoint. |
| FeaturedCarousel.qml / CarouselSlide.qml | 260px phone, 300px tablet and 340px desktop hero, rounded dark artwork, gold FEATURED IN BIBLIO, Fraunces title, Read / Details buttons, original dot indicators and local swipe between four slides. |
| ContinueRow.qml / WidgetHeader.qml | Distinct 22px Fraunces Continue Reading heading and lowercase See all ›. World ContinueTile artwork is 148×222, NOT 130×182. |
| ContinueTile.qml | In-image BOOK badge/title/chapter/progress, 46px central resume button, full-cover details click, removable × progress, bottom gold meter. No reader/player/page implementation. |
| WorldTabBar.qml | Three original equal-width Discover / Explore / Library glass buttons, selected gold fill, docking while scrolling. |
| BiblioDiscoverPage.qml / DiscoverBrowser.qml | Underlined Books lens, NOW BROWSING label, original catalogue name + ▾, Apple Books/Open Library source byline, FILTER picker, catalogue wall with 132×198 artwork/title/author and hover/focus source + rating. Original See All → Discover and ‹ Back → Explore. |
| DiscoverSidebar.qml / AddonLogo.qml / BiblioDiscoverApi.js | 52px closed and 240px expanded source rail, WORLD/Biblio heading when expanded, 46px source entries for the **actual Apple Books and Open Library** catalogue owners, gold active mark and 28px round edge toggle. Apple Books uses its bundled local applebooks.ico; Open Library keeps the original honest O letter tile fallback. Four Apple Books categories and two Open Library categories follow source descriptors. |
| DiscoverPicker.qml | Dark translucent 40px picker pill and original rounded floating popup with gold selected option; choice/clear behaviour implemented locally. |
| BiblioExplorePage.qml | Fraunces Explore heading, Customize shelves / Done controls, Top 10 and Popular, Top Rated, New Releases, Trending, Most Read, Classics, followed by Fiction, Nonfiction and Audience image mosaics. |
| RowIndexSidebar.qml | Separate existing 52/240px row index with original star/clock/line SVG paths, expandable edge toggle, and jump-to-row actions. No generic list/drawer replacement. |
| BiblioBookRail.qml | Source 20px Fraunces shelf title + small 13px See All **without any chevron**; 130×182 rounded artwork, 12px one-line title, 11px author, 16px ordinary gap, 6px ordinary leading inset; 26px ranked gap, 30px ranked leading inset, translucent 92px numeral behind cover and no rank rating overlay. |
| ReadingDesk.qml | Separately selected global Home visual: original Biblio glass, heading and Top charts, stacked desktop book shapes, Top 10 copy and four genre chips; compact phone vertical portrait composition. |
| BiblioLibraryPage.qml | Shell-context-only Library tab with representative saved-book wall and original search/filter/sort family; its deeper menu actions and real Collection/Progress state are outside this page study. |

## Local interactions and allowed fitting changes

1. The 130×182 BiblioBookRail does **not** turn into generic mobile cards or a fake two-column grid. Rail art remains physical size and scrolls horizontally; portrait intentionally reveals part of the next item. Titles and authors ellipsize as in QML.
2. Original rails remain 52px collapsed and 240px expanded. A phone expansion overlays the content rather than shrinking a 354px device's remaining area to an unusable sliver; the wide-screen variant moves the content inset. The original edge toggle, source items and row-index entries remain visible and actionable.
3. The original Android shell switches from 72px bottom tabs to the 92px left world rail at 840px. Topbar glyphs are visually unchanged, while the Android hit boxes are widened to 44px.
4. The responsive phone feature uses full-bleed local art, while wide layouts retain an inset poster; Continue keeps its separate 148×222 cover grammar and distinct WidgetHeader. Existing glass, borders, colours and typography are retained.
5. **Short-landscape dropdown exception:** original below-trigger dropdown was tried first at 792×354 and was clipped behind the bottom world bar. The **same** floating dark catalogue/filter menu now moves within the visible area and scrolls its choices. No sheet, redesign, replacement behaviour or new product control. See captures/category-short-landscape.png.
6. The original Explore controls work in the mock only: Customize shelves ↔ Done, original ⠿ drag handle, ▲/▼ reorder, Hide/Show; index expansion and row jumping. See All opens the existing Discover catalogue and ‹ Back returns to Explore. Feature dots and swipes, original source/category/filter choices and Continue remove also work.
7. Read/Details/Resume and account/provider/other-world actions deliberately do not route into invented pages. Their intended action is reported in text **outside** the app frame, without backend calls.
8. The external inspector provides editable width, height, top/bottom insets, 100%/130% font scale, viewport presets, scene/tab selection and jump controls. It is **not** Colosseum UI. Capture mode removes the inspector. Sticky world tabs can naturally overlay content scrolling behind them.

**Behaviour proposals:** none. **New UI exceptions:** none. The short-landscape adjustment is placement/scrolling of the approved popup only.

## Imagery and fixture honesty

Biblio Catalog.js fallback lists remote Apple Books covers; matching Biblio cover files are absent from the frozen local assets. Every displayed image is instead a **real, titled project asset**, not stock or a blank gradient poster. They are **not** the book jackets. The reviewer inspector explicitly identifies all titles, ratings, order, source lines, chapter states and progress as illustrative. No provider, remote image or font CDN is called.

| Illustrative book / author | Actual local project art used (under reference/desktop/assets) |
| --- | --- |
| Dune / Frank Herbert | wallpaper/sagas/dune.jpg |
| The Lord of the Rings / J. R. R. Tolkien | wallpaper/sagas/lord-of-the-rings.jpg |
| Harry Potter and the Deathly Hallows / J. K. Rowling | wallpaper/sagas/harry-potter.jpg |
| A Game of Thrones / George R. R. Martin | wallpaper/sagas/ice-and-fire.jpg |
| The Witcher: The Last Wish / Andrzej Sapkowski | wallpaper/sagas/the-witcher.jpg |
| Percy Jackson and the Olympians: The Lightning Thief / Rick Riordan | wallpaper/sagas/percy-jackson.jpg |
| The Adventures of Sherlock Holmes / Arthur Conan Doyle | wallpaper/sagas/sherlock-holmes.jpg |
| Jurassic Park / Michael Crichton | wallpaper/sagas/jurassic-park.jpg |
| The Way of Kings / Brandon Sanderson | extensions/house/cosmere.jpg |
| Heir to the Empire / Timothy Zahn | extensions/house/starwars-banner.jpg |

Project icon SVGs and Fraunces font assets are referenced in place. Segoe UI copies under frozen reference/local-fonts are referenced for **local rendering only**; no font files are copied or distributed. Extension-generated live rows are not asserted without populated extension data.

## Viewport and capture contract

| PNG capture in captures/ | Provisional logical viewport and state |
| --- | --- |
| portrait.png | 412×900 Android, Explore shelves |
| landscape.png | 900×412 Android wide world rail, Explore shelves |
| desktop.png | 1280×700 desktop shell, Explore shelves |
| portrait-small-discover.png | 354×792 Android, Discover catalogue |
| landscape-small.png | 792×354 Android bottom nav, Explore |
| tablet-portrait.png | 800×1280 Android, Explore |
| tablet-landscape.png | 1280×800 Android wide rail, Explore |
| category-open.png | 412×900, original catalogue dropdown |
| row-index-open.png | 412×900, original 240px row-index expansion |
| customize-shelves.png | 412×900, Customize shelves controls |
| reading-desk-phone.png | 412×900, separate Home Reading Desk |
| category-short-landscape.png | 792×354, corrected bounded catalogue popup |
| filter-short-landscape.png | 792×354, long genre options in the same scrollable popup |
| font130-insets.png | 354×792, 130% text scale with 24px top/bottom safe areas |

**Exact verified render/test command on Laptop 1:** from the assigned output directory, run:

    node captures/qa.cjs

This launches the already installed Brotherhood Desktop Commander Puppeteer package against **an isolated headless Google Chrome instance**. The temporary browser profile lives only under this output's captures folder and is removed after the run. It neither touches shared browser tabs nor uses shared mouse/keyboard. The script saves all above screenshots and captures/QA.json. The BDC headless HTML renderer was also used for initial captures and to discover the short-screen menu defect; because its query-string handling was inconsistent, the final authoritative screenshots and interaction tests are generated directly by the isolated Puppeteer script.

The QA record checks image decoding/missing assets, outer viewport exactness, accidental HTTP(S) requests, body-level horizontal overflow, correct 130×182 book covers, Android navigation at the 840px cutoff, ≥44px Android topbar tap areas, usable short-popup bounds/scrolling, expanded/collapsed row index dimensions (52→240→52), hide/show customization, See All/Back routing, and Apple Books/Open Library source/category and filter selection. Images were inspected, including the difficult 354×792 and 792×354 cases.

## Uncertainty and unverified boundaries

- OnePlus is disconnected. The supplied logical sizes are **provisional**, not measured native device viewport/inset values.
- No QML native runtime, actual Android build, APK, device, desktop app visual regression, backend or library state was exercised. This is not proof that an Android implementation fits.
- The substitute saga/extension artwork is not a book-cover match. Source-only data fixtures must not be represented as genuine Apple Books chart positions or user reading progress.
- Library and real book details/reader remain outside this assigned home/shelf page study; they were not redesigned or implemented.
- A separate pixel-perfect desktop screenshot baseline is unavailable. Styling has been inspected from approved source, not visually validated against a running a40f66be desktop binary.

**Disposition:** Ready for human HTML design review, with the above limitations. No application files, sibling jobs, references, external accounts or publication were changed.
