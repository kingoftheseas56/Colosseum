# Biblio book page: Colosseum 1.1.8

## Loop
The 16-second non-interactive loop rests on the physical cover of **Theo of Golden**, Allen Levi's name in the genre eyebrow, the serif title and description, plus Read and Library. It glides into the Editions and Audiobook shelves, highlights EPUB, PDF, AZW3, then the first audiobook row, and returns smoothly to the top.

## Source measurements
- Read `demos/world.html` in full first and used its fixed-stage, RAF, `?t=`, reduced-motion, and IntersectionObserver contracts.
- Inspected tagged `v1.1.8:qml/BiblioBook.qml`: bar x54/y22/1172x64/r16; viewport y108; content x54; cover 268x402 with 16px inset, page edge and spine; 64px column gap; title 54px Fraunces; synopsis 17px/1.7, max 640; action buttons 268x50/r13; source panels 640px/r14 and rows 52px.
- Inspected tagged `Theme.qml`, `Glass.qml`, `LibraryButton.qml`, `BiblioBookRail.qml`, `Bookshelf.qml`, `TopBar.qml`, `TitlePageBar.qml`, `CatalogueVisualMetrics.js`, `Catalog.js`; checked Arc 54 web `styles/tokens.css`, `styles/shell.css`, `styles/components.css`, `surfaces/detail-book/surface.css` and `surface.js` as secondary translations.
- Used the supplied `assets/biblio.webp` screenshot to confirm the book-detail composition, solid Biblio background, header, oversized cover and raised initial.
- Book cover URL and fallback color are the exact **Theo of Golden** entries in `v1.1.8:qml/Catalog.js` (`biblioTop`); page font and wallpaper are the site's own relative assets.

## Staged or approximated
- Book synopsis is concise **paraphrased staged catalogue copy**, not live BiblioApi output. The title, author and 2025 edition year were checked against Apple's ebook catalogue; the genre eyebrow composition is staged.
- Tagline is omitted because the chosen catalogue entry has no tagline field, as in the provided screenshot's empty-tagline layout.
- Three ebook rows and two audiobook rows, format candidates, counts, file sizes, English metadata, relative order, preferred EPUB, downward glyphs and focus path are **illustrative**, not current acquisition results. The Read status is also staged. No real user library or progress appears.
- The live source inventory (which precedes Editions in this version's QML) is deliberately excluded to avoid third-party source, indexer and release-group names. Editions and Audiobook remain in the correct order.
- Header blur/gray luminosity is an approximation of QML Glass sampling the offscreen wallpaper backdrop; the independent HTML scrollbar thumb uses derived geometry rather than a native HouseScrollBar.

## Browser verification
- Ran the actual file in **headless Google Chrome** with its viewport fixed to **1280×720**, device-scale factor 1 and local-file access. Captured `book-page-t0.png` from `?t=0` and `book-page-mid.png` from `?t=8000`.
- Both views measured `window.innerWidth/innerHeight = 1280×720` and document scroll size `1280×720`, so nothing escaped the stage. The cover loaded from the catalogue address with a natural size of 395×600, and Fraunces was reported available.
- At t0 the cover was positioned at (54,124) sized 268×402, with title, text and both buttons readable. At t8000 the content had glided 346px, Editions and Audiobook were fully visible, and the **PDF** row showed the active gold focus.
- No browser console/page errors and no failed resource requests were observed in either freeze frame. Local image assets have coloured fallbacks and the stage intentionally clips scrolled content.

## Not performed
- Did not launch Colosseum, perform any live provider lookup/download, or change its repository. The HTML demo is intentionally non-interactive.
