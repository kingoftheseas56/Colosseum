# Preflight worker assignment: recreate the Theatre world (movies, shows, anime) as one scrolling HTML page

You are a Preflight worker. Do this job yourself, through Brotherhood Desktop Commander (BDC) on Laptop 1, and write your results to the local files named below. Nobody will read this chat; only the files count.

## The job

The Colosseum website (getcolosseum.world) shows the app through live HTML recreations of its pages. Earlier recreations were built from reading code alone and missed details the eye catches at once: sidebar icons, source logos, the top bar's pills. This round you have real screenshots of the running app. Build: **the Theatre world (movies, shows, anime)**.

Write exactly these files, and no others:

- `C:\Users\Suprabha\Desktop\Brotherhood\agents\colosseum-site\demos\theatre.html`
- `C:\Users\Suprabha\Desktop\Brotherhood\agents\colosseum-site\demos\reports\theatre-wave3.md`
- `C:\Users\Suprabha\Desktop\Brotherhood\agents\colosseum-site\demos\reports\theatre-wave3-top.png` and `theatre-wave3-scrolled.png`

`demos\theatre.html` may already exist. Replace it.

## What it must show

The whole Theatre world page as one scrolling page: the featured hero, the Next Up row, the tab bar (Discover, Movies, Shows, Anime, Library), and under Movies, Shows and Anime the catalogue: shelf rows (Recently Released, Top Rated, Hidden Gems, Cult Classics and the rest, in the order the rules file gives) with the row index sidebar. Clicking a tab switches the content in place. The tab bar sticks under the top bar once the hero scrolls away.

## The reference screenshots (the truth for how it looks)

Folder: `C:\Users\Suprabha\Desktop\Brotherhood\agents\colosseum-site\app-shots\`. Open and look at each one before writing anything:

- `08-theatre.png`: world home, Discover tab
- `19-theatre-scrolled.png`: Discover scrolled: Movie/Series/Anime switch, filter, poster grid, sidebar of source logos
- `20-theatre-movies-catalogue.png`: Movies tab, index sidebar closed
- `20-theatre-movies-index-open.png`: Movies tab, index sidebar open
- `21-theatre-shows-catalogue.png`: Shows tab, index sidebar closed
- `21-theatre-shows-index-open.png`: Shows tab, index sidebar open
- `22-theatre-anime-catalogue.png`: Anime tab, index sidebar closed
- `22-theatre-anime-index-open.png`: Anime tab, index sidebar open

They are captures of the app running today. Each is 1920x1008 because the laptop runs at 150% scaling: divide every measurement by 1.5 to get CSS pixels, so the app's own canvas is 1280 wide. They show Hemanth's real library. Use them to see layout, sizes, icons, spacing and glass. Do **not** copy their pixels, crop them into the page, or reuse his titles as a list. Stage the page with the covers and names the mock already holds.

## Where the measurements come from

- QML, read only: `git -C C:\Users\Suprabha\Desktop\Brotherhood\Colosseum show master:qml/<File>`. Start with: `TheatreWorld.qml`, `TheatreTabBar.qml`, `TheatreCatalogPage.qml`, `TheatreCatalogRules.js`, `TheatreGenreApi.js`, `RowIndexSidebar.qml`, `TopBar.qml`, `Taskbar.qml`, `Theme.qml`, `Glass.qml`. Take sizes, spacing, radii, font sizes, colours, wording, row order and icon choice from the QML. Where the QML and a screenshot disagree, the screenshot wins and you note it in the report.
- Icons: the app's own SVGs are in `C:\Users\Suprabha\Desktop\Brotherhood\agents\colosseum-site\assets\icons\` and `C:\Users\Suprabha\Desktop\Brotherhood\agents\colosseum-site\current-ui-mock\assets\icons\`. `RowIndexSidebar.qml` chooses each row's icon from the row's title by its own rules. Follow those rules exactly and check the result against the screenshots.
- Starting material: `current-ui-mock\demos\world.html?w=theatre&tab=movies` with `current-ui-mock\shared\world-catalogue.js` and `world-catalogue.css` already hold a first version. Reuse what is right in them.
- Covers, posters and logos: use the files under `C:\Users\Suprabha\Desktop\Brotherhood\agents\colosseum-site\current-ui-mock\assets\` and reference them by relative path (`../current-ui-mock/assets/...`). Wallpaper `../assets/cold-ripple.jpg`, display font `../assets/Fraunces-Regular.ttf`.

## The page contract

Read `C:\Users\Suprabha\Desktop\Brotherhood\agents\colosseum-site\demos\feria.html` first. It is the accepted example of this contract.

1. A fixed 1280x720 stage: `html,body{width:1280px;height:720px;overflow:hidden}`. The website scales it; do not make it responsive.
2. Inside the stage the page **scrolls vertically for real** (mouse wheel, a thin scrollbar), so a visitor can see the whole page. No scripted loop and no timeline animation.
3. Where the screenshots show a top bar, it stays fixed over the scrolling content and shows **all five world pills**: Tankoban, Biblio, Theatre, Universes, Feria, with the current world in gold. No Vinyl pill. The bottom-left dock (two round icons in a glass capsule) also stays fixed.
4. It is lightly interactive: tabs switch in place, the index sidebar opens and closes from its chevron, hover states respond. Nothing needs a server, a sign-in or the network.
5. One HTML file with inline CSS and JS. No frameworks, no build step, no external scripts, no external image addresses.
6. Tokens: gold `#f0c44a` (a sparing accent), ink `#f7f7f5`, dim `#c9c8d0`, dimmer `#9a99a5`, edge `rgba(255,255,255,.18)`. UI font `"Segoe UI", system-ui`.

## Check your own work

Render it and look at the result beside the screenshots before you report:

`"C:\Program Files\Google\Chrome\Application\chrome.exe" --headless=new --disable-gpu --window-size=1280,720 --virtual-time-budget=6000 --screenshot=<out.png> "file:///C:/Users/Suprabha/Desktop/Brotherhood/agents/colosseum-site/demos/theatre.html"`

Save the top of the page as `theatre-wave3-top.png`. For the scrolled capture, support `?y=<pixels>` in the address to start the page scrolled by that amount, plus `?tab=<name>` and `?index=open` where the page has tabs or an index sidebar. Use them to save `theatre-wave3-scrolled.png`.

The report (`theatre-wave3.md`) lists, in plain sentences: what you built, each place you could not match a screenshot and why, each value you guessed, and each difference you found between the QML and the screenshots.

## Rules

- Do not change the Colosseum repository: no checkout, no branch, no edits. Read with `git show` only.
- Do not start, drive, click or close the Colosseum app. It is running and in use.
- Do not edit `index.html`, `feria.html`, `feria-app.html`, anything under `current-ui-mock`, or any file other than your own four.
- Do not run `git add`, `git commit` or `git push` anywhere. Claude reviews and integrates the files.
- If something blocks you, write what and why into the report and stop. Do not improvise around it.
