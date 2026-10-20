# Comic and manga reader recreation (Colosseum 1.1.8)

## Loop
The 17-second loop shows a right-to-left paired-page spread, turns two page units, reveals the flat command bar and anchored layout menu, then selects Long strip.
The seamless strip scrolls, the Pages thumbnail filmstrip opens above the gold progress rail and closes, and the paired layout returns.

## Source measured
Read at tag `v1.1.8` (`15fd79af6331f2b4726ab2350a69a0c896cdbecd`): `qml/comicreader/ComicReaderShell.qml`, `ComicReaderHud.qml`, `ComicReaderCommandBar.qml`, `ComicReaderDoubleSurface.qml`, `ComicReaderStripSurface.qml`, `ComicReaderPagesOverlay.qml`, `ComicReaderLayoutPopover.qml`, `ComicReaderIcon.qml`, and shared `qml/Theme.qml`, `Glass.qml`, `TopBar.qml`, `TitlePageBar.qml`, `CatalogueVisualMetrics.js`, `Catalog.js`. Also read the accepted `demos/world.html` and Arc 54 `tokens.css`, `shell.css`, `components.css`.
Measured presentation: black full-bleed page surface; 38px title strip, 46px flat command strip and 54px gold progress rail; six commands right-aligned with 26px gap and margin; anchored layout panel 322px wide at y92; Pages band 229px high at y437, with 173px-high centre thumbnail and 12px spacing.

## Staged or guessed
- `One Piece` is a public title in tagged `Catalog.js`; `Chapter 1090`, 176 pages, reading positions, manga RTL order, and the sample bookmark are staged and are not a user's library.
- Comic pages and thumbnails are original neutral CSS grey diagrams, not reproduced artwork. Their proportions, textures and panel arrangements are representative placeholders.
- All scripted motion and timing, pair-turn frames, strip scroll distance, active read position and menu dwell time are staged for the 17-second demonstration; there is no backend or real document.
- Inline SVG icon geometry approximates the tagged `ComicReaderIcon` vocabulary because only the general site icons were available; no external glyph resources are requested.
- The site wallpaper uses a colour fallback behind its asset; the reader itself correctly paints an opaque black canvas.

## Browser checks
Tested local HTML in Edge/Puppeteer at precisely 1280×720. Saved `comic-reader-t0.png` from `?t=0` (paired spread, HUD and rail) and `comic-reader-mid.png` from `?t=9000` (Long strip and HUD); viewed both. At both checkpoints viewport and document scroll dimensions remained 1280×720, with no console/page errors. Separately inspected `?t=6200` (layout panel 322px wide at y92) and `?t=13800` (Pages filmstrip from y437 to y666); both rendered legibly with no reported errors. The wallpaper loaded (2560×1600) and the Fraunces font loaded.

## Could not do
Did not launch the Colosseum app (explicitly prohibited). No actual Qt page decoding, pairing engine, or user interaction was exercised because this is an intentionally inert HTML recreation.
