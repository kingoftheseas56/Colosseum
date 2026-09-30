# Discover sidebar — design

Status: **Approved by Hemanth 2026-09-29** (Claude, brotherhood-brainstorming).
Scope: the Discover page of Theatre, Biblio, Tankoban Manga and Tankoban Comics (all one `DiscoverBrowser`).
Reference look: `colosseum-theatre-sidebar-concept.html` (maintainer-local file) (the `worldnav` rail) and, for retracting,
`colosseum-theatre-sidebar-concept (1).html` (Hemanth, 2026-09-29).

## 1. Experience promise and scope

**Promise:** on Discover you can always see every place you can browse and every way to narrow it, and change
either with one click, without a menu hiding the list.

**Replaces:** the catalogue dropdown behind "Now browsing ▾" (it hides the list, overflows the screen with a few
addons, costs two clicks per switch, and traps keyboard focus) and the separate Genre/Filter picker.

**In scope:** one sidebar design inside `DiscoverBrowser`, so Theatre, Biblio, Tankoban Manga and Tankoban Comics
get it together; the summary line above the wall; collapse; keyboard; empty states.

**Out of scope (Deferred, will join the sidebar later as their own groups):** browse by Awards, browse by
Language, "Can't decide?" (Harbor's Surprise me), Watch Party, Activity, Stats.

## 2. Primary journey

Layout of Discover with the sidebar open (left to right): sidebar (184 px wide, 28 px gap), then the wall. Above
the wall: the page title and the **summary line**. Nothing else sits above the wall any more: the Movie/Series/
Anime switch, the catalogue dropdown and the Genre picker are gone from there.

Sidebar, top to bottom:
1. **Retract toggle** — a 28 px round glass button straddling the rail's right edge near its top (the
   retractable concept, `colosseum-theatre-sidebar-concept (1).html`: `right:-14px; top:18px`), chevron
   pointing left; it rotates 180° when retracted.
2. **Type switch** — the world's types as a segmented row (Theatre: Movie · Series · Anime; Tankoban: its
   types; Biblio has a single type, so the row is hidden).
3. **Catalogues**, grouped by source. Each group has a small header with the source's logo and name
   (Theatre: Cinemeta, then each installed addon: Streaming Catalogs, Trakt, Torrent Catalogs, …; Biblio and
   Tankoban: their own sections as the adapters already declare them). The current catalogue carries the gold
   bar and a lighter plate (the concept's `.on` style). One click switches the wall.
4. **Filters**, one block per filter group of the current catalogue (Theatre: Genre; Manga: Genres,
   Demographics; Comics: Genres, Publishers, Formats, Availability; Biblio: its groups). Options are rows; the
   chosen one in each group is marked like the current catalogue. Clicking a chosen option clears it.

The rail only lists catalogues that fit the current type, so switching Movie → Series swaps the catalogue list.

**Summary line** (where "Now browsing" is today): the current catalogue, then each active filter, each with a
✕ (for example `Netflix · Crime ✕`, `Marvel · Omnibus ✕`). ✕ on a filter clears that filter; the catalogue part
has no ✕ (there is always a catalogue). The line is the always-visible statement of what fills the wall, rail
open or collapsed.

**Filters combine one per group** where the source supports it (Comics: Publisher Marvel + Format Omnibus;
Manga: Genre Action + Demographic Shōnen). Where a source accepts only one filter (Theatre addon catalogues, one
genre), choosing a second group's option replaces the first, and the rail shows only one group as active.

**Long groups:** the first eight options, then "Show all (N)" which expands in place; "Show less" folds it.
Groups with more than 40 options show a small search box at the top of the expanded list.

**Scrolling:** the sidebar is pinned. As the page scrolls, it stays beside the wall, its top just under the
docked tab bar; the rail's own list scrolls independently when it is taller than the window.

**Retract (per the retractable concept):** the toggle animates the rail to a 52 px strip: labels and group
titles fade, rows become 44 px icon buttons (type initials, the catalogue sources' logos; a logo click expands
the rail at that group), the gold active bar stays. The wall and the docked tab bar slide left to use the freed
width, so the wall gains its column back. The choice is remembered per world across launches. The summary line
keeps active choices visible while retracted.

## 3. States, interruptions, recovery, edge cases

- **Per-type memory** (as today): each type keeps its own catalogue, filters and scroll; switching back restores
  them.
- **No catalogues** for a type: the catalogue block says "Nothing to browse here yet" (today's copy).
- **Catalogue missing** (addon uninstalled): today's notice ("… is no longer available — showing the built-in
  catalogue instead") appears in the summary line area; the rail selects the fallback.
- **Filter yields nothing:** the wall shows "Nothing here matches this filter" with a **Clear filters** button.
- **Offline / downloading catalogue:** today's notices, unchanged, shown above the wall.
- **Catalogue with no filters:** the Filters section is hidden entirely.
- **Many addons:** the catalogue block scrolls with the rail; groups can be collapsed by clicking their header
  (remembered per world). No limit on the number of addons.
- **Small windows (under ~1250 px wide):** the rail starts retracted (the concept's narrow rule); under ~900 px it hides and the summary
  line's catalogue part becomes a button that opens the rail as an overlay (the concept's breakpoints).

## 4. Controls, feedback, accessibility, integration

- **Keyboard (constraints from the world-feel rules):** ← from the wall's first column enters the sidebar at
  the current catalogue; ↑/↓ move through type switch, catalogues, "Show all", filters; Enter/Space selects;
  → returns to the wall (to the card focus left). ↓ from the sidebar never reaches the TopBar. Esc inside the
  expanded search box clears it, then leaves it. Arrows use the shared navigator (nearest row, rows park), and the
  one focus look (`FocusRing`) is used throughout.
- **Mouse:** hover moves focus once Slice 4 lands; until then, hover gives the concept's hover plate.
- **Feedback:** selecting a catalogue or filter updates the wall in place (skeleton cells, as today) and the
  summary line immediately; the rail never closes or jumps on selection.
- **Accessibility:** rail items are real focus stops with accessible names ("Catalogue Netflix, Streaming
  Catalogs, selected"); the collapse chevron announces its state.
- **Look:** the concept's rail: glass panel with a thin right-edge highlight, 12 px item radius, gold active bar
  with glow, inkDim items brightening on hover/focus. Glass uses the fixed Glass mask (`9f29b622`).
- **Integration:** fits the one-scroller world page (Slice 7) and the docked tab bar (Slice 8); the wall keeps
  its row parking. Other tabs (Movies, Shows, Anime, Library, Manga, Comics tabs outside Discover) are unchanged.

## 5. Technical shape (only what the contract needs)

- The sidebar is part of `DiscoverBrowser`, fed by the existing adapter contract: `types()`, `catalogs(type)`
  (descriptors already carry `title`, `section`, `attribution`, and in Theatre the addon name/transport URL for
  `AddonLogo`), `filters(type, catalogKey)` (groups with options). No per-world sidebar code.
- **Multi-filter:** the browser state `filterGroup/filterKey` becomes a map `{group: key}`. The adapter declares
  whether a catalogue combines groups (new optional `combinesFilters(type, catalogKey)`; default false = one at a
  time). Tankoban (local MAL/comics DBs) and Biblio answer true where their queries support it; Theatre answers
  per catalogue from the Stremio manifest's supported extras. `fetchPage` receives the map.
- The dropdown (`catalogMenu`) and `filterPicker` are removed; the masthead keeps the title and gains the summary
  line.
- Pinned rail: positioned against the world page viewport (like the dock), not a second scroller for the wall.
- Retract state and group-fold state persist per world (existing Settings store). While the sidebar is present,
  the docked tab bar centres over the content column (right of the rail), as in the concept.

## 6. Acceptance criteria, non-goals, deferred

Acceptance (runtime, isolated Lanista session per world, plus Hemanth's eye):
1. Theatre, Biblio, Manga and Comics Discover each show the sidebar with type switch (when >1 type), grouped
   catalogues and filter groups; the old dropdown and Genre picker are gone.
2. One click on a catalogue switches the wall and the summary line; the current catalogue shows the gold bar.
3. Comics: Publisher Marvel + Format Omnibus both apply and both appear in the summary line; ✕ on one clears only
   it. Theatre: choosing a second genre replaces the first.
4. Publishers shows eight + "Show all (N)"; expanding shows the search box (N > 40) and it filters the list.
5. Scrolled to wall row 20, the sidebar is still beside the wall and switching a genre works without scrolling.
6. The edge toggle retracts the rail to the 52 px strip (chevron flipped), the wall gains a column, and the
   state survives a restart.
7. Keyboard: from the wall's first column ← enters the sidebar at the current catalogue; ↑/↓/Enter change it;
   → returns to the card focus left; ↓ never lands in the TopBar.
8. Empty filter result shows the message and Clear filters works.
9. Qt Quick tests: sidebar model from adapter descriptors; multi-filter state map; combine vs replace rule;
   keyboard enter/exit; collapse persistence. Negative control per test.

Non-goals: new data sources, new catalogues, awards/language/surprise features, changes to other tabs.

Discarded: sidebar across all of Theatre (other tabs would lose width for a list they do not use); filters kept
in a separate picker (keeps the split, the thing this replaces); always-open rail (costs a poster column
permanently).

Deferred: Awards, Languages, "Can't decide?", Watch Party, Activity, Stats — each can become a sidebar group
later; the concept's model (sidebar items that swap the page) was chosen for when they arrive.
