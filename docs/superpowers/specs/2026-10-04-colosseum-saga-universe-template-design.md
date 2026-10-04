# Colosseum — Saga universe template (Arc 58)

[Agent 0 (Claude), design + build] · 2026-10-04 · locked by Hemanth 2026-10-04 ("lock it and build the QML yourself").

Concept authority: `Preflight-Architect/arcs/58-universes-and-tributes/README.md`.
Layout authority: `Brotherhood/agents/colosseum-universe-saga-organised-mock.html` (five sagas, one renderer; measured at 1280×720).
Code: `qml/SagaUniversePage.qml`, `qml/SagaApi.js` (`sagaGrid`, `gridMove`, `enrichWatch`), `qml/Universes.js` (`sagaGrid` pairing).

## Promise

A saga's universe page lets you see the whole story at once and pick up any part of it in either form, without the page ranking the book above the film or the film above the book.

## Locked decisions

1. **Equal weight.** A book and its adaptation are the same tile, the same size, in the same grid. Exception: adaptations that have little to do with the source (Hemanth's examples: Dexter, the Bond films) are not paired; they sit in their own block.
2. **One grid for the saga.** A column per story, a row per form (Books, Films, Series). A story told in two films is two columns wide and its book centres over them.
3. **A show that covers several books is one wide tile across them.** Season tiles were rejected: shows stop following the books (Game of Thrones after season five), and the template must stay reusable.
4. **Colosseum's glass stays the material.** Panels, the section switcher and Continue tiles are glass over the universe's wallpaper; the hero has no panel over the artwork.
5. **Gold is sparing.** Gold marks progress and focus only. The active tab and primary buttons are white; number plates, ticks and the Upcoming tag are neutral. (Hemanth: "our gold color scheme is working against us in this mock"; this is the proposal carried forward — he has not explicitly ratified a tone change, and the house gold token is unchanged.)
6. **A Continue row**, the house Continue tile, limited to this universe's works.
7. **Harbor-level depth** below the story: gallery, creators, information, more universes.
8. **Every work carries a provider identity** (Universe Page Law). Unresolved slots stay empty and cannot be opened.
9. **Reusable without fuss.** The only hand-edited part per franchise is the pairing (which screen work adapts which story), plus branch titles, wallpaper and name, which the archive already holds.

## The page, top to bottom

- **Chrome** — Back, window controls, top scrim (unchanged house chrome).
- **Hero** — `SAGA`, the name, fact pills (story count + the archive's chips), `Read` and `Watch` with equal weight. While resuming they name where: "Read · Book 2".
- **Section switcher** — glass capsule: The saga · Beyond the saga · Upcoming · About. It scrolls with the page under the hero, then docks beside Back. A tab exists only if its section does.
- **Continue** — shown only when this universe has unfinished works in the Progress store.
- **The saga** — the grid in one glass panel, with a detail line for the work under the pointer or the keyboard: where it sits ("STORY 3 OF 7 · BOOK · 41% READ"), title, facts, synopsis, one action, and which world opens it.
- **Beyond the saga** — a glass block per branch (`sagaBranches`), each holding shelves by form. Adaptations the pairing left out fall into an "On screen" block. The comics door stays here.
- **Upcoming** — works a provider already identifies as unreleased.
- **Gallery · Creators · Information · More universes.**

## Grid rules

| Situation | Behaviour |
|---|---|
| More than fits the panel | The grid scrolls sideways, all rows together, with edge arrows. |
| Four columns or fewer | The detail line moves beside the grid. |
| Three or more rows | Tiles cap at 132 px wide instead of 164 px. |
| A story with no book | Its book slot stays empty. |
| A form with nothing in it | No row is drawn. |
| No pairing in the data | Every core book is a story; every adaptation goes to "On screen". |

Tiles are 124 px wide at minimum and grow to fill a short saga.

## Data contract

`Universes.js`, per saga entry, by canon index into `novels` / `films` / `shows`:

```js
sagaGrid: {
  stories: [ { short: "Deathly Hallows", book: 6, films: [6, 7] }, … ],   // `short` optional; default is the curated novel title
  spans:   [ { shows: 0, from: 1, to: 5 } ]                                // a work across several stories
}
```

`SagaApi.sagaGrid(uni, cfg)` → `{ stories, rows, cols, blocks, upcoming }` (pure). `SagaApi.gridMove(grid, row, cell, dx, dy)` → next cell or `null` (pure). `SagaApi.enrichWatch` fetches one full Cinemeta record per screen work for synopsis, runtime, rating and makers.

Creators, information and gallery are derived from the provider records, not written down.

## Keyboard and accessibility

- The grid is one Tab stop. Left/Right walk the story within a form; Up/Down move between the forms of the same story, skipping a form that has nothing there; Enter opens. At an edge the arrow leaves the grid.
- Each branch shelf is one Tab stop: Left/Right, Enter.
- Hover and keyboard move the same selection; the detail line follows both.
- Focus uses the house focus ring. Entering the grid scrolls the saga panel under the docked switcher.

## Opening a work

A tile, the detail action, Read and Watch hand the work to its owner: books to Biblio, films and series to Theatre. The universe page opens no player and no reader. Back returns to the universe (the shell stacks title pages above the universe layer).

## States

- **First visit** — no Continue row, no marks; Read and Watch start at the first story.
- **Returning** — Continue row; a tick on finished works, a gold bar on unfinished ones; Read and Watch resume.
- **Loading** — tiles show the title on the universe colour until the cover lands; slots keep their place.
- **Unresolved** — the slot shows its curated title, says "Details pending", cannot be opened.
- **Nothing upcoming / no branches / no stills** — the section and its tab are absent.

## Known gaps (state at commit)

- **Not reachable in the app yet.** `Main.qml` does not route to this page; the Universes world owns that. Verified through `tests/saga_template_preview.qml` under `qml.exe`, not in the running app.
- **Upcoming** is a shelf of tagged tiles. The mock's teaser-first block needs a teaser field the archive does not carry.
- **Information** shows the archive's `blurb`; several saga blurbs are house-written, not sourced. Status, studio, language and box office from the mock are not derived.
- **Creators** show initials; no portrait source is wired.
- **Book identities** are still resolved by title at runtime. Harry Potter lands "Enhanced Edition" listings and Percy Jackson's second book lands the graphic novel. Pinning Apple Books ids per novel fixes both.
- **Progress** matching for books assumes the Progress store keys a book by its Biblio id; not exercised against real progress.
- **Glass while scrolling** — the saga panel is a large live-blurred surface. Not measured for smoothness.
- Lord of the Rings, The Witcher and Dune have pairings and pass the headless grid test; they were not captured.

## Verification

- `tests/saga_grid_harness.qml` — the grid for all eight archived sagas and the keyboard walk (exit code).
- `tests/qml/tst_SagaTemplate.qml` — 12 cases: Read/Watch, keyboard order and form swap, detail follows selection, Continue and resume, edge arrows, unresolved works, empty state.
- `tests/saga_template_preview.qml --universe "<name>" --state returning --scroll N --wait S --capture out.png` — the real page with live provider data.

## Template model — one page, two centres (locked by Hemanth 2026-10-04)

This supersedes the earlier lock that the standard pages follow the archive's seven templates.
An arc grid (columns as arcs, tiles as chapter and episode ranges) was proposed for One Piece and
withdrawn the same day on Hemanth's ruling: "only tributes get such meticulous cataloguing". Arc-,
chapter- and episode-level structure belongs to a Tribute; one variation per type, never per franchise.
There is one universe page. Hero, section switcher, Continue, the detail line, gallery, creators,
information and more universes are shared. Only the centre section differs, and the archive
category picks it:

| Centre | For | Archive categories |
|---|---|---|
| **Story grid** (built, this spec) | A column per story, a row per form. For anime the column is a series and the rows are Manga and Anime: Dragon Ball has several columns, One Piece and Attack on Titan have one (the manga over its adaptations). Films, specials, spin-offs and novels sit in blocks beneath. A universe page lists WHOLE WORKS only. | `saga`, `anime`, `onepiece`, `dragonball` |
| **Eras** (not built; mock first, on Bond and Marvel) | No book-to-film pairing to line up: ordered groups (actors, phases, eras, decades), each a block with a shelf per form; the switcher jumps between groups. Long groups need a collapsed state or "see all". | `eras`, `cinematic`, `galaxy`, `studio`, `magazine` |

Known weak points, accepted: Eras flattens a franchise's own character (that belongs in a
Tribute); Shonen Jump is a publisher's lineup and may belong in Tankoban rather than Universes.

## The Universes world (built 2026-10-04)

`qml/UniversesWorld.qml` on the shared `WorldPage` shell. Not an endless catalogue, so not a
catalogue's layout: a carousel of **Tributes** (the hand-made art pages; buttons "Enter" and
"Universe page"), then every universe with a working page as a large full-width **banner**
(`qml/UniverseBanner.qml`: art along its length, name, what it holds), alphabetical, under one row
of **filter chips** by what a universe holds (All · Books · Manga · Anime · Films · Series ·
Comics). No tabs. A banner opens the universe's standard page; an installed universe with no
payload (Cosmere) opens its Tribute. The Home page keeps its carousel and its wording.
Not built: an "In progress" chip (needs per-universe progress matching), search.

## Anime universes on the story grid (built 2026-10-04, GLM; reviewed and rendered by Agent 0)

One Piece and Dragon Ball ride `SagaUniversePage` with the read form in manga. `SagaApi.sagaCanon`
gives both kinds of universe the same three shelves; `loadMangaUniverse` builds every work from its
curated pin and searches nothing. A book may span stories (`spans: [{ books, from, to }]`); row
labels come from the data (`MANGA`, and `sagaGrid.labels` for `ANIME`); leftover manga are
`Spin-offs`; each `filmEras` group is a block; a shelf wraps inside its block. `worldRoster` gives
any entry with a `sagaGrid` the page `"saga"`, and `openRosterUniverse` prefers the page.

Known gaps:

- **Manga progress is unmatched.** Tankoban keys reading by `mal:<id>`; the curated manga carry no
  MAL id, so a manga has no Continue tile, bar or resume here. Pinning MAL ids fixes it.
- **A manga opens Tankoban by its curated title query**, not a pinned id (same fix).
- **A book centred over three columns sits exactly on the middle one.** Dragon Ball's original
  manga reads as belonging to Z alone; only the detail line ("STORIES 1 TO 2") says otherwise.
- The manga detail panel has no facts or synopsis (none curated).
- "The saga", "Beyond the saga" and the "1 story" pill are saga wording on an anime page.
- Long film groups wrap to many lines (Dragon Ball's Z films); no "see all".
- An anime universe with no `wallpaper` uses the low, wide AniList banner as its backdrop
  (Naruto, Attack on Titan); it is soft at full height.

Naruto and Attack on Titan joined the same day (Arc 58-D): Preflight pinned every work, Agent 0
re-verified all 29 identities live against Cinemeta and AniList and merged them. Their manga carry
`al` and `mal` ids, which is what manga progress matching and open-by-id need; One Piece and Dragon
Ball manga still lack them. `openUniverseByName` follows the roster's rule (a `sagaGrid` opens
"saga"), so nothing routes to the July anime page any more.

## Deferred

The Eras centre; Tribute doors and downloadable Tributes; the Patreon gate; the One Piece story line (reading and watching position on one line) — the saga grid already shows both positions per story.
