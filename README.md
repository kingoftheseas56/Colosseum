<p align="center">
  <img src="assets/icons/colosseum.svg" alt="Colosseum" width="96" />
</p>

<h1 align="center">Colosseum</h1>

<p align="center">
  <strong>One desktop home for manga, comics, books, audiobooks, movies, shows, and anime.</strong>
</p>

<p align="center">
  <a href="https://github.com/kingoftheseas56/Colosseum/releases"><img src="https://img.shields.io/badge/Windows-10%2F11-111111?style=flat-square" alt="Windows 10/11" /></a>
  <a href="https://github.com/kingoftheseas56/Colosseum/releases/latest"><img src="https://img.shields.io/github/v/release/kingoftheseas56/Colosseum?display_name=tag&sort=semver&style=flat-square&label=release" alt="Latest release" /></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-MIT-777777?style=flat-square" alt="MIT License" /></a>
  <a href="https://github.com/kingoftheseas56/Colosseum/actions/workflows/desktop-ci.yml"><img src="https://img.shields.io/github/actions/workflow/status/kingoftheseas56/Colosseum/desktop-ci.yml?branch=master&style=flat-square&label=desktop-ci" alt="desktop-ci" /></a>
</p>

<p align="center">
  <img src="docs/media/hero.gif" alt="Colosseum home" width="840" />
</p>

<p align="center">
  <a href="https://github.com/kingoftheseas56/Colosseum/releases/latest"><strong>Download latest</strong></a> &nbsp;|&nbsp;
  <a href="docs/README.md">Documentation</a> &nbsp;|&nbsp;
  <a href="CONTRIBUTING.md">Contribute</a> &nbsp;|&nbsp;
  <a href="SUPPORT.md">Support</a> &nbsp;|&nbsp;
  <a href="SECURITY.md">Security</a>
</p>

## What is Colosseum?
Colosseum is a fullscreen Qt desktop media app built around a simple idea: different media deserve different interfaces, but they should not need different homes.

Three main worlds share one shell:

| World | Media | Main experience |
|---|---|---|
| **Tankoban** | Manga and western comics | Catalogue discovery, native acquisition, volume/chapter reading, custom comic reader |
| **Biblio** | Ebooks and audiobooks | Book discovery, Reader2, typography, annotations, audiobook playback and read-along |
| **Theatre** | Movies, shows and anime | Catalogue discovery, Stremio-compatible sources, mpv-based playback and episode sessions |

Across them, Colosseum provides a shared **Home**, **Continue**, **Collection**, **Downloads**, **Vault**, open-session taskbar, extensions, wallpapers, preferences, and cross-media universe pages.

**Vault** is the local-media side of Colosseum. Point it at folders you already own and it indexes those files in place, keeps their identity and progress, and routes them back into the same readers and player used elsewhere in the app.

## Latest release: 1.1.5

**Colosseum 1.1.5** is the current stable Windows 10/11 release. The installer is per-user and does not require administrator privileges.

Highlights include:

- **Read and Download are separate intents.** Read acquires what is needed and opens the requested media; Download acquires without unexpectedly launching a reader.
- **Large catalogues moved out of the installer.** Manga, Tankoban, comics, and IMDb databases are delivered into AppData through the Colosseum-Data release path.
- **Tankoban, Vault, Reader2, and Player 1 received major reliability work** around identity, recovery, progress, local media, switching, and background activity.
- **The release pipeline is stricter**, with CodeQL, clang-tidy, AddressSanitizer, fuzzing, dependency checks, installer fingerprinting, fresh-install boot checks, and uninstall smoke coverage.

Read the full [1.1.5 release notes](docs/release-notes/v1.1.5.md).

## On `master`

`master` is ahead of the 1.1.5 release and is where the next round of Colosseum work is being integrated. Current post-release work includes:

- **Multilingual Tankoban Chapter Mode** through Tankoyomi, with language-aware chapter providers and configurable provider order.
- **Keyboard-only navigation** across the shell, catalogues, readers, players, utilities, and deep pages, including directional focus and scrolling.
- **Linux beta qualification** and portable runtime/package work.
- **Account, recovery, sync, and lifecycle hardening** across the desktop client and account-service code.
- **Colosseum Server** development for the native streaming/server path.

These changes are development state, not a promise that every item above ships in the current 1.1.5 installer.

## Players and readers

- **Comic Reader**: long strip, single/double page, manga pairing, LTR/RTL, fit/zoom, wide-page splitting, prefetch, page grid, scrub navigation, and exact resume.
- **Reader2**: native QML chrome around a constrained WebEngine reading surface, with contents, bookmarks, annotations, search, footnotes, typography, themes, and audiobook integration.
- **Player 1**: the default fullscreen video player, built on MpvQt/libmpv, with resume, subtitles and audio tracks, episode queues, source failover, skip segments, PiP, chapters, captures, and seek thumbnails.
- **Player 2**: an experimental opt-in D3D11/FFmpeg engine. It is not the default player.

## Extensions and acquisition

Colosseum supports **Stremio-compatible extensions** for Theatre source discovery and playback, plus compatible catalogue discovery in Tankoban and Biblio.

Tankoban and Biblio also keep native acquisition paths for media types that do not map cleanly onto a generic video-stream model. Downloaded and local media remain usable independently of the discovery provider that originally found them.

## Install and build

### Windows

Download the latest `Colosseum-x.x.x-setup.exe` from [GitHub Releases](https://github.com/kingoftheseas56/Colosseum/releases/latest). The current stable installer targets Windows 10/11.

For a source build, follow [docs/build/windows.md](docs/build/windows.md).

### Linux

Linux is currently a beta/source-build track, targeting Ubuntu 24.04 / Linux Mint 22.x on x86_64. See [docs/build/linux.md](docs/build/linux.md) for the current runtime and packaging boundary.

### macOS

Apple-silicon source builds are documented in [docs/build/macos.md](docs/build/macos.md).

## Tech stack

**Qt 6 Quick/QML + C++**, with WebEngine, SQLite, MpvQt/libmpv, FFmpeg, libtorrent-rasterbar, and Stremio-compatible extension protocols.

The architectural direction is straightforward: QML owns presentation and interaction; native C++ owns durable state, media engines, files, catalogues, downloads, Vault indexing, playback machinery, and system integration where practical.

## Current boundaries

- The latest published binary release is **Windows 1.1.5**. Linux remains a beta/source-build track.
- A public production account service is not part of 1.1.5. The repository contains the account service and active client/service work, but released builds should not be treated as having generally available cloud accounts or sync.
- Home-wide cross-world search is not implemented; search remains world-specific.
- Player 2 is experimental and opt-in. Player 1 remains the default.
- Vinyl is a coming-soon world rather than a finished media surface.
- Casting and live TV/DVR are less mature than the core reading and playback paths.
- External sites, APIs, indexers, datasets, and extensions can change independently of Colosseum.

## Repository map

```text
Colosseum/
├── qml/          Application shell, worlds, readers, player UI, components
├── native/       C++ application entry, services, engines, stores, platform code
├── extensions/   Bundled extension and Tankoyomi resources
├── resources/    Reader/runtime resources
├── server/       Account, Watch Party, and Colosseum server-side components
├── data/         Local/deployed catalogue data used by development workflows
├── tests/        Native harnesses, QML tests, contracts, journeys, and regression tests
├── scripts/      Build, catalogue, release, verification, and maintenance tooling
├── packaging/    Packaging support
└── docs/         Public docs plus design/research/history used during development
```

## Documentation

Start at [docs/README.md](docs/README.md). Useful entry points:

- [Windows source build](docs/build/windows.md)
- [Linux build and runtime dependencies](docs/build/linux.md)
- [macOS source build](docs/build/macos.md)
- [1.1.5 release notes](docs/release-notes/v1.1.5.md)
- [Test verification](docs/colosseum-test-verification.md)
- [Lanista assembled-app verification](docs/colosseum-lanista-verification.md)
- [Terminology](docs/terminology.md)

## Contributing

Bug reports and focused pull requests are welcome. For larger behavior or architecture changes, open an issue first so the direction can be agreed before implementation.

See [CONTRIBUTING.md](CONTRIBUTING.md), [SUPPORT.md](SUPPORT.md), [SECURITY.md](SECURITY.md), and the [Code of Conduct](CODE_OF_CONDUCT.md).

## License

[MIT](LICENSE) © 2026 Hemanth Ganneni

> [!NOTE]
> Colosseum is a client and does not host media. External services and community extensions are independent of this project. Use sources and content only where you have the right to access them.
