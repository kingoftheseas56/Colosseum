# Video player demo | Colosseum v1.1.8

## Loop
A 16-second still-backdrop player sequence begins without chrome, then fades in the title bar, pause information card, timeline, volume and transport controls while the preview thumbnail moves over multiple chapter marks. The subtitle selector opens and closes before the chrome disappears, leaving the gently drifting backdrop.

## Measured from the v1.1.8 Git tag
- `qml/player2/Player2Shell.qml`; `qml/player2/controls/TopBar.qml`, `TransportBar.qml`, `SeekBar.qml`, `PauseCard.qml`, `TrackMenu.qml`, `Player2Icon.qml` (geometry, font, fades, transport and popover).
- `qml/PlayerPage.qml` (shipped-player 216px thumbnail and chapter label/ticks, not present in Player 2's timestamp-only scrubber); `qml/SubtitleMenu.qml` (subtitle presentation context).
- `qml/Theme.qml`, `qml/Glass.qml`, `qml/TopBar.qml`, `qml/TitlePageBar.qml`, `qml/CatalogueVisualMetrics.js`, `qml/Catalog.js`; checked Arc54 web `styles/tokens.css`, `shell.css`, `components.css`. QML wins when values differ.

## Staged values and educated approximations
- **Dune: Part Two** is a public title in v1.1.8 `Catalog.js`; its canonical backdrop is `https://live.metahub.space/background/large/tt15239678/img`. It timed out from Laptop 1, so the rendered image is a verified same-title TMDB backdrop (`https://image.tmdb.org/t/p/w1280/uhUO7vQQKvCTfQWubOt5MAKokbL.jpg`), used for the thumbnail too. Both images have colored fallbacks.
- The 166-minute duration, simulated elapsed/finish time, clock fixed at 1:41 PM, initial playhead, 83% volume, 87% cache, synthetic chapter boundaries/names, paused interval, subtitle track names/selection and sync are *demonstration data*, not live session facts.
- The thumbnail is **backdrop artwork, not a film frame**, intentionally; no film-frame extraction, real subtitles, audio, codec probing, or provider identity. The mock quality line (1080p · H.264 · AAC · 5.1), icon availability, plot, and on-screen control timing are staged.
- Inline SVG strokes reproduce the Lucide glyph shapes because the website's `../assets/icons/` folder lacks the player-specific icons. A small amount of spacing in the thumb and iconography is visually inferred; major dimensions are measured from QML.

## Browser verification
- Inspected `?t=0` and `?t=7500` in Chrome **with CSS viewport exactly 1280×720**; captured `video-player-t0.png` and `video-player-mid.png` at 1280×720. Also inspected `?t=10800` for the open subtitle menu.
- Browser runtime assertions: viewport, document scroll size and stage all 1280×720; no overflow; both backdrop and thumbnail load completely at intrinsic 1280×720; no JavaScript or browser-console errors. Visual inspection: first frame clean, middle frame shows readable card, timeline, hover preview and correctly contrasted center play glyph; subtitle menu is legible.

## Could not do
- Could not load the exact `Catalog.js` Metahub artwork URL from Laptop 1 (request timeout), so the fallback above is disclosed; the Colosseum app was deliberately not launched, as instructed.
