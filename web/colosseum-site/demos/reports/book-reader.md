# Book reader demo | Colosseum 1.1.8

## Loop
The paper-themed opening of Jane Austen's *Pride and Prejudice* rests beneath the reader's icon-only top bar and gold page rail, then turns to its next page. The right-hand Appearance panel slides in; the page changes from Paper to Night and from the Literata stand-in to Fraunces, then the panel closes and a short concealed reset completes the 17.6-second loop.

## Measured source
- Git tag `v1.1.8`: `qml/reader2/ReaderShell.qml`, `ReaderChrome.qml`, `TopBar.qml`, `BottomRail.qml`, `AppearancePanel.qml`, `Theme.qml`, `Paper.qml` and `Reader2Logic.js`. Used their 348px panel, 64px bar, 22px rail, 28px inset, 18px meta, reader palette, labels, card/sliders/spacing, 300/320ms reveal semantics, and 100%/1.6/72 defaults.
- Reviewed shared `qml/Theme.qml`, `Glass.qml`, `TopBar.qml`, `TitlePageBar.qml`, `CatalogueVisualMetrics.js`, and searched `Catalog.js`; also `resources/reader2/paper.html`, the ratified `agents/colosseum-book-reader-chrome-mock.html`, the site's `world.html`, `assets/reader.webp`, and Arc 54 CSS reference. Reader2 QML takes precedence over shared/world styling.

## Staged or estimated
- Public-domain Austen prose and chapter heading "Chapter One"; not a loaded EPUB, a personal library, or a real saved session. The exact paragraph split between two demonstrated pages is staged.
- The 8-page chapter indicator, 0%/1% book progress and sixty equally spaced chapter tick marks are illustration data, not EPUB-derived locators. The first theme is Paper although Reader2's stored default is Night; this is deliberate to make the theme change visible.
- The site's approved assets do not include Literata or Inter, so Georgia stands in for Literata in the page and cards, and Segoe UI for Inter controls; Fraunces comes from the supplied local TTF. The page-turn interpolation, visual reset, and timed panel activations are staged and not claimed as Foliate's exact native page-turn animation.
- Icons missing from the website's asset folder (back, contents, appearance, bookmark, close and chevrons) use approximate inline strokes; the three present matching icons load from `../assets/icons/` with fallback strokes. All image layers have coloured backing/fallback.
- The appearance panel covers the paper instead of resizing it, as in the QML. Its controls are display-only, and the covered top-bar chapter caption is faded while the panel is open to avoid clipped lettering.

## Browser checks
- Opened the local HTML in headless Google Chrome through BDC/Puppeteer at exactly 1280x720, device scale factor 1. Captured `book-reader-t0.png` at `?t=0` and `book-reader-mid.png` at `?t=10500`.
- Both snapshots have 1280x720 document/viewport dimensions, no overflow, loaded local Fraunces, three of three loaded icons, and **zero JavaScript/console errors**. Page one measures 530px of content and page two 432px, each clipped only by the intentional native-style reader surface/panel.
- Also froze and inspected `?t=3500`, `7500`, `12500`, and `15000`: the turn, open panel, Night theme, Fraunces typeface, and closure appeared in order. Reduced-motion without a query stayed on frame zero; after fixing a tick-name collision, the ordinary autoplay advanced page one offscreen by 4.25 seconds with zero errors.

## Could not do
- Did not run or modify Colosseum, as prohibited. Exact native QML/WebEngine visual parity and Foliate pagination could not be runtime-compared; the screenshot and tag-specific source are the reference.
