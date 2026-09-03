# QML → GPUI feature-parity ledger

Tracks parity between the legacy Qt/QML app (`qml/`, design reference only) and the
GPUI POC (`crates/ui-gpui` + `crates/player` + daemon). One row per top-level surface
that `qml/Main.qml` actually mounts — enumerated from its Loader layers, world
dispatcher, and window-root components, never inferred. It is the work list that
drives the B (breadth) and C (bespoke) waves after Phase A lands.

## Parity definition

- **Same journeys and same look-and-feel tokens** (colors/spacing/fonts —
  `qml/Theme.qml`, `Glass.qml`), **not** pixel-identical output. The visual language
  is a **rebuild** in GPUI with imperative animation, not a port of QML scenes.
- The old app **cannot run from this branch**: `native/` was deleted here (it lives on
  `master`), so `qml/` is consulted by eye as the spec of each surface below.
- Playback parity is bounded by the player contract (`crates/player`): the native
  backend today is macOS AVFoundation/`AVAssetReader` delivering silent BGRA frames.
  **Any surface that needs sound (video audio, watch party, audiobooks, read-along)
  is blocked on the future `player-libmpv` backend** (TODO-124cbd1e, deferred until
  phase-a-ui lands).

## POC baseline (today, on this branch)

| Piece | State |
|---|---|
| daemon routes | `GET /healthz` `GET /readyz`; catalog: `GET /catalog/search` (seeded; live Cinemeta rows w/ `ADDONS_LIVE=1`), `GET /catalog/home`, `GET /catalog/series/{id}`, `GET /catalog/series/{id}/sources`, `GET /catalog/meta/{type}/{tt}` (live-gated); torrent: `GET /sources/imdb/{tt}` (live Torrentio), `POST /torrents/spool` (librqbit sidecar → `file://`); account: `POST /v1/accounts` `POST /v1/sessions` `POST /v1/sessions/refresh`. Genre/source-search/download endpoints still missing |
| `crates/player` | `Player` trait, `native()` → avfoundation; load/play/pause/seek/position/duration/next_frame/event; BGRA `VideoFrame`; **silent, no audio** |
| `crates/ui-gpui` | GPUI 0.2.2 shell: daemon HTTP over gpui_tokio, catalog list, frames → `RenderImage` at a 10 ms pump |
| GPUI media-widget kit | **does not exist** (TODO-2e161921: poster grid/rail/hero/list/dialog) — nothing under `(c)` below is built |
| Rust catalog | SQLite store + LIKE search + home aggregate + series detail; `seed_demo()` deterministic 10 rows — no genre/source surfaces yet |

## Legend

- **(b) Daemon endpoints** — name only what the surface needs. `PRESENT` = live route
  above; anything else is `MISSING` (unserved by the daemon today, and not in the Go
  migration roadmap unless noted).
- **(d) status** — `planned` on every row (nothing GPUI-side exists yet).
- **(e) phase** — **A** = spine wave (shell + Home→grid→detail→player, daemon
  `/catalog/home` + `/catalog/series/{id}`); **B** = breadth (browse/read surfaces
  riding provider-absorbing catalog endpoints); **C** = bespoke surfaces, and **any
  row with sound needs the future player-libmpv backend**.

## Ledger

Status: `planned` on all rows (omitted below).

| Surface | (a) QML reference files | (b) Daemon endpoints it needs | (c) GPUI widgets needed | (e) Phase |
|---|---|---|---|---|
| Boot splash | `Main.qml` boot item → `BootSplash.qml` | `/healthz`, `/readyz` PRESENT (ready gate); first-frame cover warmth: `/catalog/home` PRESENT | fullscreen splash + crossfade (custom) | A |
| Home shell (top bar · wallpaper backdrop · Continue · intro widgets) | `Main.qml` Home tree: `TopBar.qml`, `Theme.qml`, `Glass.qml`, `FeaturedCarousel.qml`/`CarouselSlide.qml`, Continue rail (`ContinueRow.qml`, `ContinueTile.qml`), per-medium intro widgets `Bookshelf.qml`, `TheatreStrip.qml`, `ReadingDesk.qml`, `VaultHomeWidget.qml`; wallpaper still (assets) | home rails: `/catalog/home` PRESENT; account medallion presence: account routes PRESENT; Progress/Collection are client-local | hero, poster rail, tile, glass panel, wallpaper image, rail header | A |
| Continue see-all | `Main.qml` `continueSeeAllLayer` → `ContinueSeeAllPage.qml` | resume backlog is local Progress; cover enrich via `/catalog/search` PRESENT; `/catalog/home` PRESENT | poster grid, list, back header | A |
| Taskbar + session chrome | `Main.qml` window root → `Taskbar.qml`; launch popups `OpenRecentPanel.qml`, `NextToOpenTray.qml`, `FullscreenTransitionShield.qml` | sessions client-side today; resume sync would need `POST /v1/sync/push`·`/pull` MISSING | dock/tray, session tiles, badges, popups | A |
| Theatre world (Discover hub) | worldStack → `TheatreWorld.qml` on `WorldPage.qml` chrome (+`WorldTabBar.qml`, `TrendingTop10.qml`); deep-catalogue see-all pins `TheatreSeeAllPage.qml`; shared grid shells `DiscoverBrowser.qml`/`DiscoverPage.qml` | `/catalog/home` PRESENT (rails) — see-all shelves + genre pins MISSING | hero, poster rail, poster grid, list row, tab bar | A |
| Theatre series detail | `Main.qml` `theatreSeriesLayer` → `TheatreSeries.qml` (+ episode window, `MoreLikeThisRow.qml`, `CastRow.qml`, `LibraryButton.qml`, `EpisodeBrowser.js`) | `/catalog/series/{id}` PRESENT; episode meta + source resolution MISSING (provider-absorbing) | hero + backdrop, poster rail (more-like-this), episode list, source sheet (dialog) | A |
| Video player shell + controls | `Main.qml` `playerLayer` → `PlayerPage.qml` (P1/mpv) or `player2host/Player2Page.qml` (P2, `player2/Player2Shell.qml` + `player2/controls/*`: TransportBar, SeekBar, EpisodeBrowser, SubtitleLayer, StatsOverlay, TrackMenu, OverflowMenu, SourceDrawer, ShortcutsSheet, CloseConfirm, SkipButton, PauseCard, TopBar); `player2host/ColosseumHostServices.qml` | playback URL is local/passed-in; source/torrent resolution + subtitle fetch MISSING; **audio needs player-libmpv (C)** | HUD transport/seek, episode list, subtitle layer, overflow/shortcuts sheets — bespoke; ui-gpui proves only frames→`RenderImage` | A |
| Search (generic + Biblio) | `worldSearchLayer` → `SearchSurface.qml` (Tankoban/Theatre per-source); `searchLayer` → `BiblioSearch.qml` | `/catalog/search` PRESENT — only route a browse surface can use today; per-source search (MAL/AniList/Cinemeta/libgen) MISSING | search field, result grid/list, filter chips | B |
| Tankoban world (manga/comics hub) | worldStack → `TankobanWorld.qml` (Discover/Manga/Comics/Library tabs) on `WorldPage.qml`; `TankobanDiscoverPage.qml`, `TankobanMangaTab.qml`, `TankobanComicsTab.qml`, `TankobanLibraryTab.qml` (+`LibraryPage.qml`, `ComicsDbLoader.qml`, `GenreMosaic.qml`) | per-source catalog (MAL/AniList/ComicsDb/GetComics) MISSING; `/catalog/series/{id}` PRESENT | poster rail/grid, tile, library list | B |
| Manga series detail | `Main.qml` `seriesLayer` → `MangaSeries.qml` (+`MangaSeriesSharedHeader.qml`, `MangaChapterSeriesView.qml`, tankoban volume lanes `MangaTankobanLibrary.qml`, `MangaReadingRoom.qml`; bakeoff mocks `MangaSeriesThumbnailMock.qml`/`MangaReadingRoomThumbnailMock.qml` are dev-only) | `/catalog/series/{id}` PRESENT; edition/source-search profile routes MISSING | hero, chapter/volume list | B |
| Comic series detail + browse | `westernLayer` → `ComicSeries.qml` (GetComics shelf); `comicSeriesLayer` → `ComicSeriesPage.qml` (LOCG catalogue, `ComicDbLedger.qml`); browse: `comicBoardLayer` → `ComicArchiveBoard.qml`, `comicIndexLayer` → `ComicArchiveIndex.qml`, `locgPublisherLayer` → `LocgPublisherPage.qml` | `/catalog/series/{id}` PRESENT; GetComics/LOCG ingest + download routes MISSING | hero, issue list, archive/publisher grids | B |
| Comic reader (+ Vault comic host) | readers mount inside detail pages (`ComicSeries.qml`/`ComicSeriesPage.qml`/`MangaSeries.qml`) via `comicreader/*` (`ComicReaderShell`, `ComicReaderCommandBar`, `ComicReaderHud`, `ComicReaderDouble/Single/StripSurface`, `ComicReaderLoupe`, `ComicReaderPagesOverlay`, `ComicReaderSettingsSheet`, `ComicReaderLayoutPopover`, `ComicReaderPreview`, …); loose-file host `Main.qml` `vaultComicLayer` → `comicreader/VaultComicReader.qml`; manga strip `MangaReader.qml` | none (local CBZ/CBR); content download ingest MISSING at daemon | paged image surface, loupe, HUD, settings sheet — bespoke | C |
| Biblio world (books hub) | worldStack → `BiblioWorld.qml` (Discover/Explore/Library tabs) on `WorldPage.qml`; `BiblioDiscoverPage.qml` (via `DiscoverBrowser.qml`), `BiblioExplorePage.qml`, `BiblioLibraryPage.qml` (+`LibraryPage.qml`, `BiblioBookRail.qml`) | `/catalog/search` PRESENT; Biblio/libgen ingest + download MISSING | book grid, rails, library list | B |
| Biblio book detail | `Main.qml` `bookLayer` → `BiblioBook.qml` | `/catalog/series/{id}` PRESENT (book meta); download MISSING | dust-jacket hero, meta list | B |
| Genre indexes & pages | `genreIndexLayer`→`GenreIndex.qml`, `genreLayer`→`GenrePage.qml` (manga); `theatreGenreIndexLayer`→`TheatreGenreIndex.qml`, `theatreGenreLayer`→`TheatreGenrePage.qml`; `biblioGenreIndexLayer`→`BiblioGenreIndex.qml`, `biblioGenreLayer`→`BiblioGenrePage.qml` — the Biblio pair is Main-mounted but retired from `BiblioWorld` tabs (comment in `BiblioWorld.qml`) | `/catalog/genres` + `/catalog/genre/{slug}` MISSING | index grid, genre rail, mosaic | B |
| Downloads | `Main.qml` `downloadsLayer` → `DownloadsPage.qml` | client-side download manager in the old app; daemon-owned job service MISSING (not on the migration roadmap) | job list w/ progress, badges, confirm dialog | B |
| Vault | `Main.qml` `vaultLayer` → `VaultPage.qml` (browse shelves, `VaultDetailSheet.qml`, `VaultIdentityCeremonyDialog.qml`/`VaultIdentifyDialog.qml`, add-folder); Home widget `VaultHomeWidget.qml`; local-launch glue in `Main.qml` (`dispatchLocalRoute`) | none (local filesystem); local-media library service MISSING if daemon later owns it | shelf grid, detail sheet/dialog, folder dialog | B |
| Extensions | `Main.qml` `extensionsLayer` → `ExtensionsPage.qml` (+`ExtensionsSources.qml`) | addon store/browse/install was C++ `Extensions` store; daemon proxy MISSING | list, detail, install dialog, toggles | B |
| Utility pages — Settings · KeyboardGuide · Update | `settingsLayer`→`SettingsPage.qml`, `keyboardGuideLayer`→`KeyboardGuidePage.qml`, `updateLayer`→`UpdatePage.qml` (+`update/UpdateLivingGallery.qml`) | none (local prefs/keys); update-feed route MISSING if the daemon serves the release chronicle | form fields/toggles, shortcut list, dialog | B |
| Universe hall + universe pages | `universeHallLayer` → `UniverseHallPage.qml`; `universeLayer` → per-extension: `DCAUUniversePage.qml`, `GalaxyUniversePage.qml` (Star Wars), `OnePieceUniversePage.qml`, generic `UniverseExtensionPage.qml`; bespoke scenery + media rails (`DCAUWorldPage.qml`/`DCAUTheatreCard.qml`/`StarWarsMediaShelf.qml`/`OnePieceArcCatalogue.qml`/`EraUniversePage.qml`/`SagaUniversePage.qml`/…) | universe/extension payload feed MISSING | editorial layouts, poster rails, portal tiles — mostly bespoke | B |
| Wallpapers | `wallpaperLayer` → `WallpaperSearch.qml` picker; living scenes `wallpapers/{NoirFlow,AuroraFlow,MeshTwilight,MeshEmber,MeshMint,LowPoly}.qml` (the six `nativeWallpaperFile`-registered ids) under the persistent backdrop | none for local picks; remote wallpaper feed MISSING if ever needed | animated scene canvas + motion scenes — bespoke (GPUI imperative animation, no shaders/QML) | C |
| Watch party | `Main.qml` `watchPartyJoinSheet` → `WatchPartyJoinSheet.qml`; in-player panel `WatchPartyPanel.qml` (inside `PlayerPage.qml`); room glue `Main.qml`/`Taskbar.qml` | room create/join/state sync MISSING (was the native `WatchPartyUi` service) | join sheet, presence panel; **live AV → player-libmpv (sound)** | C |
| Book reader + read-along audiobooks | `Main.qml` `bookReaderLayer` → `reader2/ReaderShell.qml` (+`reader2/*` chrome: ReaderChrome, TopBar, AppearancePanel, LeftPanel, SearchSheet, SelectionMenu, DictCard, FootnoteCard, Paper, RulerOverlay); shared audio engine `AudiobookSession.qml` at window root (+`AudioMenu.qml`, `reader2/AudioGlyph.qml`) | none (local epub/mp3); audiobook/download ingest MISSING; **audio → player-libmpv** | paged-text surface, selection menu, side panels; audio transport — bespoke | C |
| Account + profile surfaces | window-root `AccountCenter.qml`, `AccountFlyout.qml`, `AccountOnboardingHost.qml` driving `account/*` pages (Welcome, Create, SignIn, Recovery/RecoveryKey/RecoveryPage, Devices/DeviceApproval, Security, Profile, DataPrivacy, PendingSyncSignOut, YourColosseum, Onboarding, Choice) | `POST /v1/accounts`, `POST /v1/sessions`, `POST /v1/sessions/refresh` PRESENT; everything else MISSING — profile/username, password change, recovery, devices, challenges/approvals, avatar, sync (rust-poc migration slices 1–7) | form fields, dialogs, device/approval lists — sign-in/create subset is the A-shell identity gate | B |

## Dropped or merged (and why)

These QML files exist but are **not** mounted by `Main.qml` or any reachable surface,
so they get no row; they are visual dead ends, not parity targets:

- `CalendarPage.qml` + `CalendarApi.js` — zero references outside themselves (a
  Theatre "Coming-up" calendar spec that never landed a door).
- `PersonalizePage.qml` — parked personalization gallery; the live picker is the
  Wallpaper row's `WallpaperSearch.qml` (`Main.qml` notes "swap from the parked
  gallery later").
- `MangaCatalogPage.qml` — no instantiators.
- Dev-only / harness / placeholder files: `_*check.qml` design checks,
  `BakeoffStripHost.qml` (blind-trial harness), `DemoWorld.qml` (unbuilt-medium
  fallback in `worldSourceFor`), `ComicsDbLoader.qml` (silent catalog ingest,
  referenced only as plumbing by the Comic rows), and the dev thumbnail-mock lane
  behind `legacyWeebCentral` (Main's `seriesLayer` mock source):
  `MangaSeriesThumbnailMock.qml` → `MangaReadingRoomThumbnailMock.qml` →
  `MangaTankobanLibraryThumbnailMock.qml`.
- `wallpapers/MeshGradient.qml` + `wallpapers/GildedRainHeavy.qml` — scene files not
  in `Main.nativeWallpaperFile`'s registry (an unknown id falls back to the default
  still), so they have no mount path.
- Genre rows merged: the manga/Theatre/Biblio index+page layers share one family and
  one endpoint shape (`/catalog/genres`, `/catalog/genre/{slug}`).
- Utility pages merged: Settings, KeyboardGuide, Update are three taskbar full-pages
  with no catalog dependency — one row.
- `CastRow.qml` is the **cast/credits strip inside `TheatreSeries.qml`**, not a live-TV
  surface; it lives in the Theatre detail row.
- Biblio genre layers (`BiblioGenreIndex/Page`) remain rows (Main still mounts them)
  even though `BiblioWorld.qml` retired them from its tabs.

## Driving B and C from this ledger

After Phase A, breadth (B) is ordered by what the daemon can absorb from each
provider (per-source search/catalog, genre, library, downloads — see TODO.md queue),
and each C row that touches audio waits on `player-libmpv`. Bespoke-layout rows
(comic reader, wallpapers, watch party, book reader) also define the GPUI widget kit
gaps the A0 `ui-widgets` crate (TODO-2e161921) does not cover.
