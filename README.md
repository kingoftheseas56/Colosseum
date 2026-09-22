<p align="center">
  <img src="assets/icons/colosseum.svg" alt="Colosseum" width="96" />
</p>

<h1 align="center">Colosseum</h1>

<p align="center">
  <strong>Manga, comics, books, audiobooks, movies, shows, anime, and local media in one desktop app.</strong>
</p>

<p align="center">
  <a href="https://github.com/kingoftheseas56/Colosseum/releases"><img src="https://img.shields.io/badge/Windows-10%2F11-111111?style=flat-square" alt="Windows 10/11" /></a>
  <a href="https://github.com/kingoftheseas56/Colosseum/releases/latest"><img src="https://img.shields.io/github/v/release/kingoftheseas56/Colosseum?display_name=tag&sort=semver&style=flat-square&label=release" alt="Latest release" /></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-MIT-777777?style=flat-square" alt="MIT License" /></a>
</p>

<p align="center">
  <a href="https://github.com/kingoftheseas56/Colosseum/actions/workflows/desktop-ci.yml"><img src="https://img.shields.io/github/actions/workflow/status/kingoftheseas56/Colosseum/desktop-ci.yml?branch=master&style=flat-square&label=desktop-ci" alt="desktop-ci" /></a>
  <a href="https://github.com/kingoftheseas56/Colosseum/actions/workflows/code-quality.yml"><img src="https://img.shields.io/github/actions/workflow/status/kingoftheseas56/Colosseum/code-quality.yml?branch=master&style=flat-square&label=CodeQL" alt="CodeQL" /></a>
</p>

<p align="center">
  <img src="docs/media/hero.gif" alt="Colosseum home with the Continue row and world shelves" width="840" />
</p>

<p align="center">
  <a href="https://github.com/kingoftheseas56/Colosseum/releases/latest"><strong>Download latest</strong></a> &nbsp;|&nbsp;
  <a href="docs/README.md">Docs</a> &nbsp;|&nbsp;
  Source builds: <a href="docs/build/windows.md">Windows</a> &middot;
  <a href="docs/build/macos.md">macOS</a> &middot;
  <a href="docs/build/linux.md">Linux</a> &nbsp;|&nbsp;
  <a href="https://github.com/kingoftheseas56/Colosseum/issues/new?template=bug_report.yml">Report a bug</a> &nbsp;|&nbsp;
  <a href="SUPPORT.md">Support</a> &nbsp;|&nbsp;
  <a href="CONTRIBUTING.md">Contribute</a> &nbsp;|&nbsp;
  <a href="SECURITY.md">Security</a>
</p>

## What Colosseum is

Colosseum is a native Qt desktop app for keeping different kinds of media in one place. Tankoban handles manga and western comics, Biblio handles ebooks and audiobooks, Theatre handles movies, shows, and anime, and Vault indexes files you already have on your machine.

Those parts share Home, Continue, Collection, Downloads, settings, and open sessions, but each medium keeps its own reader or player. Comics use the comic reader, books use Reader2 with audiobook support, and Theatre uses the mpv-based player. Downloaded media stays local, and Vault does not move the files it indexes.

> [!IMPORTANT]
> Colosseum 1.1.6 is the current stable Windows 10/11 release.
>
> If you are upgrading from 1.1.5 or earlier, install 1.1.6 manually once from [Releases](https://github.com/kingoftheseas56/Colosseum/releases). 1.1.6 moved to a new Ed25519 update-signing trust root, so the older updater cannot authenticate it. After 1.1.6 is installed, automatic updates can use the new signing key normally.

## What's in 1.1.6

1.1.6 concentrates on keyboard control and reliability, with more background work moved off the UI thread.

- Arrow-key navigation now follows the visible layout across the shell, worlds, catalogues, account pages, settings, readers, and player controls. Enter activates, Escape backs out one level, and Tab remains available as a secondary way to move focus.
- Tankoyomi Chapter Mode now keeps multilingual provider settings, routes languages more safely, and falls back between sources more carefully. Metadata and image requests also have better IPv4 fallback when IPv6 routing is broken.
- Remembered account sessions, refresh locking, recovery approvals, and cross-device profile, activity, history, and recovery state are more reliable.
- More recurring work runs away from the main interface thread, hidden worlds do less background work, and Theatre can recover the bundled Stremio runtime after a failed start.
- The Windows installer remains the stable release artifact. Linux is still on its separate beta and source-build track.

Full release notes: [docs/release-notes/v1.1.6.md](docs/release-notes/v1.1.6.md).

## Screenshots

<table>
  <tr>
    <td align="center"><img src="docs/media/screens/tankoban-series.png" alt="One Piece Tankoban series page" /><br /><sub>Tankoban series, volume view</sub></td>
    <td align="center"><img src="docs/media/screens/comic-reader.png" alt="Comic reader with page scrubber" /><br /><sub>Comic reader</sub></td>
  </tr>
  <tr>
    <td align="center"><img src="docs/media/screens/biblio-book.png" alt="Biblio book page" /><br /><sub>Biblio book page</sub></td>
    <td align="center"><img src="docs/media/screens/reader2-typography.png" alt="Ebook reader typography panel" /><br /><sub>Reader themes and typography</sub></td>
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
| **Tankoban** | manga and western comics | local/managed MAL + Tankoban catalogues; AniList metadata; Tankoyomi chapter providers; Nyaa volume source (off by default); GetComics + local comic catalog |
| **Biblio** | ebooks and audiobooks | Apple Books + Open Library discovery, LibGen, AudioBookBay |
| **Theatre** | movies, shows, anime | Cinemeta + offline IMDb catalogue, Jikan/AniList/Kitsu; installed Stremio extensions |

## Vault

Vault is Colosseum's library for local files. It is separate from Downloads: Vault indexes folders you choose, while Downloads tracks media acquired by Colosseum's own backends.

The Vault entry stays on Home even before you add a folder. Once you add roots, Colosseum scans them in place and watches confirmed roots for changes. Missing or disconnected roots stay visible as `away` instead of silently disappearing. The Browse view has recent arrivals, folder navigation, search, filters, and identity correction for files that were matched badly.

Artwork comes from the media when possible. Books and comics can reuse embedded covers, recognized movies and shows can use locally cached posters, and local video can get persistent ffmpeg frame grabs. Once artwork has been acquired, Vault can keep showing it offline.

Vault also tries to keep identity and progress when files move or appear as copies. When a likely copy needs a decision, you can keep the existing state or treat the file as separate. Supported books, comics, manga, and video open through the same readers and player used elsewhere in Colosseum. The Downloads tree can also appear as a synthetic Vault root without moving or deleting the underlying download data.

## Readers and playback

- **Theatre player:** MpvQt/libmpv under a fullscreen QML surface. It has resume, warm minimize, audio and subtitle selection, online subtitles, track delays, speed controls, fill and aspect controls, PiP, skip segments, episode queues, source failover, Up Next, A-B loop, sleep timer, captures, GIF tools, chapter markers, loudness normalization, and ffmpeg-backed seek thumbnails.
- **Player 2:** an experimental D3D11/FFmpeg engine behind an opt-in build and boot gate. It is not the default player.
- **Comic reader:** shared by Tankoban volumes and western comic editions. It supports long strip, single and double page, MangaPlus-style pairing, LTR/RTL direction, fit and zoom, wide-page splitting, prefetch, page grids, spread knowledge, scrub navigation, and exact resume.
- **Reader2:** QML chrome over a least-privilege WebEngine paper. It has contents, bookmarks, annotations, search, footnotes, typography, themes, keyboard navigation, and minimizable sessions. Audiobook playback and Follow my reading live inside its Audio surface.

## Watch Party

Watch Party lives inside Player 1. Creating or joining a room uses the hosted protocol-v3 relay by default, so normal use does not require an endpoint setting. The taskbar has a Join action, and the player owns room controls, participant state, chat, reactions, reconnect behavior, host/shared control, source readiness, sync state, and timeline commands.

Torrent portability is strict: the room proves an exact torrent with `infoHash + fileIdx` so another participant can fetch the same source. Generic direct-stream URLs are not eligible.

`COLOSSEUM_WATCH_PARTY_URL` is still available for self-hosting and testing. The repository includes the Cloudflare Worker and Durable Object relay plus deployment notes in [`server/watchparty-relay/DEPLOYMENT.md`](server/watchparty-relay/DEPLOYMENT.md). Guest room flows work without an account. Public signed-in hosting still depends on account-service bearer authority being wired into the hosted relay. Final in-app synced-playback acceptance remains a field-testing boundary.

## Extensions

Colosseum's extension system is Stremio-compatible. Sources controls world-aware source chains, Browse handles curated and community discovery plus manifest preview, and Installed manages ordering, configuration, enable/disable state, and removal.

Theatre extensions can return torrent or direct HTTP streams. Direct results can carry provider request headers into the player, and configured manifests can hand setup or authentication back to the provider. Colosseum does not store a provider's debrid credentials as its own account state.

Tankoban and Biblio can use compatible extension catalogues for discovery, while their download and acquisition paths remain native to those worlds.

**NoTorrent** ships enabled by default in Theatre and can be removed like any other extension. Explicit-content manifests are hidden by default and follow the same global **Explicit Content** preference whether they arrive through direct installation or community Browse.

## Downloads, Collection, and sessions

- **Downloads** collects Tankoban volumes, comics, LibGen ebooks, and Theatre video in one taskbar surface. Open, retry, pause, cancel, and delete actions go back to the backend that owns the item.
- **Collection** is a manual library shared by all three worlds. It is separate from progress and from whether the media exists locally.
- **Sessions** are open books, comic or manga readers, and video surfaces that you can switch from the taskbar. Audiobook playback stays inside its book session.

## Accounts and sync

Colosseum 1.1.6 contains the desktop account client and uses an app-owned account-service endpoint by default. `COLOSSEUM_ACCOUNT_SERVICE_URL` can override it at runtime, and source builds can provide `-DCOLOSSEUM_ACCOUNT_SERVICE_URL=https://<host>` when they need a different service. The Go service lives at [`server/account-service`](server/account-service), with its deployment contract in [`server/account-service/DEPLOYMENT.md`](server/account-service/DEPLOYMENT.md).

An account is optional for local use. Vault, local media, readers, playback, and the rest of the on-device library still work without one. Hosted account features depend on the service being reachable; the repository does not treat provider-side deployment state as something the desktop build can prove. For local account testing, run [`tests/mock-account-service`](tests/mock-account-service) and point `COLOSSEUM_ACCOUNT_SERVICE_URL` at it.

When the service is available, Account Centre has Profile, Your Colosseum, Security, Devices, Recovery, and Data & privacy pages. Profile handles the account name and built-in avatar. Security owns new-device protection, pending sign-in approvals, password changes, and sign-out-everywhere. Devices can refresh and revoke trusted devices. Recovery can replace the recovery key without exposing it in normal page state.

Portable sync covers Collection, Continue/progress, ordinary history, and profile preferences. Machine-specific paths, downloaded or local media files, window state, search history, and the raw Your Colosseum activity ledger stay local. Data & privacy can clear local search and activity history; its policy switches, data export, and account-deletion flow still do not have authoritative service wiring.

## Development on `master`

`master` has moved past the v1.1.6 tag. It currently includes the Stremio Sync integration described in [docs/stremio-sync.md](docs/stremio-sync.md) along with later fixes and keyboard polish. Source builds from `master` can therefore differ from the stable 1.1.6 installer.

## Wallpapers

Each world can keep its own wallpaper. Colosseum ships Noir Flow and Low Poly as animated shaders, Aurora Flow adapted from an LGPL KDE Plasma wallpaper, and the Twilight, Ember, and Mint mesh-gradient stills. There is also a curated KDE Plasma still shelf and Wallhaven search. Animated scenes freeze while immersive media owns the screen.

## Tech stack

Qt 6 (Quick/QML, WebEngine, SQL, Concurrent) · C++ · MpvQt + libmpv · FFmpeg ·
libtorrent-rasterbar · SQLite catalogues · Stremio-compatible extension protocol.

QML handles presentation. Native C++ owns most durable state, files, catalogues, readers, playback engines, torrent transport, WebEngine bridges, downloads, Vault indexing, accounts/sync, and system integration.

## Code quality and security

Every push runs the project's desktop and code-quality checks, including:

- CodeQL across C/C++ and the Python, JavaScript, and GitHub Actions code.
- clang-tidy on native C++.
- AddressSanitizer coverage for the app and lifetime/ownership harnesses.
- Coverage-guided fuzzing for untrusted-input parsers such as CBZ/archive handling, Watch Party protocol parsing, and update manifests.
- Dependency checks for bundled and service dependencies.

See [SECURITY.md](SECURITY.md) to report a vulnerability.

## Install

### Windows installer

Download `Colosseum-1.1.6-setup.exe` from [Releases](https://github.com/kingoftheseas56/Colosseum/releases). It installs per user on Windows 10/11 and does not need administrator access.

If you are on 1.1.5 or earlier, 1.1.6 needs one manual install because the update-signing trust root changed. After that handoff, the built-in updater can use the new production signing key.

The updater checks the stable GitHub Releases channel. When a newer signed release is available, the Home top bar shows an Update control. The update page downloads into a resumable cache, verifies the signed manifest and installer hash, and then launches the installer. Drafts, prereleases, malformed manifests, unsigned assets, wrong hashes, and unsafe URLs are rejected. Source-tree development launches do not perform normal automatic update checks.

### Build from source

The published 1.1.6 installer is Windows-only. Source-build guides are maintained for Windows, macOS, and Linux. Linux is currently a beta/source-build track.

Windows builds use Visual Studio 2022 C++ Build Tools, CMake/Ninja, Qt 6.11.1 MSVC 2022 64-bit, MpvQt/libmpv, and libtorrent/Boost/OpenSSL. Pass your dependency locations explicitly when configuring the build.

- **[Build Colosseum on Windows](docs/build/windows.md)**
- **[Build Colosseum on macOS](docs/build/macos.md)**
- **[Build Colosseum on Linux](docs/build/linux.md)**

Player 2 remains an opt-in experimental build path. MpvQt/libmpv is the default player.

### Development verification

The repository includes Lanista UI journeys and the Night Watch/Guardian verification pipeline. Night Watch collects failed journeys and quality signals. Guardian is document-only: it may reproduce, triage, diagnose, and write a bug record, but it does not merge repairs into `master` on its own.

## First run

1. Launch Colosseum. It opens fullscreen on Home.
2. Pick Tankoban, Biblio, or Theatre and browse or search inside that world.
3. Open a series, book, or title. Read and Watch open media immediately when the source is ready; download actions send media to Downloads for offline use.
4. Add folders to Vault if you want Colosseum to index media already on your machine without moving it.
5. Anything you start can appear in Continue, and open readers or players stay available as taskbar sessions.
6. Extensions, wallpapers, preferences, account access, and updates live in the shell controls.

## Known boundaries

- Home does not have one cross-world search yet. Search is per world.
- After dismissing the Tankoban volume sources picker, volume cards can remain unresponsive for a few seconds before recovering.
- Tankoban and Biblio can use compatible extension catalogues for discovery, but their native acquisition paths are not generic Stremio stream consumers. Theatre handles generic torrent and direct-stream playback from compatible add-ons.
- Hosted account features depend on the account service being reachable. Local media use does not.
- Watch Party can share exact torrent identity, but generic direct URLs are deliberately excluded. Public signed-in hosting also depends on bearer-authority wiring in the hosted relay.
- The calendar implementation exists in the repository but has no live navigation route.
- Player 2 is opt-in and Windows/D3D11-oriented. MpvQt/libmpv remains the default.
- Vinyl is visible as a coming-soon world and is not implemented yet.
- Catalogue databases such as `data/comics_catalog.db`, `data/mal_catalog.db`, `data/tankoban_catalog.db`, and `data/imdb_catalog.db` are deployment artifacts rather than normal Git source. Source builds prefer local copies and otherwise use the catalogue vault to fetch published databases into AppData.
- Casting and live TV/DVR are still under development.

## How the project is built

Transport is shared where it makes sense, while each medium keeps its own rules. Collection and progress are separate concepts. Vault keeps identity and metadata for local files without taking ownership of those files. Matching is conservative, so an uncertain result is left alone instead of opening the wrong work.

The UI should show real empty states and fallbacks instead of inventing content. Vault's Browse card layout is adapted from Jellyfin's library view: poster grids, near-square corners, centered one-line titles, dim fact lines, circular corner indicators, hover reveal, and 16:9 episode cards.

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

## Contributing and project help

Bug reports and focused pull requests are welcome. For larger changes, open an issue first so the direction can be discussed before implementation.

- [Contributing guide](CONTRIBUTING.md)
- [Bug reports and feature requests](https://github.com/kingoftheseas56/Colosseum/issues/new/choose)
- [Support and troubleshooting](SUPPORT.md)
- [Security policy](SECURITY.md)
- [Code of Conduct](CODE_OF_CONDUCT.md)

## License

[MIT](LICENSE) © 2026 Hemanth Ganneni

> [!NOTE]
> Colosseum is a client and does not host media. External APIs, sites, extensions, indexers, datasets, and scrapers are independent services and can change or disappear. Use sources and content only where you have the right to access them.
