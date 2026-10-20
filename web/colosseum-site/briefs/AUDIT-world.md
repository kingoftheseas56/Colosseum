# Preflight worker assignment: audit the three world catalogue pages recreation against the real QML

You are a Preflight worker. Do this job yourself, through Brotherhood Desktop Commander (BDC) on Laptop 1, and write your result to the one local file named below. Nobody will read this chat; only the file counts.

## The job

The Colosseum website shows the app through HTML recreations of its pages. Hemanth has looked at them and found that several are not 1:1 with the app. Your job is to **name every deviation** in one recreation. You do not fix anything.

- The recreation to audit: `C:\Users\Suprabha\Desktop\Brotherhood\agents\colosseum-site\demos\world.html (three variants: `?w=tankoban`, `?w=biblio`, `?w=theatre`)`
- Write exactly one file: `C:\Users\Suprabha\Desktop\Brotherhood\agents\colosseum-site\demos\audits\world.md`

## The authority

The real page as it is in **Colosseum 1.1.8**: the QML at tag `v1.1.8` in `C:\Users\Suprabha\Desktop\Brotherhood\Colosseum`.

Read it with `git -C C:\Users\Suprabha\Desktop\Brotherhood\Colosseum show <ref>:<path>`. Record the full commit SHA of the ref you read (`git -C ... rev-parse <ref>`).

Start with these files, then follow every component they instantiate until you have read everything that draws on this page:

- `qml/TankobanWorld.qml`
- `qml/BiblioWorld.qml`
- `qml/TheatreWorld.qml`
- `qml/WorldPage.qml`
- `qml/WorldTabBar.qml`
- `qml/TheatreTabBar.qml`
- `qml/FeaturedCarousel.qml`
- `qml/CarouselSlide.qml`
- `qml/ContinueRow.qml`
- `qml/ContinueTile.qml`
- `qml/TankobanDiscoverPage.qml`
- `qml/BiblioDiscoverPage.qml`
- `qml/DiscoverPage.qml`
- `qml/DiscoverBrowser.qml`
- `qml/DiscoverSidebar.qml`
- `qml/AddonLogo.qml`
- `qml/DiscoverPicker.qml`
- `qml/CatalogueVisualMetrics.js`
- `qml/TopBar.qml`

Do not use the earlier HTML rewrite of the app as evidence (the `arc54-developer-webui-seam` branch, or the folder `Colosseum-arc54-svelte-base`). It is out of date. Only the source named above counts.

## How to audit

1. Open the recreation in a browser at exactly 1280x720. Look at `?t=0` and at three or four later moments (`?t=<milliseconds>` freezes the loop; the loop length is in the file's script).
2. Walk the QML from top to bottom and list every element the real page draws: each piece of text, each icon, logo and image, each button, bar, panel and divider, with its size, position, colour, radius, font and wording.
3. Compare the two lists, element by element.

Be exhaustive about **icons, logos and SVGs**: window buttons, top-bar icons, index and sidebar icons, app and add-on logos, glyphs inside buttons. For each one the app shows, say whether the recreation shows the same asset, a different one, or nothing, and give the real asset's path in the Colosseum repository.

Hemanth has already reported these on this page. Confirm each against the source and include it:

- "Where are all the index sidebar SVGs and icons?" The recreation's catalogue sidebar shows one stand-in icon. Say exactly which rows the real sidebar lists for each world and which icon each row uses (`AddonLogo`), with paths.
- Audit all three variants. Give one table, with a column or prefix saying which world each deviation belongs to.

## The file you write (`audits/world.md`)

```
# Audit: world
Authority: <ref> = <full SHA>
Recreation: demos/world.html (three variants: `?w=tankoban`, `?w=biblio`, `?w=theatre`)

## Deviations
| # | Kind | The recreation shows | The app has | Evidence | Real asset |
|---|------|----------------------|-------------|----------|------------|

## Staged on purpose
## Matches I verified
## Could not determine
```

- **Kind** is one of: `MISSING` (the app has it, the recreation does not), `EXTRA` (the recreation has it, the app does not), `WRONG` (both have it but it differs: asset, size, position, colour, wording).
- **Evidence** is the QML file and line at the ref, for example `qml/TopBar.qml:263`.
- **Real asset** is the repository path of the icon, logo or image the fix should use, when the deviation is about one.
- Order the table from most visible to least visible.
- Put content that differs only because it is staged (titles, covers, progress numbers) under "Staged on purpose", not in the table.
- If you cannot tell whether something deviates, put it under "Could not determine" and say what you would need.

## Rules

- Do not edit the recreation or any other existing file. Write only your one audit file.
- Do not change the Colosseum repository in any way: no checkout, no branch, no edits. Read only.
- Do not run the Colosseum app.
- Do not run `git add`, `git commit` or `git push` anywhere.
- Report what the source says, not what looks nicer. A deviation is a difference from the source, whatever you think of the design.

When the file exists, end your reply with the single line: `DONE audit world`
