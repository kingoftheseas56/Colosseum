# Task 18 — Comic details: fidelity ledger

**Status:** Standalone, review-only HTML/CSS/JS phone fitting. This is not a QML implementation or Android runtime validation. Open index.html or any individual page from a local HTTP server rooted at D:\Colosseum-Android-Phone-Mocks-20261101. CSS/JS/art references are relative to that batch root. Reviewer presets and state controls sit **outside the framed app viewport**. No app, reference, repository, sibling job, beta or published content was modified.

## Reference authority

- reference/desktop/qml/ComicSeries.qml — active western-comics release shelf, hero, filter/sort, collected editions/issues, Read and acquisition semantics.
- reference/desktop/qml/ComicSeriesPage.qml — explicitly parked LOCG series route, with the approved ComicDbLedger.qml path. The second mock shows that source design, not a claim that the route is currently live.
- reference/desktop/qml/ComicDbLedger.qml — series hero and synopsis, edition format groups, cover/metadata/description, sources, alternate-source control.
- reference/desktop/qml/ComicArchiveBoard.qml and ComicArchiveIndex.qml, plus GenreMosaic.qml — publisher/franchise taxonomy and series archive index.
- reference/desktop/qml/ComicTorrentSourcesPage.qml and ComicTorrentArchivePicker.qml — original full-screen source page and specialized archive file picker (not mocked).
- reference/desktop/qml/BackAction.qml, Glass.qml, LibraryButton.qml, collections/AddToCollectionsButton.qml, collections/AddToCollectionsPopup.qml, Theme.qml and relevant Main.qml layer routing — shared approved original controls.
- reference/desktop/resources/comics_db.json — real Batman title, five editions, publisher/rank, creator, pages/year, ISBN, descriptions and availability flags used for representative edition identities.
- context/ANDROID-PORT-CLASSIFICATION.md and context/VIEWPORTS.json — constraints and provisional logical viewport contract; reference/alpha/qml was consulted for legacy phone treatment.

MediaDetailScaffold.qml is **absent** from approved desktop and alpha references and only exists in rejected android-drift, so its absence is expected. No drift component or flat-card substitute was used; the original inline QML renderer remained authoritative.

## Original identity retained across all three pages

Deep dark wallpaper and wash, house gold #f0c44a, muted pale ink, original fine borders, softened glass material and source radius, Fraunces display typography, Segoe UI body/controls, approved back chevron/label, actual local SVG window control glyphs (minimize, fullscreen, power), Library ghost and Add to Collections ghost, and existing modal collection picker.

The three original window glyphs remain visible **at phone and desktop widths**. On phone the 22px icons sit inside 44px invisible touch regions; this changes hitbox size only, not identity or the number of controls. Desktop glyph dimensions remain source-sized. Android behavior of these OS-style window verbs remains unverified and is not simulated as an actual OS action.

The desktop's live Glass.qml GPU backdrop shader is represented by translucent CSS with backdrop blur. This preserves material intent but is **not** a pixel-identical shader reproduction. The source reader (owned by Claude), downloads, accounts, windows and provider APIs are never invoked.

## Comic series — comic-series.html

| Approved source component | HTML counterpart | Phone fitting change |
| --- | --- | --- |
| ComicSeries.qml full-screen wallpaper and dark film | .app-wall, .hero-art and wash | Keeps immersive black/glass palette, fixed local wallpaper fixture |
| BackAction and original window glyph row | .chrome, .back-button, .window-controls | Keeps every glyph; phone increases invisible touch target |
| Banner hero, cover 128×190, Western Comics · Tankoban, Fraunces 54 title | .hero-series, .hero-poster, .hero-series-copy | Portrait stacks artwork before full title/facts; landscape/desktop keep row composition |
| Release count, collected count, GetComics | .facts-row | Same inline bright count + dim medium, wrapping permitted |
| Library and Add to Collections | .hero-actions and .collection-popup | Same ghost identity; collection selection is local |
| Underlined Search releases… and clear control | .search-inline and release-filter | Field fills phone width; no new rounded-card search bar |
| Newest, Oldest, A–Z gold sort underline | .sort-controls | Preserves original text/indicator, larger phone hitbox only |
| Single Glass table: COLLECTED EDITIONS and ISSUES | .glass-panel, .release-section-head | Original order, glass panel, section counts/dividers |
| Release cover 58×82, title/status, gold Read button, trailing round acquire utility | .release-row, .release-cover, .read-action, .round-action | Narrow rows stack actions below long title without deleting the icon or verb |
| Read/continue/downloading/queued/empty filter | in-memory releaseDownloads state | Original action intent simulated without backend |
| Pack-mode VOLUMES/EXTRAS branch | reference-only caveat | Representative active GetComics edition/issue branch is shown, not falsely relabeled as a pack |

The series page does **not** invent a synopsis; active ComicSeries.qml has a banner hero, facts and release table. Synopsis belongs to the approved catalogue ledger shown on the edition page.

## Editions — comic-edition.html

| Approved source component | HTML counterpart | Phone fitting change |
| --- | --- | --- |
| ComicSeriesPage.qml DB-ledger route | .edition-page | Keeps original DB format, with parked-route warning here |
| ComicDbLedger series cover 158×236, COMICS eyebrow, Fraunces title | .edition-ledger-hero | Portrait cover above text, landscape adjacent |
| Distinct creators, rank, publisher, editions count, synopsis | .creator-line, .edition-meta, .synopsis | Same hierarchy, natural line wraps; default 3-line synopsis |
| LibraryButton and AddToCollectionsButton | .hero-actions | Original ghost controls and popup |
| Format groups with gold heading, count, fine rule | .edition-group-head | Original format order; categories remain separate |
| Large edition cover 104×156, 21px Fraunces name, author/pages/year, two-line blurb | .edition-row, .edition-poster, .edition-copy | Same visible information, cover retained, title wraps |
| Read/books, download and progress state | .edition-act | Original icon family and availability distinction, state fixture only |
| Round Find alternate sources | .alt-button | Original circular search separate from edition's primary action, only on idle titles |
| Source rail with verified/community rows | .edition-sources in external reviewer sources state | Source example illustrative, not live trust verification |
| ComicTorrentSourcesPage full-screen key-art banner, edition identity, Search, letter-source rows and round gold download glyph | .source-overlay | Preserves the existing full-screen picker, **not a new sheet**; no provider operations |

The five Batman editions are based on actual entries in the local comics_db: Deluxe Detective Comics Book 1 and four Batman trade-paperback volumes. Group order follows ComicDbLedger. The long synopsis is visible via an **external reviewer state**, rather than a new in-app expand control.

## Archives — comic-archive.html

| Approved source component | HTML counterpart | Phone fitting change |
| --- | --- | --- |
| ComicArchiveBoard GetComics Archives hero and back/window controls | .archive-page and .chrome | Original title/eyebrow/subtitle; no new phone shell |
| GenreMosaic image-backed taxonomy, darkened art, count top right, name bottom left | .archive-board-grid and .archive-board-tile | Five columns desktop, three landscape phone, two portrait phone, each still 112px high |
| Board box opens ComicArchiveIndex | in-memory archiveView=index | Original two-step archive flow, same page |
| Index box title, number of series, All N releases › | .archive-index-meta | Exact control role; wraps into additional line as needed |
| Archive covers with title and release count | .archive-posters and .archive-poster | Six desktop columns, fewer on phone; original poster proportion and captions |
| Series open / raw-release open | .archive-poster and .link-button | Local demonstration routes to the Batman series study, not a complete dynamic router |

## Responsive review contract and fixture disclosure

Editable reviewer controls offer 412×900, 354×792, 900×412, 792×354, 1280×700, 800×1280 and 1280×800; numeric viewport width/height, separate top/bottom insets and 1.0×/1.3× font scale. These are **provisional logical pixels**, not measured OnePlus dimensions. The review controls and explanatory notes are outside the phone frame, never promoted to Colosseum UI.

Actual matching comic cover binary files are **not present** in the frozen local assets; the comics_db has remotely hosted cover URLs only, and external network image fetching is prohibited. Every visible substitute is existing project artwork: assets/universes/dcau/portals/gotham.jpg, future-gotham.jpg, metropolis.jpg, watchtower.jpg; assets/universes/dcau/environments/gotham-v11-bg.png and justice-v11-bg.png; assets/universes/marks/dc.png and marvel.png; assets/wallpaper/cold-ripple.jpg. The Gotham Bat-signal is **not** the genuine Batman collected-edition cover. The limitation is declared outside the mock, not inserted as new product copy.

The three individual issue rows, archive counts, selected representative box titles, downloaded/read progress, local collection memberships, source-result titles and trust labels are **illustrative fixtures**. No real catalogue/provider verification or download occurred. The title, creators, edition counts/specs and descriptions on the edition page are from the supplied local comics_db. No fake blank cover slot, stock photograph, external script, or remote provider call is used.

## Behavior exceptions / remaining boundary

**Alternative behavior proposed:** None. The original controls were kept; the UI-level click results are deliberately local state transitions only. The source popup preserves the existing full-screen picker rather than proposing a new mobile sheet. No new UI exception is required.

**Unverified:** physical OnePlus resolution/insets, keyboard and orientation changes, native Qt/QML font rendering or GPU blur, actual mobile window-control semantics, reader handoff, real source availability or trust, downloading, account/library persistence, performance, accessibility screen readers, exact remotely-hosted covers and real app screenshot parity.

**Verified only for standalone HTML:** node syntax check for app.js and render/test scripts; 24 isolated headless browser screenshots for the earlier page composition and widths, image/font availability, script errors and horizontal overflow, plus 14 browser interaction assertions for search/sort, Library/Collections, viewport changes, alternate-source open/close and board/index navigation. See captures/visual-audit.json and captures/interaction-audit.json. Reproduction scripts are node captures/.capture.cjs and node captures/.verify.cjs. BDC render_html returned no PNG, so isolated Puppeteer headless Edge/Chrome was used instead. **Evidence limit:** the last source-preservation edit restored the three original glyphs in the phone chrome after the 24-image audit was taken; subsequent full headless recapture timed out. The original screenshots still evidence earlier layout, but do **not** establish final post-edit visual placement, clipping or touchability of those glyphs. The current HTML/CSS was statically inspected only at that final boundary. No Qt app was built, installed, run or edited.

