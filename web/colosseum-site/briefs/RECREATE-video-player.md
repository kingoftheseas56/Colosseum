# Preflight worker assignment: recreate the Colosseum video player as one HTML file

You are a Preflight worker. Do this job yourself, through Brotherhood Desktop Commander (BDC) on Laptop 1, and write your results to the local files named below. Nobody will read this chat; only the files count.

## The job

The Colosseum website (getcolosseum.world) shows the app through live HTML recreations of its pages instead of screenshots or video. Three are already built. Build one more: **video player**.

Write exactly these files, and no others:

- `C:\Users\Suprabha\Desktop\Brotherhood\agents\colosseum-site\demos\video-player.html`
- `C:\Users\Suprabha\Desktop\Brotherhood\agents\colosseum-site\demos\reports\video-player.md`
- `C:\Users\Suprabha\Desktop\Brotherhood\agents\colosseum-site\demos\reports\video-player-t0.png` and `video-player-mid.png`

## The standard to match

Read this first, in full: `C:\Users\Suprabha\Desktop\Brotherhood\agents\colosseum-site\demos\world.html`. It is the accepted standard. Your file follows the same contract:

1. A fixed 1280x720 stage (`html,body{width:1280px;height:720px;overflow:hidden}`). The website scales it; do not make it responsive.
2. One self-contained HTML file: inline CSS and JS, no frameworks, no build step, no external scripts.
3. Fonts and wallpaper come from the site's own assets: `../assets/Fraunces-Regular.ttf`, `../assets/cold-ripple.jpg`, icons in `../assets/icons/`. UI font is `"Segoe UI", system-ui`. Tokens: gold `#f0c44a`, ink `#f7f7f5`, dim `#c9c8d0`, dimmer `#9a99a5`, edge `rgba(255,255,255,.18)`, page margin 54px. Gold is a sparing accent only.
4. It is not interactive. It plays one scripted loop of 12 to 18 seconds, driven by `requestAnimationFrame` from a single `frame(t)` function, exactly as `world.html` does.
5. `?t=<milliseconds>` in the address freezes that moment of the loop (for checking). `prefers-reduced-motion` shows the first frame only. The loop runs only while the page is on screen (IntersectionObserver), as in `world.html`.

## What it must show

The video player with its controls showing: the top row (back, Now Playing title and details, clock, window buttons), the info card, the timeline with chapter marks and the hover thumbnail, and the transport row (volume, play, skip, subtitles, audio, speed, fullscreen). A real screenshot is at `agents\colosseum-site\assets\player.webp`; use it to confirm your reading of the QML. For the picture itself use the title's backdrop image from `Catalog.js`, slowly drifting, not a film frame. The screenshot shows a source name beside the quality; leave that out.

The loop: the picture plays with no controls; the controls fade in; the playhead moves along the timeline and the hover thumbnail follows it across two chapter marks; the subtitle menu opens and closes; the controls fade out.

## Where the truth is

The recreation must match **Colosseum 1.1.8**, measured from its source, not from memory or taste.

- The app's source at the 1.1.8 tag. Read it with `git -C C:\Users\Suprabha\Desktop\Brotherhood\Colosseum show v1.1.8:qml/<File>.qml`. Start with: `player2/Player2Shell.qml`, everything in `player2/controls/`, `SubtitleMenu.qml`. Shared pieces: `Theme.qml`, `Glass.qml`, `TopBar.qml`, `TitlePageBar.qml`, `CatalogueVisualMetrics.js`, `Catalog.js`.
- An earlier HTML/CSS translation of the app, useful for how QML values map to CSS: `C:\Users\Suprabha\Desktop\Brotherhood\Colosseum-arc54-svelte-base\web\developer-colosseum` (`styles/tokens.css`, `styles/shell.css`, `styles/components.css`). Where it disagrees with the 1.1.8 QML, the QML wins.

Take sizes, spacing, radii, font sizes, colours and wording from the QML. Where you cannot find a value, make the smallest reasonable guess and list it in your report.

## Rules

- Do not change the Colosseum repository in any way: no checkout, no branch, no edits. Read only, with `git show`.
- Do not run the Colosseum app.
- Do not edit `index.html`, `world.html`, or any file other than your own four.
- Do not run `git add`, `git commit` or `git push` anywhere. Claude integrates the files.
- Staged content only: well-known titles, with cover and banner addresses taken from the app's own `Catalog.js` where possible. Never use a real person's library.
- Do not show the name or logo of any download or streaming source (for example torrent sites, indexers, release groups). Where the app would show one, leave the slot out or write `Source`.
- Every image must have a coloured fallback behind it, as `world.html` does, so a failed image never leaves a hole.

## Check your own work

Open your file in a browser at exactly 1280x720 (the laptop screen is 1280x720, so nothing may be judged at a larger size). Check `?t=0` and one moment in the middle of the loop. Confirm: no console errors, every image loaded, nothing overflows the stage, text is readable. Save those two views as the two PNG files named above.

## Your report (`reports/video-player.md`, short)

1. What the loop shows, in two sentences.
2. Which QML files you measured from.
3. Everything that is staged or guessed, as a list.
4. What you checked in the browser and what you saw.
5. Anything you could not do.

When the four files exist, end your reply with the single line: `DONE video-player`
