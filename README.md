<p align="center">
  <img src="assets/icons/colosseum.svg" alt="Colosseum" width="96" />
</p>

<h1 align="center">Colosseum</h1>

<p align="center">
  <strong>A native desktop media environment for manga and comics, books and audiobooks, movies, shows, and anime.</strong>
</p>

<p align="center">
  <a href="https://github.com/kingoftheseas56/Colosseum/releases"><img src="https://img.shields.io/badge/Windows-10%2F11-111111?style=flat-square" alt="Windows 10/11" /></a>
  <a href="https://github.com/kingoftheseas56/Colosseum/releases"><img src="https://img.shields.io/badge/release-1.0-111111?style=flat-square" alt="1.0" /></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-MIT-777777?style=flat-square" alt="MIT License" /></a>
</p>

<p align="center">
  <img src="docs/media/hero.gif" alt="Colosseum home — Continue row and world shelves" width="840" />
</p>

## Overview

Your media library lives in five different apps: one for manga, one for comics, one for books,
one for audiobooks, one for film and TV. Each has its own catalogue, its own reader or player,
its own idea of progress — and none of them talk to each other.

Colosseum is one fullscreen home for all of it. Three worlds — **Tankoban** (manga and western
comics), **Biblio** (ebooks and audiobooks), **Theatre** (movies, shows, anime) — share a single
shell: one Continue row across every medium, one collection, one downloads vault, one taskbar of
open sessions. Each medium keeps the surface it deserves: a real comic reader, a real book
reader with audiobook read-along, a real video player. Browsing is catalogue-first and
discovery-rich; reading is download-fed, so your shelf works offline.

> [!IMPORTANT]
> Colosseum 1.0 is a Windows 10/11 desktop release. Download the installer from
> [Releases](https://github.com/kingoftheseas56/Colosseum/releases) for a per-user install —
> no administrator required. Building from source is documented below.

## Highlights

- **Three worlds, one shell.** Universal Continue, Collection, downloads, search history,
  wallpapers, and window sessions shared across manga, comics, books, audiobooks, and video.
- **Real readers and players, built in.** A from-scratch comic reader (long strip, paired
  pages, RTL, exact resume), an ebook reader with typography control and audiobook
  read-along, and an mpv-based player with subtitles, skip segments, episode queues, and
  seek thumbnails.
- **Deep catalogues that work offline.** Theatre shelves ranked by a local IMDb index;
  manga discovery from a bundled MAL catalogue; Apple Books charts for Biblio — live rows
  layered on top when the network is there.
- **Stremio-compatible extensions.** A world-aware Sources picker, Browse and Installed
  management, and per-world source chains. NoTorrent ships by default and is removable
  like any extension.
- **Download-fed by design.** Acquisition lands in one vault; reading and listening never
  depend on a live connection to the source that provided the file.

## Screenshots

<table>
  <tr>
    <td align="center"><img src="docs/media/screens/tankoban-series.png" alt="One Piece Tankoban series page" /><br /><sub>Tankoban series — volume view</sub></td>
    <td align="center"><img src="docs/media/screens/comic-reader.png" alt="Comic reader with page scrubber" /><br /><sub>Comic reader</sub></td>
  </tr>
  <tr>
    <td align="center"><img src="docs/media/screens/biblio-book.png" alt="Biblio book page" /><br /><sub>Biblio book page</sub></td>
    <td align="center"><img src="docs/media/screens/reader2-typography.png" alt="Ebook reader typography panel" /><br /><sub>Reader — themes and typography</sub></td>
  </tr>
  <tr>
    <td align="center"><img src="docs/media/screens/theatre-series.png" alt="Theatre series page" /><br /><sub>Theatre series page</sub></td>
    <td align="center"><img src="docs/media/screens/player-hud.png" alt="Player HUD" /><br /><sub>Native player</sub></td>
  </tr>
  <tr>
    <td colspan="2" align="center"><img src="docs/media/screens/player-loading.png" alt="Cinematic loading screen" width="70%" /><br /><sub>Cinematic loader</sub></td>
  </tr>
</table>

<!-- VIDEO EMBEDS: drag 01-hero-home.mp4, 02-theatre-discover.mp4, 05-tankoban-discover.mp4
     into this section in the GitHub editor; each becomes a playable embed. -->

## The three worlds

| World | For | Built-in sources |
|---|---|---|
| **Tankoban** | manga and western comics | WeebCentral, AniList, Kitsu, MangaDex, MAL + Jikan; GetComics; local comic catalog |
| **Biblio** | ebooks and audiobooks | Apple Books discovery, LibGen, AudioBookBay |
| **Theatre** | movies, shows, anime | Cinemeta + offline IMDb catalogue, Jikan/AniList/Kitsu; installed Stremio extensions |

## Players and readers

- **Theatre player** — fullscreen QML surface over MpvQt / libmpv. Resume, warm minimize, audio and subtitle selection, online subtitles, track delays, speed, fill and aspect, PiP, skip segments, episode queues, source failover, Up Next, A-B loop, sleep timer, captures, GIF tools, chapter markers, loudness normalization, and ffmpeg-backed seek thumbnails.
- **Player 2** — an experimental from-scratch D3D11 / FFmpeg engine, integrated behind an opt-in build and boot gate. Not the default player.
- **Custom comic reader** — built from scratch and shared by manga chapters, Tankoban volumes, and western comic editions. Long strip, single and double page, MangaPlus-style pairing, LTR / RTL direction, fit and zoom, wide-page splitting, prefetch, page and chapter grids, spread knowledge, scrub navigation, and exact resume.
- **Reader2** — native QML chrome over a least-privilege WebEngine paper. Contents, bookmarks, annotations, search, footnotes, typography, themes, keyboard navigation, and minimizable sessions. The audiobook engine lives behind its Audio surface with Follow my reading read-along.

## Extensions

The extension system is Stremio-compatible and has three surfaces: **Sources** (a world-aware picker with Theatre, Tankoban, and Biblio source chains on one page), **Browse** (discover and preview compatible manifests), and **Installed** (enable, order, remove).

**NoTorrent**, an HTTP streaming source extension, ships enabled by default in Theatre's Sources sheet and is removable like any extension. Adult manifests are rejected by the native registry rather than merely hidden.

## Downloads, Collection, sessions

- **Downloads** — one vault across manga chapters, Tankoban volumes, comics, LibGen ebooks, and Theatre video, with open, retry, pause, cancel, and delete routed to the owning backend.
- **Collection** — a durable manual library across all three worlds, separate from progress and local ownership.
- **Sessions** — open books, comic / manga readers, and video surfaces, switched from the taskbar. Audiobook playback stays inside the open book session.

## Wallpapers

Each world can persist its own wallpaper. The picker ships original Colosseum shaders — **Noir Flow** and **Low Poly** (animated) and **Aurora Flow** (adapted from an LGPL KDE Plasma wallpaper) — plus native mesh-gradient stills (Twilight, Ember, Mint). A curated KDE Plasma still shelf and Wallhaven search are also available. Animated scenes freeze while immersive media owns the screen.

## Tech stack

Qt 6 (Quick / QML, WebEngine, SQL, Concurrent) · C++ · MpvQt + libmpv · FFmpeg ·
libtorrent-rasterbar · SQLite catalogues · Stremio-compatible extension protocol.
QML owns presentation; native C++ owns durable state, files, catalogs, readers, playback
engines, torrent transport, WebEngine bridges, downloads, and system integration.

## Install

### Download the installer

Grab the latest **Colosseum-x.x-setup.exe** from
[Releases](https://github.com/kingoftheseas56/Colosseum/releases). It installs per-user — no
administrator needed — and runs on Windows 10/11.

### Automatic updates

Starting with 1.1.0, an installed Colosseum checks the stable GitHub Releases channel and shows a
quiet **Update** icon in the taskbar when a signed release is ready. Open the Update page to read
the release chronicle, review feature highlights, and choose **Download update**. Downloads stream
to a resumable cache; after the installer verifies its manifest, choose **Restart and update** to
apply the side-by-side update. The previous installation remains recoverable until the new shell
boots successfully, and the page keeps the latest chronicle afterward.

Colosseum renders release copy with local templates and accepts only HTTPS, signed, hash-verified
release assets. Drafts, prereleases, malformed releases, and unsigned releases remain invisible.
Source-tree/development launches do not perform automatic checks. Users on 1.0 should perform one
final manual install from [Releases](https://github.com/kingoftheseas56/Colosseum/releases); later
stable releases arrive through the Update page. The manual download remains a fallback.

### Build from source

Requirements: Windows 10 or 11, Visual Studio 2022 C++ Build Tools, CMake 3.16+, Ninja, Qt 6.11.1
MSVC 2022 64-bit (with Quick, QML, Network, GUI, SQL, Concurrent, WebEngineQuick, WebChannel,
WebSockets), MpvQt + libmpv, libtorrent-rasterbar with Boost and OpenSSL, the bundled Stremio
stream-server runtime, and ffmpeg. Python 3 is only needed to rebuild the optional catalogue
databases.

```bat
cmake -S native -B native/build-msvc -G Ninja ^
  -DCMAKE_PREFIX_PATH=C:/Qt/6.11.1/msvc2022_64 ^
  -DMPVQT_PREFIX=C:/tools/mpvqt-feasibility/mpvqt-msvc-install ^
  -DLIBMPV_PREFIX=C:/tools/mpvqt-feasibility/libmpv-prefix ^
  -DLIBTORRENT_ROOT=C:/tools/libtorrent-2.0-msvc ^
  -DBOOST_ROOT=C:/tools/boost-1.84.0 ^
  -DOPENSSL_MSVC_ROOT=C:/tools/openssl-msvc

cmake --build native/build-msvc
```

Then run the live QML loop with `dev.bat`. Player 2 additionally requires
`-DCOLOSSEUM_PLAYER2_IN_APP=ON` at configure time and `COLOSSEUM_PLAYER2=1` at boot.

## First run

1. Launch Colosseum — it opens fullscreen on Home, with each world one click away.
2. Pick a world and browse its Discover shelves, or search within the world.
3. On a series, book, or title page: **Read** / **Watch** streams or opens immediately;
   download actions pull media into the vault for offline reading and listening.
4. Everything you start appears in **Continue** on Home and in each world; open surfaces
   live on the taskbar as switchable sessions.
5. Extensions, wallpapers, and preferences live behind the shell's top-bar controls.

## Known boundaries

- Home-wide cross-world search is not implemented (per-world search is).
- Tankoban and Biblio extension consumption is incomplete; Theatre consumption is live.
- The calendar implementation is banked but has no live navigation route.
- Player 2 is integrated but opt-in and Windows / D3D11-oriented; mpv remains the default.
- Vinyl is visible as a coming-soon world, not yet implemented.
- The catalogue databases (`data/comics_catalog.db`, `data/mal_catalog.db`, `data/imdb_catalog.db`)
  are pipeline-built, gitignored deployment artifacts; their dependent shelves stay dormant when
  absent and the runtime never downloads the source dumps.
- Casting, live TV / DVR, and networked watch rooms are less mature than core playback.

## Design principles

- **Each medium gets the surface it needs.** Manga, books, and film are not the same problem.
- **Share transport, not policy.** One acquisition layer; each medium keeps its own rules.
- **Separate browsing from acquisition, and collection from progress.**
- **Match conservatively rather than opening the wrong work.**
- **Show real fallbacks and empty states instead of invented content.**

## Repository layout

```text
Colosseum/
├── qml/         Shell, worlds, media surfaces, components, providers
├── native/      C++ launcher and native services
├── resources/   Reader assets and vendored runtime resources
├── data/        Pipeline-deployed, gitignored SQLite catalogs
├── scripts/     Catalogue bake, installer, verification, maintenance
├── assets/      Icons, extension logos, fonts, wallpaper assets
├── docs/        Architecture laws, specifications, release notes
├── tests/       Contract, harness, source, and smoke tests
├── archive/     Retired implementations and preserved universe pages
├── dev.bat      Standard Windows QML live-reload loop
└── dist/        Built installers
```

## Contributing

Colosseum is developed in the open and steered by what its users actually hit. The most
valuable contribution is a **real issue**: a bug, a rough edge, a source that stopped
working, a feature you reached for and didn't find. Open one at
[Issues](https://github.com/kingoftheseas56/Colosseum/issues) — include what you clicked,
what you expected, and what happened. The app writes a rolling log at
`%APPDATA%/Brotherhood/Colosseum/logs/colosseum.log`; attaching its tail makes most bugs
diagnosable in one pass.

Pull requests are welcome for focused fixes. For anything larger, open an issue first so
the approach can be agreed before the work.

## License

[MIT](LICENSE) © 2026 Hemanth Ganneni

> [!NOTE]
> Colosseum is a client and does not host media. External APIs, sites, extensions, indexers, datasets, and scrapers are independent services and can change or disappear. Use sources and content only where you have the right to access them.
