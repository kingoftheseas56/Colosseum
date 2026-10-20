# Manga series (Tankoban) | Colosseum 1.1.8

## What the loop shows
One Piece rests on its art-backed title, author/status/year/score/genres, Read and Library actions, synopsis and series facts, then glides down into the Tankoban volume flow. The gold centre focus advances from Volume 1 through Volumes 2, 3 and 4 before the page returns to the header, repeating every 16 seconds.

## 1.1.8 source read and measured
Read the tagged QML for `MangaSeries.qml`, `MangaSeriesSharedHeader.qml`, `TankobanSeriesView.qml`, `MangaChapterSeriesView.qml`, `MangaReadingRoom.qml`, `MangaTankobanLibrary.qml`, `OnePieceVolumeCard.qml`, `Theme.qml`, `Glass.qml`, `TopBar.qml`, `TitlePageBar.qml`, plus `CatalogueVisualMetrics.js` and `Catalog.js`; checked `Main.qml`'s title-page selection. Measured 54px margin, Segoe UI/Fraunces, gold/ink/glass tokens, 58px legacy top chrome, 258×36 mode selector, and the volume flow's 44px header, 52px action strip, cover 2:3 ratio, 6px radius, 18px gap, 2px gold focus outline and 1.10 centre scale. Also reviewed the supplied `assets/tankoban.webp` and Arc54 HTML/CSS translation.

## Staged, inferred, or intentionally different
- **Header parity limit:** v1.1.8 `Main.qml` defaults `rebuiltTitlePages=false`, choosing the compact 263px `MangaReadingRoom` / `MangaSeriesSharedHeader`; the opt-in `TankobanSeriesView` also has a different poster-led masthead. This demo's taller 360px header plus 278px separate synopsis/facts block is derived from the supplied Tankoban screenshot and the requested scroll sequence; it is *not* pixel-identical to either tagged variant.
- Screenshot-style text CTA buttons (Read and Library) and full synopsis/facts composition, rather than the compact default's library icon and volume-flow primary Read action, are staged to match the assignment's visual request.
- One Piece banner and main catalogue cover addresses are copied from `v1.1.8:qml/Catalog.js`. The nine additional cover addresses are public Open Library editions; volume titles, score presentation, 115-volume count, chapter count and one-device-volume state are staged from the screenshot or for continuity, not real user data. Edition artwork is illustrative and may differ.
- The 16-second scroll/focus timings, 490px scripted travel, 64px large title, 250px flow covers and loopable 10-item sample are presentation geometry. Actual QML flow clamps cover height to viewport space (maximum 276px) and owns real per-volume states. No source/provider branding appears.

## Browser verification
Opened `manga-series.html?t=0` and `?t=10000` in Chrome/Puppeteer at **exactly 1280×720, device scale 1** and saved both requested PNGs. At both positions all **14 of 14 image elements** decoded to nontrivial dimensions, Fraunces registered, document width/height stayed 1280×720 with no overflow, and the gold focus was respectively **VOL 1** and **VOL 3**; header, facts, cover art and text were visibly readable. A separate browser run reported **zero page/console errors** and verified reduced-motion freezes the first frame; an earlier invalid 1×1 Open Library cover was replaced and the final image check passed.

## Not done
Did not launch Colosseum or assert rendered QML runtime pixel-parity, as prohibited. No index, world demo, source repository files, commits or deployment were touched.
