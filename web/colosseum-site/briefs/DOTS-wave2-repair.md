# Dots repair assignment: make your six website pages 1:1 with the app

From Claude, on Hemanth's instruction (2026-10-08). Hemanth looked at the six pages you built and several are not 1:1 with the app. This assignment is to fix them. You own it end to end.

## Where to work

- Repository: `kingoftheseas56/Brotherhood`. Start from the current tip of `master`; resolve it and record the full SHA before you edit.
- Your six pages are merged there: `agents/colosseum-site/demos/home.html`, `extensions.html`, `vault.html`, `universes.html`, `feria.html`, `vinyl.html`.
- Work on your own branch (suggested `dots/colosseum-site-wave2-repair`) and push to it. Do not push to `master`.
- Merge endpoint: Claude reviews the branch and merges it into `master`; the branch is retired after that.

You must not change `index.html`, `demos/world.html`, or the six pages you did not build (`manga-series`, `book-page`, `theatre-series`, `video-player`, `comic-reader`, `book-reader`). You must not change the Colosseum repository.

## What "1:1" is measured against

The app's real QML, element by element. The earlier HTML rewrite of the app (branch `arc54-developer-webui-seam`) is out of date and is not evidence.

| Page | Authority |
|---|---|
| Home, Extensions | Colosseum tag `v1.1.8` (`15fd79af6331f2b4726ab2350a69a0c896cdbecd`) |
| Vault | Hemanth's mock `agents/colosseum-vault-browse-face-mock.html` for content and layout; `v1.1.8` QML for shared chrome |
| Universes, Feria | current tip of Colosseum `master` (resolve and record the SHA) |
| Vinyl | your branch `dots/vinyl-pages-qml-prototype` |

## The audits

Preflight workers are auditing each of your pages against that source and naming every deviation. Each audit lands on Brotherhood `master` as `agents/colosseum-site/demos/audits/<page>.md` (home, extensions, vault, universes, feria, vinyl).

For each page: pull `master`, read that page's audit, and fix every row in its Deviations table. If an audit has not landed yet, fix the items listed below for that page, then return to it when the audit arrives. If you believe an audit row is wrong, do not silently skip it: say so in your delivery record with the QML file and line that shows why.

## Rule change: show the real logos and names

The first brief told you to blank source names and to avoid brand logos. **That rule is withdrawn.** The pages must show what the app shows, including add-on names and logos and app icons. Copy the real assets, unchanged, from the Colosseum repository into the site:

- add-on logos: Colosseum `assets/addon-logos/` → site `agents/colosseum-site/assets/addon-logos/`
- the Extensions backdrop: Colosseum `assets/extensions/` → site `agents/colosseum-site/assets/extensions/`
- Feria app logos and glyphs: Colosseum `qml/feria/brand-logos/`, `qml/feria/portico-glyphs/`, `qml/feria/assets/` → site `agents/colosseum-site/assets/feria/`
- any other icon the app uses: Colosseum `assets/icons/` → site `agents/colosseum-site/assets/icons/`

Copy only the files a page actually uses.

## Already known, per page

**All six pages**

- Top-bar icons are the app's 22px line icons at 0.72 opacity with 20px gaps and nothing behind them. Four of your pages drew a dark box behind each icon and used 14px gaps. Claude has already removed that on `master`; do not bring it back.
- The world pills must match the page's own source, in the source's order. `universes.html`, `feria.html` and `vinyl.html` currently disagree with each other.
- `home.html` and `extensions.html` were delivered as a single 19,000-character line. Format every page as ordinary readable HTML, CSS and JS, like `world.html`.

**Extensions** (source: `qml/ExtensionsStorePage.qml`, `ExtensionsHouseHero.qml`, `ExtensionsStoreRail.qml`, `ExtensionsStoreCard.qml`, `ExtensionsStoreApi.js`, `AddonLogo.qml`)

- The page has its own backdrop, not the pier wallpaper: `assets/extensions/store-jolly-roger.jpg` over `#06070a`, under a vertical wash of `rgba(4,5,8)` at 0.55, 0.66 and 0.80.
- The hero tray shows eight tiles with their real icons and names, in this order: Tributes (`assets/icons/universes-atom.svg`), Grand Database, Nyaa, Tankoyomi, GetComics, Tankorent, LibGen, AudioBookBay (all in `assets/addon-logos/`). The recreation shows letters and the word "Source".
- Every store card shows the add-on's logo inside its 112px tile, with a blurred copy of the same logo washing the card behind it. The recreation shows a letter.
- Essentials holds five add-ons: OpenSubtitles v3, Streaming Catalogs, Comet, Anime Kitsu, Torrentio.
- After Essentials the store has seven more rows, each with its title and sub-line from `ExtensionsStoreApi.js`: Most popular, Play sources, Something to watch, Anime, World cinema, Subtitles, Better titles. The recreation has two rows.
- The store ends with the line "Community add-ons and stars from stremio-addons.net. Their authors run them, not Colosseum."

**Feria** (source: Colosseum `master`, `qml/feria/`, and `docs/research/2026-10-07-feria-dots-ui-contract-draft.md`)

- Hemanth: "Why does Feria have a tab splitter at the bottom too?" The recreation draws a tab strip at the bottom (`Feria | Netflix | Vinyl`) as well as the spaces sidebar. Draw only what the live source and the contract have there.
- Hemanth: "Where are all the app icons in Feria?" The app tiles show only a name. Use each app's real logo from `qml/feria/brand-logos/`.
- Hemanth: "Feria has wrong elements." Trace from `qml/feria/FeriaWorld.qml` to the components that are really instantiated (most `Portico*` variants in that folder are not live) and remove anything the live source does not draw.
- The opening frame puts the hero text straight on the wallpaper and it is hard to read. Use the scrim and backdrop the source uses.

**Universes** (source: Colosseum `master`, `qml/UniversesWorld.qml` and what it instantiates)

- 28 of the page's 53 images fail to load. Use the art addresses and assets the app really uses.

**Home, Vault, Vinyl**

- No specific reports beyond the shared ones above. Apply the audits.

## Checking your work

For each page, open it at exactly 1280x720 and check `?t=0` and two later moments against the source: every element the source draws is present, with the right asset, in the right place; nothing is drawn that the source does not draw; no console errors; every image loads.

## What to deliver

On your branch, under `agents/colosseum-site/`:

- the six repaired pages and the assets they use
- `demos/reports/<page>-t0.png` and `demos/reports/<page>-mid.png` for each page (these were missing last time)
- `demos/reports/DOTS-WAVE2.md` (also missing last time), short:
  1. the branch and exact tip commit, and the exact Colosseum commits you read from
  2. for each page, every audit row and what you did about it: FIXED, or DISPUTED with the QML file and line
  3. what you checked in the browser, per page, as PASS / FAIL / BLOCKED / NOT RUN
  4. anything you could not reach or could not do

If a source cannot be reached, fix what you can and report the rest as BLOCKED with the exact error. Do not rebuild a page from memory.
