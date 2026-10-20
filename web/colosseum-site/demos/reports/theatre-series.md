# Theatre series page, Colosseum 1.1.8

## Loop
The Bear rests on its full-bleed series header, metadata, Watch/In Library/Notifications actions, synopsis, and facts before gliding down to the season and episode ledger. Over 17 seconds it switches once from Season 2 to Season 3, highlights three episode rows in gold (E1, E2, E3), and glides back to the header.

## Source measured
- Tagged `v1.1.8:qml/TheatreSeries.qml` is the default (confirmed in `Main.qml`: `rebuiltTitlePages: false`); `TheatreSeriesView.qml` is the opt-in replacement, **not** the layout chosen here.
- Read `Theme.qml`, `Glass.qml`, `TopBar.qml`, `TitlePageBar.qml`, `LibraryButton.qml`, `BackAction.qml`, `TheatreFacts.js`, `EpisodeBrowser.js`, `CatalogueVisualMetrics.js`, and `Catalog.js`. Cross-checked the older `web/developer-colosseum/surfaces/detail-theatre/` CSS translation and the supplied `assets/theatre.webp` screenshot.
- Original dimensions retained include a 360px hero, 54px margins, 64px Fraunces title, 42px action pills with 11px radii, 580px synopsis, 56px fact gap, 44px season tabs, 86px ledger, 30px column header, 104px episode rows, and 148px Next Up row.

## Staged and approximated
- **Staged title:** The Bear, a well-known series featured in tagged `Catalog.js`. Its banner address is exactly that catalogue entry; title metadata, synopsis, episode names, descriptions, still URLs and season counts were checked against the public Cinemeta feed. Unlike the supplied Silo screenshot, this uses The Bear deliberately.
- **Staged account/progress state:** In Library, Notifications on, no episodes watched or partially watched, and E1 treated as Next Up. No real person's library or history is used.
- **Staged motion:** Season 2 to 3 selection, three keyboard-style focus states, static ten-episode lists, 2023/2024 US-style episode dates, and the watch CTA tracking E1. The other season tabs are scenery, not interactive controls.
- **Rendering approximations:** QML backdrop shader and full-page wash are represented by layered CSS gradients; static scrollbar thumb/offset, pinned chrome scrim, small SVG play/download glyphs, and fallback colors substitute for QML-specific renderers. Pixel values without an explicit tagged equivalent (scrollbar geometry, a few line heights, text ellipsis, focus-outline strength) were kept restrained.
- All actions are inert presentation. No streaming/download provider names, user details, credentials, frameworks, scripts, or other files were introduced.

## Browser checks
- Opened `theatre-series.html?t=0` in headless Chrome with viewport **1280x720**, device scale 1: document measured **1280x720**, all **14/14** HTML images loaded including backdrop/stills/icons, and **no console or page errors**. Screenshot: `theatre-series-t0.png`. Explicit image decoding before capture ensured the large art was fully painted.
- Opened `?t=9000` in the same **1280x720** viewport: document measured **1280x720**, **14/14** images loaded, **no console or page errors**; Season 3 was active, E2 "Next" had gold focus, the season bar parked at **y=72**, and the first Next Up episode began at **y=232**, below the chrome. Screenshot: `theatre-series-mid.png`.
- Tested `prefers-reduced-motion: reduce` without `?t`: remains at first-frame Season 2, hero top at 0, no active episode focus. Visual review found readable text and no stage overflow at either freeze point.

## Not done
- The native Colosseum app was not launched, per instruction; this is source-measured HTML rather than a live runtime parity test. Remote artwork requires network access at playback, with dark colored fallbacks if a request fails.
