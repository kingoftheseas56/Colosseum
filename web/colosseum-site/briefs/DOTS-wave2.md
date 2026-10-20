# Dots assignment: six page recreations for the Colosseum website

From Claude, on Hemanth's instruction (2026-10-08). You own this end to end in your own environment. No Codex, Work, BDC or Hemanth's machines are needed.

## What this is

The Colosseum website (getcolosseum.world) shows the app through live HTML recreations of its pages, not screenshots or video. Three are built and accepted, and six more are with Preflight workers. You build the last six:

| File | Page | State of the feature |
|---|---|---|
| `home.html` | The Home screen | Released in 1.1.8 |
| `extensions.html` | The Extensions store | Released in 1.1.8 |
| `vault.html` | Vault, shown full | Released in 1.1.8; this view is illustrative |
| `universes.html` | The Universes world | In development, not in 1.1.8 |
| `feria.html` | Feria | In development, not in 1.1.8 |
| `vinyl.html` | Vinyl | In development, not in 1.1.8 |

## Where to work

- Repository: `kingoftheseas56/Brotherhood`. Start from the current tip of `master`: resolve it, and record the full SHA in your delivery record before you edit anything.
- Create your own task branch (suggested name `dots/colosseum-site-wave2`) and push to it. Do not push to `master`.
- Merge endpoint: Claude reviews the branch and merges it into Brotherhood `master`; the branch is retired after that. Adoption is Hemanth's and Claude's step, not yours.

## Files you write

All under `agents/colosseum-site/`:

- `demos/<name>.html` for each of the six names above
- `demos/reports/<name>-t0.png` and `demos/reports/<name>-mid.png` for each
- `demos/reports/DOTS-WAVE2.md`, your one delivery record
- New icons, only if a page needs them: `assets/icons/<name>.svg`, copied unchanged from the Colosseum repository's `assets/icons/`

You must not change `index.html`, `demos/world.html`, any existing file under `assets/`, or anything outside `agents/colosseum-site/`. You must not change the Colosseum repository; read it only.

## The standard to match

Read `agents/colosseum-site/demos/world.html` in full first. It is the accepted standard, and every file you write follows the same contract:

1. A fixed 1280x720 stage (`html,body{width:1280px;height:720px;overflow:hidden}`). The website scales it. It is not responsive.
2. One self-contained HTML file: inline CSS and JS, no frameworks, no build step, no external scripts.
3. Fonts and wallpaper from the site's assets: `../assets/Fraunces-Regular.ttf`, `../assets/cold-ripple.jpg`, icons in `../assets/icons/`. UI font stack `"Segoe UI", system-ui`. Tokens: gold `#f0c44a`, ink `#f7f7f5`, dim `#c9c8d0`, dimmer `#9a99a5`, edge `rgba(255,255,255,.18)`, page margin 54px. Gold is a sparing accent only. The wallpaper carries the app's vignette, as in `world.html`.
4. Not interactive. One scripted loop of 12 to 18 seconds, driven by `requestAnimationFrame` from a single `frame(t)` function, as in `world.html`.
5. `?t=<milliseconds>` freezes that moment of the loop. `prefers-reduced-motion` shows the first frame only. The loop runs only while the page is on screen (IntersectionObserver), as in `world.html`.
6. Every image has a coloured fallback behind it, so a failed image never leaves a hole.

## Sources, and which one wins

Colosseum repository: `kingoftheseas56/Colosseum` (or the connected alternate fork if that is what you can reach; record which).

**Released pages (Home, Extensions): the 1.1.8 QML is the authority.** Tag `v1.1.8`, commit `15fd79af6331f2b4726ab2350a69a0c896cdbecd`. Take sizes, spacing, radii, font sizes, colours and wording from the QML. The older HTML mocks in the Brotherhood repo are background only.

- Home: the home section of `qml/Main.qml`, with `TopBar.qml` (no world pill selected, the Update glyph in place of Search), `ContinueTile.qml` (the `home` variant), `ContinueRow.qml`, `FeaturedCarousel.qml`, `VaultHomeWidget.qml`. Loop: rest on Home; the gold focus moves across the Continue cards; the page glides down past the world rows and back.
- Extensions: `qml/ExtensionsStorePage.qml`, `ExtensionsStoreCard.qml`, `ExtensionsStoreRail.qml`, `ExtensionsHouseHero.qml`, `ExtensionsCatalog.js`, `TopBar.qml` (its back-only form with the page title). Loop: rest on the store; a rail scrolls; one card takes focus and its Install button changes to Installed.

**Vault: Hemanth's mock is the authority.** He asked for Vault to be shown as it looks when full, from his own mock: `agents/colosseum-vault-browse-face-mock.html` in the Brotherhood repo. Recreate that populated view. Use the 1.1.8 QML (`VaultPage.qml`, `VaultPosterCard.qml`, `VaultWideCard.qml`, `VaultBrowseRail.qml`, `VaultBrowseCrumb.qml`) only for shared chrome, glass and type. Loop: rest on the full Vault; the focus moves through a few items; one folder opens and closes.

**In-development pages (Universes, Feria, Vinyl): the newest design source is the authority.** Resolve the current tip of each branch named below, record the full SHA in your delivery record, and work from that commit.

- Universes: Colosseum `master` (`qml/UniversesWorld.qml`, `UniverseTile.qml`, `UniverseBanner.qml`, `Universes.js`, and one universe page such as `OnePieceUniversePage.qml`). Loop: the Universes world home; focus moves across universes; one universe opens.
- Feria: Colosseum `master` (`qml/feria/`, and `docs/research/2026-10-07-feria-dots-ui-contract-draft.md`, which supersedes earlier tab and taskbar ideas), plus your own `dots/feria-*` branches where they are newer. Loop: the Feria home with its apps; one app opens; its spaces sidebar slides out and back.
- Vinyl: your own prototype branch `dots/vinyl-pages-qml-prototype`, and the Vinyl handoff in the Brotherhood repo (`agents/HANDOFF-CLAUDE-2026-10-07-vinyl-pages.md`). Loop: Vinyl Home; one album opens; a track takes focus.

An earlier HTML/CSS translation of the app exists on Colosseum branch `arc54-developer-webui-seam` under `web/developer-colosseum/` (`styles/tokens.css`, `styles/shell.css`, `styles/components.css`). It is useful for how QML values map to CSS. Where it disagrees with the authority named above, the authority wins.

## Rules for what appears on screen

- Staged content only: well-known titles, with art addresses taken from the app's own `qml/Catalog.js` and `qml/Universes.js` where possible. Never a real person's library.
- Do not show the name or logo of any download or streaming source (torrent sites, indexers, release groups, stream add-ons). Where the app shows one, write `Source`. Catalogue, metadata and subtitle extensions may keep their names (for example Cinemeta, AniList, OpenSubtitles, Apple Books, Colosseum Grand Database).
- Feria: service names may appear as plain text. Do not use third-party brand logos.
- Vault: label nothing as a real person's files; use generic file and folder names.
- The three in-development pages must not show a version number or the words "1.1.8".

## Checking your work

Open each file in a browser at exactly 1280x720. For each, check `?t=0` and one moment in the middle of the loop: no console errors, nothing overflowing the stage, text readable, every image either loaded or showing its fallback. Save those two views as the PNG files named above.

Your environment may lack Segoe UI and may not reach the art addresses. Both are acceptable: say so in the delivery record, and do not substitute a web font or embed images to compensate.

This is browser evidence from your Linux environment. Claude repeats the check on Windows before adoption.

## Delivery record (`demos/reports/DOTS-WAVE2.md`)

One file, short:

1. The Brotherhood branch and exact tip commit.
2. The exact Colosseum commits you read from (the tag, and the resolved tip of each branch).
3. For each page: what the loop shows in one or two sentences, the source files measured from, and everything staged or guessed.
4. What you checked in the browser, per page, as PASS / FAIL / BLOCKED / NOT RUN, with the browser and OS.
5. Anything you could not reach or could not do.

A Dots-Colosseum entry pointing at this record is welcome but not required. Do not duplicate the files there.

If a source you need cannot be reached, build the pages you can, and report the rest as BLOCKED with the exact error. Do not invent the missing page from memory.
