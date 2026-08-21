<p align="center">
  <img src="assets/icons/colosseum.svg" alt="Colosseum" width="96" />
</p>

<h1 align="center">Colosseum</h1>

<p align="center">
  <strong>A native desktop media environment for manga and comics, books and audiobooks, movies, shows, and anime.</strong>
</p>

<p align="center">
  <a href="https://github.com/kingoftheseas56/Colosseum/releases"><img src="https://img.shields.io/badge/Windows-10%2F11-111111?style=flat-square" alt="Windows 10/11" /></a>
  <a href="https://github.com/kingoftheseas56/Colosseum/releases"><img src="https://img.shields.io/badge/release-1.1.2-111111?style=flat-square" alt="1.1.2" /></a>
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
shell: one Continue row across every medium, one Collection, one Downloads surface, one taskbar
of open sessions, and **Vault**, a local-media library for files you already own. Each medium keeps
the surface it deserves: a real comic reader, a real book reader with audiobook read-along, and a
real video player. Browsing is catalogue-first and discovery-rich; downloaded media remains local
so reading and listening can continue offline.

> [!IMPORTANT]
> Colosseum 1.1.2 is the current Windows 10/11 desktop release. Download the installer from
> [Releases](https://github.com/kingoftheseas56/Colosseum/releases) for a per-user install —
> no administrator required. Building from source is documented below.

## What's new in 1.1.1

- **Tankoban no longer needs a live manga catalogue to draw a series page.** The browse path
  resolves from the bundled MAL catalogue, while `TankobanCatalog` supplies local volume counts
  and shelf art. The 1.1.1 data set covers 10,000 manga series and 28,856 BookWalker volume
  covers; uncovered volumes render an explicit **NO COVER** tile instead of invented artwork.
- **Tankoban is volume-only.** Chapter browsing/downloading and the old WeebCentral-backed chapter
  lane are no longer routed in the UI. On first launch, a one-time migration removes the obsolete
  chapter tree and `manga` progress records; downloaded Tankoban volumes are left alone. Nyaa is
  now the volume-source extension and starts disabled, with the source sheet routing to Extensions
  instead of silently enabling it.
- **Account Centre now has six pages with real backend wiring.** Profile supports username/avatar changes;
  Security handles protected sign-ins, approvals, password changes, and sign-out-everywhere;
  Devices can refresh and revoke; Recovery can replace a recovery key; **Your Colosseum** projects
  monthly watch and reading activity. Local search/activity clear actions are wired; data export,
  account deletion, and the privacy-policy switches remain explicit boundaries.
- **Watch Party landed as a configurable Player 1 preview.** The taskbar Join sheet and player
  panel cover room membership, guest/signed-in identity, rosters, chat/reactions, host/shared
  control, reconnect/grace/end flows, source readiness, drift/catch-up, and timeline commands.
  Exact torrent identity is supported; generic direct URLs are deliberately not. The desktop
  remains fail-closed until a Watch Party service URL is configured.
- **The updater can now hand off cleanly to the installer.** After a verified installer launches,
  1.1.1 persists the Installing state and requests Colosseum shutdown so the installer's
  `/WAITPID` handoff can continue. The public 1.1.1 release also includes the signed update
  manifest and signature. 1.1.0 users need one manual 1.1.1 install because the 1.1.0 release did
  not ship those assets.

Full release notes: [docs/release-notes/v1.1.1.md](docs/release-notes/v1.1.1.md).

## Highlights

- **Three worlds, one shell.** Universal Continue, Collection, Downloads, search history,
  wallpapers, local Vault media, and window sessions across manga, comics, books, audiobooks,
  and video.
- **Real readers and players, built in.** A from-scratch comic reader (long strip, paired
  pages, RTL, exact resume), an ebook reader with typography control and audiobook read-along,
  and an mpv-based player with subtitles, skip segments, episode queues, and seek thumbnails.
- **Local media is first-class.** Vault indexes folders in place, watches confirmed roots for
  arrivals, keeps a recent-arrivals view, preserves unavailable roots, and routes supported local
  files back into the same readers and player used elsewhere in Colosseum.
- **Deep catalogues that work offline.** Theatre shelves ranked by a local IMDb index; Tankoban
  discovery and series pages from bundled MAL/Tankoban catalogues; Apple Books charts for Biblio,
  with live rows layered on top when the network is there.
- **Stremio-compatible extensions.** World-aware Sources, community/curated Browse, Installed
  management, configured manifests, and direct/torrent Theatre stream results. NoTorrent ships
  by default and is removable like any extension.
- **Download-fed reading.** Tankoban volumes, comics, and Biblio retain native acquisition paths
  so the media you keep does not depend on the source remaining online after download.

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
| **Tankoban** | manga and western comics | bundled MAL + Tankoban catalogues; AniList metadata; Nyaa volume source (off by default); GetComics + local comic catalog |
| **Biblio** | ebooks and audiobooks | Apple Books discovery, LibGen, AudioBookBay |
| **Theatre** | movies, shows, anime | Cinemeta + offline IMDb catalogue, Jikan/AniList/Kitsu; installed Stremio extensions |

## Vault

**Vault is Colosseum's library for local files you already have.** It is separate from the
Downloads screen: Vault can index ordinary folders anywhere you choose, while Downloads continues
to track media acquired by Colosseum's own backends.

Add one or more roots and Colosseum scans them without relocating the originals. The Browse face
has recent arrivals, a root rail, breadcrumbs, nested folder navigation, media-shaped cards, and
in-place identity correction. Confirmed roots are watched for new files, while disconnected or
missing roots remain represented as **away** instead of making your library silently shrink.

Artwork follows the media rather than one generic poster rule. Comics and books can reuse covers
inside their files; recognized movies and shows can receive locally cached canonical posters;
episodes, clips, and other local video can receive persistent ffmpeg frame grabs. The resolver
keeps the result local once it has been acquired, so the Browse wall can keep its art offline.

Vault also has identity continuity for files that move or appear as copies. When a likely-copy
ceremony needs a decision, you can keep existing state or treat it as a separate copy rather than
letting the scanner silently merge the two. Supported books, comics/manga, and video then open
through Reader2, the comic reader, or the Theatre player instead of a second set of local-only
viewers. Colosseum's Downloads tree can also appear as a synthetic Vault root without deleting or
moving the underlying download data.

## Players and readers

- **Theatre player** — fullscreen QML surface over MpvQt / libmpv. Resume, warm minimize, audio and subtitle selection, online subtitles, track delays, speed, fill and aspect, PiP, skip segments, episode queues, source failover, Up Next, A-B loop, sleep timer, captures, GIF tools, chapter markers, loudness normalization, and ffmpeg-backed seek thumbnails.
- **Player 2** — an experimental from-scratch D3D11 / FFmpeg engine, integrated behind an opt-in build and boot gate. Not the default player.
- **Custom comic reader** — built from scratch and shared by Tankoban volumes and western comic editions. Long strip, single and double page, MangaPlus-style pairing, LTR / RTL direction, fit and zoom, wide-page splitting, prefetch, page grids, spread knowledge, scrub navigation, and exact resume.
- **Reader2** — native QML chrome over a least-privilege WebEngine paper. Contents, bookmarks, annotations, search, footnotes, typography, themes, keyboard navigation, and minimizable sessions. The audiobook engine lives behind its Audio surface with Follow my reading read-along.

## Watch Party

Colosseum 1.1.1 includes the Player 1 Watch Party client and the protocol-v3 relay package. A Join
action lives on the taskbar; room controls live inside Player 1. The client supports guest and
signed-in identity, participant rosters, chat and reactions, host/shared control, reconnect and host
grace, kick/rejoin, room end, local source readiness, sync status, catch-up, and room timeline
commands.

Source portability is intentionally strict. The current UI can prove torrent identity from
`infoHash + fileIdx`, so those sources can be shared exactly. Generic direct-stream URLs are not
eligible, and the verified-debrid seam is not inferred from ordinary QML rows.

The feature only activates when `COLOSSEUM_WATCH_PARTY_URL` is configured. With it unset, Join is
fail-closed and solo playback is unchanged. The repository includes a Cloudflare Worker + Durable
Object relay and self-hosting/deployment notes in
[`server/watchparty-relay/DEPLOYMENT.md`](server/watchparty-relay/DEPLOYMENT.md). Guest room flows
are accountless; public signed-in hosting still depends on account-service bearer authority.
Multi-client room membership/chat/kick/rejoin/grace/end behavior was runtime-validated for 1.1.1;
final in-app synced-playback acceptance remains a field-testing boundary.

## Extensions

The extension system is Stremio-compatible and has three surfaces: **Sources** (world-aware source
chains), **Browse** (curated/community discovery and manifest preview), and **Installed** (enable,
order, configure, and remove).

In Theatre, compatible add-ons can return ordinary torrent streams or direct HTTP streams. Direct
results can carry the add-on's request headers into the player, while configured manifests can
hand their own setup/authentication flow back to the provider. Colosseum does not try to own a
provider's debrid credentials or authentication state.

Tankoban and Biblio can consume compatible extension **catalogues** in their Discover surfaces,
while their acquisition/download paths remain native to those worlds rather than pretending every
Stremio stream shape maps cleanly onto a book or manga volume.

**NoTorrent**, an HTTP streaming source extension, ships enabled by default in Theatre and is
removable like any extension. Explicit-content manifests are hidden by default, but they follow the
same global **Explicit Content** preference as the rest of Colosseum when that setting is enabled;
direct manifest installation and community Browse use the same gate so the two paths cannot drift.

## Downloads, Collection, sessions

- **Downloads** — one taskbar surface across Tankoban volumes, comics, LibGen ebooks, and Theatre video, with open, retry, pause, cancel, and delete routed to the owning backend. Tankoban volume acquisitions expose resolving/progress/done state both in the source sheet and on the volume shelf.
- **Collection** — a durable manual library across all three worlds, separate from progress and local ownership.
- **Sessions** — open books, comic / manga readers, and video surfaces, switched from the taskbar. Audiobook playback stays inside the open book session.

## Accounts and sync

Colosseum 1.1.1 has account onboarding, remembered-session restore, an account medallion/flyout,
and a six-page Account Centre: **Profile**, **Your Colosseum**, **Security**, **Devices**,
**Recovery**, and **Data & privacy**.

Profile can rename the account and choose a built-in avatar. Security owns new-device protection,
pending sign-in approvals, password changes, and sign-out-everywhere. Devices can refresh and
revoke trusted devices. Recovery can replace the recovery key without exposing the secret through
the normal page state. Your Colosseum is backed by a profile-owned activity ledger and projects
monthly watch time, pages read, completions, active days, highlights, and recent activity.

Portable sync remains narrower than "sync my computer": Collection, Continue/progress, ordinary
history, and profile preferences have sync adapters. Machine-specific paths, downloaded/local
media files, window state, search history, and the raw Your Colosseum activity ledger stay local.
The Data & privacy page can clear local search history and activity history; its policy switches,
data export, and account-deletion flow do not yet have authoritative service wiring.

The account service endpoint is configurable rather than hard-coded into the public desktop source.
A build or runtime needs a configured account service before cloud sign-in/sync can function.

## Wallpapers

Each world can persist its own wallpaper. The picker ships original Colosseum shaders — **Noir Flow** and **Low Poly** (animated) and **Aurora Flow** (adapted from an LGPL KDE Plasma wallpaper) — plus native mesh-gradient stills (Twilight, Ember, Mint). A curated KDE Plasma still shelf and Wallhaven search are also available. Animated scenes freeze while immersive media owns the screen.

## Tech stack

Qt 6 (Quick / QML, WebEngine, SQL, Concurrent) · C++ · MpvQt + libmpv · FFmpeg ·
libtorrent-rasterbar · SQLite catalogues · Stremio-compatible extension protocol.
QML owns presentation; native C++ owns durable state, files, catalogs, readers, playback
engines, torrent transport, WebEngine bridges, downloads, Vault indexing, accounts/sync, and
system integration.

## Install

### Download the installer

Grab the latest **Colosseum-x.x-setup.exe** from
[Releases](https://github.com/kingoftheseas56/Colosseum/releases). It installs per-user — no
administrator needed — and runs on Windows 10/11.

### Automatic updates

Colosseum's installed updater checks the stable GitHub Releases channel and shows a quiet
**Update** icon when a newer signed release is available. The Update page can show the release
chronicle, download into a resumable cache, verify the signed manifest and installer hash, and then
launch the side-by-side installer.

1.1.1 closes the installer handoff that was incomplete in 1.1.0: once the verified installer has
successfully launched, Colosseum persists the Installing state and requests its own orderly
shutdown, allowing the installer's `/WAITPID` contract to continue. The public 1.1.1 release ships
all three required assets: the installer, `colosseum-update-v1.json`, and
`colosseum-update-v1.json.sig`.

Release acceptance remains fail-closed. Drafts, prereleases, malformed manifests, unsigned assets,
wrong hashes, and unsafe URLs are rejected; source-tree/development launches do not perform normal
automatic checks. Because the 1.1.0 GitHub release did not contain the signed manifest assets,
1.1.0 users need one manual install of 1.1.1 from
[Releases](https://github.com/kingoftheseas56/Colosseum/releases). From 1.1.1 onward, later stable
releases can use the Update page when their signed release assets are present. Manual download
remains the fallback.

### Build from source

Requirements: Windows 10 or 11, Visual Studio 2022 C++ Build Tools, CMake 3.16+, Ninja, Qt 6.11.1
MSVC 2022 64-bit (with Quick, QML, Network, GUI, SQL, Concurrent, WebEngineQuick, WebChannel,
WebSockets), MpvQt + libmpv, libtorrent-rasterbar with Boost and OpenSSL, the bundled Stremio
stream-server runtime, and ffmpeg. Python 3 is only needed to rebuild catalogue databases or run
the repository's Python verification tooling. The catalogue databases are deployment artifacts rather than ordinary Git
source; a source build needs the relevant files under `data/` for the corresponding offline
catalogue surfaces to populate.

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

### Development verification

The repository also contains **Lanista**, UI journey fixtures, and the Night Watch / Guardian
pipeline used to exercise the assembled app in isolated runs. Night Watch can collect failed
journeys and quality signals; the current Guardian policy is **document-only**, so its automated
path may reproduce, triage, diagnose, and write a bug record, but it does not silently merge a
repair into `master`. This is development infrastructure, not part of the installed media UI.

## First run

1. Launch Colosseum — it opens fullscreen on Home, with each world one click away.
2. Pick a world and browse its Discover shelves, or search within the world.
3. On a series, book, or title page: **Read** / **Watch** streams or opens immediately;
   download actions pull media into Downloads for offline reading and listening.
4. Add folders to **Vault** when you want Colosseum to index media already on your machine without
   moving the originals.
5. Everything you start appears in **Continue** on Home and in each world; open surfaces live on
   the taskbar as switchable sessions.
6. Extensions, wallpapers, preferences, account access, and updates live behind the shell's
   top-bar/taskbar controls.

## Known boundaries

- Home-wide cross-world search is not implemented (per-world search is).
- Tankoban is volume-only in 1.1.1. The old chapter browser/downloader is unrouted, and first launch
  removes the obsolete chapter tree plus `manga` progress. Downloaded Tankoban volumes are kept.
- Tankoban and Biblio can consume compatible extension catalogues for discovery, but their native
  acquisition paths are not generic Stremio stream consumers. Theatre is the world with generic
  torrent/direct-stream playback from compatible add-ons.
- Account/cloud sync requires a configured account-service endpoint. Profile, Security, Devices,
  Recovery, and Your Colosseum are live surfaces; Data & privacy policy switches, data export, and
  the account-deletion flow still lack authoritative service wiring.
- Watch Party requires a configured service URL. Exact torrents are eligible; generic direct URLs
  are deliberately not, public signed-in relay hosting still depends on account-service bearer
  authority, and final in-app synced-playback acceptance remains a field-testing boundary.
- The calendar implementation is banked but has no live navigation route.
- Player 2 is integrated but opt-in and Windows / D3D11-oriented; mpv remains the default.
- Vinyl is visible as a coming-soon world, not yet implemented.
- Catalogue databases such as `data/comics_catalog.db`, `data/mal_catalog.db`,
  `data/tankoban_catalog.db`, and `data/imdb_catalog.db` are pipeline/deployment artifacts rather
  than normal Git source. A source checkout without the relevant database keeps its dependent
  offline catalogue surface dormant instead of downloading source dumps at runtime.
- Casting and live TV / DVR remain less mature than core playback.

## Design principles

- **Each medium gets the surface it needs.** Manga, books, and film are not the same problem.
- **Share transport, not policy.** One acquisition layer; each medium keeps its own rules.
- **Separate browsing from acquisition, and collection from progress.**
- **Treat local files as media, not anonymous paths.** Vault can identify, decorate, and reopen
  local works without taking ownership of the originals.
- **Match conservatively rather than opening the wrong work.**
- **Show real fallbacks and empty states instead of invented content.**
- **Credit influence instead of styling it away.** The Vault's Browse card language — poster
  grid, near-square corners, a centered one-line title with a dim fact line beneath, circular
  corner indicators, dim-and-reveal hover, 16:9 episode cards — is adapted from Jellyfin's
  library view as rendered.

## Repository layout

```text
Colosseum/
├── qml/         Shell, worlds, media surfaces, components, providers
├── native/      C++ launcher and native services
├── resources/   Reader assets and vendored runtime resources
├── data/        Pipeline-deployed, gitignored SQLite catalogs
├── scripts/     Catalogue bake, installer, verification, maintenance
├── assets/      Icons, extension logos, fonts, wallpaper assets
├── docs/        Architecture, research, mockups, specifications, release notes
├── tests/       Contract, harness, source, journey, and smoke tests
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