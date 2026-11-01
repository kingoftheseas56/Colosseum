# Theatre film/episode source picker — fidelity dossier

Task: 01-theatre-sources. Scope: standalone local HTML/CSS/JS mock, not Colosseum implementation.
Authority: approved desktop checkout at a40f66be06f22cdbe75e97e7dc4f1108ab2f5d41.
Pages: film-sources.html and episode-sources.html. All work is in outputs/01-theatre-sources.

## Source hierarchy

- reference/desktop/qml/SourcesSheet.qml is authoritative: it contains both the original desktop row renderer and inline phone renderer, provider selector, filters and actions.
- reference/desktop/qml/Theme.qml sets house gold #f0c44a, ink #f7f7f5, inkDim #c9c8d0, inkDimmer #9a99a5, edge rgba(1,1,1,.18), Segoe UI, Fraunces and 54px desktop margin.
- reference/desktop/qml/Glass.qml defines 18px frosted/glass material; the mock approximates it with CSS blur/tint and edge rather than claiming Qt shader pixel parity.
- reference/desktop/qml/BackAction.qml gives the genuine round-joined chevron and plain Back label, not a generic arrow icon.
- reference/desktop/qml/TheatreSeriesView.qml hosts SourcesSheet over the whole Theatre detail and calls sources.show for movie, series, episode download and season selections. It supplies the title, year/genre or episode metaLine, and a title backdrop. The approved Watch action is the only minimal parent detail element reproduced in the closed view.
- reference/desktop/qml/Main.qml establishes the persistent wallpaper/glass shell; the open sheet itself covers the host detail.
- reference/alpha/qml is recovery only. SourcePickerList.qml and SourceCard.qml are ABSENT from approved desktop and alpha; their rejected counterparts occur only in reference/android-drift/qml. No flat-card design is borrowed from those files.
- context/ANDROID-PORT-CLASSIFICATION.md demands restoration of the original source rows, unknown-seeder correctness and phone reach via min(width,height)<600. context/VIEWPORTS.json identifies dimensions/insets as provisional.

## Source-to-HTML component map

| Approved QML control / content | Mock counterpart | Original design retained |
|---|---|---|
| Fullscreen black overlay + title banner with bottom fading scrim | .sources + .hero | Title art, dark gradient 0.25/0.5/0.92, no invented OS chrome. |
| BackAction plain vector chevron + label | #backAction | Original stroke grammar, label, 44px hit region and close action. |
| Title metaLine gold above Fraunces title | #heroMeta + #heroTitle | Desktop 12px tracked eyebrow and 56px display; phone 11px and 30px. |
| topBar provider with 2×2 grid badge, label and chevron | #providerTrigger | 64px desktop / 52px portrait / 44px landscape bar, same four-square glyph, glass border and selected-gold text. |
| Original dropdown popup (not a sheet) | #providerMenu | #141414 popup, 52px rows, each provider with filtered count, gold selection, keyboard navigation, clamped to viewport. |
| Flow quality pills + counts | #quality | All, 4K, 1080p, 720p, SD; gold active fill/dark lettering; provider-based count and suppression of absent qualities. |
| Glass source table and heading | .source-table, .table-head | 18px radius, white-alpha edge, source count, via-provider/extension summary and internal list scrolling. |
| Provider letter tile | .provider-tile | Fraunces initial in bordered translucent 54px desktop / 44px phone square; not a brand logo or generic card. |
| Provider heading + gold qualityLine | .provider-quality | Segoe name, gold 13px quality with original weight and letter spacing. |
| Bounded release filename | .row-release | Single ellipsized line desktop; up to two word-wrapped/anywhere lines phone, with full local accessible name/tooltip. |
| Separate seeders, size and sourceName metadata | .row-meta .meta-piece | Dot-separated visible values; missing and -1 seeders are omitted rather than printed or converted to zero; no emoji. |
| Audio + language chips | .audio-langs, .lang-chip | Original text hierarchy, outlined round-corner chips and wrapping for many languages. |
| Format/type chips | .tags, .tag-chip | Transparent/subdued glass, 6px rounded edges and original small letter spacing. |
| Stream kind + stream label | .stream-line | Torrent/Direct and P2P/HTTP stream information preserved. |
| Gold round Play/down-arrow action | .circle.play | 56×56 desktop / 56×48 phone gold action with ▶ or ↓, not text button. |
| Round copy link + momentary tick | [data-act=copy] | ⧉ to ✓ for 1.2s, same separate verb; visual only, no clipboard call. |
| Round per-title Download + queued tick | [data-act=download] | Separate ↓/✓ circle in Play mode; one queued title flag shared across rows. |
| Whole-row activation | .source-row | Click/Enter/Space to activate selected source, without copy/download clicks falling through. |
| Keyboard/right-click context popup | #contextMenu | Play/Choose source, Copy link, optionally Download; dark centered 210px menu. |
| mode=download + mode=season | Outside-frame mode input controls original in-frame title/row actions | Choose-source closes on pick; full-season torrents only in season, download glyph for choosing, no redundant per-row download. |

## Layout changes allowed by fitting

**Portrait 412×900 and 354×792:** 200px hero, 16px margins, original tile-led first line, full-width bounded release/metadata/chips below, bottom-right round actions, 70px two-line glass-table heading and vertically scrollable rows. No data blocks are removed.

**Landscape 900×412 and 792×354:** fitting the unmodified 200px phone banner leaves too little vertical space. The hero compresses to 96px, or 82px at heights below 385px. Back remains at the upper left; the same gold metaLine/title relocates to the right of it. Gaps shrink, provider bar remains the same selector, visible pill height fits landscape, and the source table retains its count/header and scroll. The same circular actions move into a 174px right-hand row column, ensuring all three sit on one line and are visible next to the provider title. Remaining row content wraps below and is reachable by table scrolling. This is reflow, not a novel UI or interaction.

**Desktop 1280×700:** preserves 300px banner, 54px side margin, 64px provider bar, 40px quality pills, 52px table header and 150px min-height horizontal source rows. Tablet 800×1280 and 1280×800 intentionally use this nonphone branch; a tablet is not incorrectly classified as a phone by width under 840.

**Hit areas, font and safe area:** the output references local Segoe UI copies and bundled Fraunces via CSS @font-face; it does not copy/distribute fonts. Phone controls use 44px or larger hit regions; 34px-high landscape pill appearance has a hidden ±5px clickable extension. Editable logical dimensions, all four insets and font scale 1.0/1.3 appear OUTSIDE the app viewport; zero and illustrative 24px top/bottom cases are available. No fake status bar, bottom system navigation or keyboard is rendered.

## Data and art declarations

- Film: Star Wars: A New Hope; reference/desktop/assets/extensions/house/starwars-banner.jpg is actual local Colosseum franchise art, NOT verified film key art from the running app.
- Episode: The Mandalorian, S1E1 — Chapter 1: The Mandalorian; reference/desktop/assets/universes/star-wars/nevarro.jpg is actual local project Nevarro artwork, NOT verified per-episode key art.
- Faint background: reference/desktop/assets/wallpaper/captured-motion.jpg, actual local project wallpaper.
- All stream release names, sizes, seeds, source/provider attributions, audio labels and counts are ILLUSTRATIVE populated fixtures, not provider results or availability claims. Torrentio, NoTorrent, MediaFusion and Comet are known provider names, but these specific rows are invented examples for visual stress testing.
- Includes long release titles, 6 languages in one row, multiple tags, negative/unknown seed counts suppressed in presentation, direct HTTP rows, all four qualities and 4 season packs.
- The closed state reveals a MINIMAL Theatre parent anchor (real local art + title + approved Watch action), not a replacement design or full reconstruction of TheatreSeriesView.

## Interaction review and proposed exceptions

Source sheet, original dropdown (open/closed/provider choice), quality pills, play/download/season mode, row activation, copy and queued ticks, keyboard/right-click context menu, Back and reopen are locally interactive. Real clipboard/provider/player/download operations are deliberately absent. No original behaviour was replaced by an alternative; behaviourProposals = [] and NEW UI exceptions = [].

Query parameters for repeatable state capture include render=1, w/h, menu=1, context=1, closed=1, mode=download/season, quality=1080p, provider=NoTorrent, scale=1.3 and insetTop=24. The review controls stay OUTSIDE the app viewport and are suppressed in render-only captures.

## Verification boundaries

Only the standalone HTML and headless browser geometry/interactions can be evaluated here. This is NOT an implementation, compilation or runtime verification of Colosseum/Qt/Android. Qt MultiEffect glass, live QML font registration, native pointer handling, real source data, physical device DPI, exact viewport/insets, keyboard/IME, player handoff, stream operations and publication state remain outside this task.

A local headless capture helper (capture.js) starts a private Chrome profile and ephemeral HTTP server rooted at the provided batch root, writes PNGs and a measurements log under captures/, and cleans its profile. If render/proof remains incomplete, REPORT.json will identify the exact missing evidence rather than claim success.

