# Colosseum World Feel Design

**Date:** 2026-09-24

**Status:** Brainstorm locked by Hemanth on 2026-09-24. This specification is awaiting his review.

**Owner:** Hemanth (product). Agent 0 (Claude) is design lead.

## Promise

Every Colosseum page should move like the Siaran (formerly Portico) prototype and the Theatre halfway mock. Focus is always visible and in one place. The mouse and keyboard drive the same focus. Each page has one scroller. Back always returns you to exactly where you were. Tankoban, Biblio and Theatre keep their own looks, but going from a world page into a title, a search or a utility page must still feel like the same app.

## Scope

In scope:

- Home and the shared world shell (`WorldPage`, `TopBar`, the world tab bars).
- Theatre, Tankoban and Biblio, including every tab.
- Global and world search.
- Title and detail pages: Theatre series and movie pages, the manga series page (Tankoban Mode and Chapter Mode), the comics series page, the Biblio book page, genre pages, See-all pages and Continue See-all.
- Utility pages: Vault, Extensions, Settings, Downloads, the Keyboard Guide and Sync.
- The taskbar, the account flyout and the Escape/Back policy.

Out of scope:

- The book reader, the comic reader and the video player.
- Siaran. It stays a separate surface with its own data and design.
- Universe pages. They receive the frame and Back rules only where they already use the shared shell.

## Relationship to the Preflight WorldPage prototype

Preflight is writing a prototype `WorldPage.qml` modelled on the Theatre halfway mock, keyboard behaviour included. If it is good enough, parts of it may be adopted. It is not yet the production path. This specification is the yardstick for judging it: adopt the parts that meet the interaction contract, and rebuild the rest. This specification also covers every surface the mock does not reach. Where the mock and this specification differ, this specification wins:

- The mock's horizontal episode rail is not used. Episodes keep the vertical ledger (see "Long lists").
- The world pills stay visible on title, search and utility pages. The mock's title page hides them.
- Escape on Home never quits the app. In the mock, Home was out of scope.

## Locked decisions

1. **Escape on Home no longer quits.** Back on Home scrolls to the top and stops there. Quit is available only from the system menu, and it asks for confirmation first. `Ctrl+Q` remains a direct quit.
2. **Home gets search** across Tankoban, Biblio and Theatre only, never Siaran. Siaran would duplicate every title. Home search is styled like Siaran's search. Each world's own search searches that world by default.
3. **The world tab row stays pinned** under the top bar while you scroll.
4. **The Tankoban · Biblio · Theatre pills are visible on every page.** The only exceptions are the book reader, the comic reader and the video player.
5. **The taskbar becomes a smaller pill** that hides while you scroll down and returns when you scroll up. It never covers a control.
6. **The Tankoban Mode volume carousel stays.** It accelerates with wheel speed, and it keeps opening on the volume you are reading.
7. **Chapter Mode becomes one continuous list**, gated on thumbnail reliability (see "Long lists"). The pages of ten stay until that gate passes.
8. **Chapters are never grouped by volume.** Volume-to-chapter mapping metadata is a settled lost cause. `MangaChapterGrouping.js`'s exact-volume mode stays dormant.
9. **Everything else follows the interaction contract and page specifications below**, as presented in the feel audit and accepted when the brainstorm was locked.

## Constraints

- No existing capability may be lost (see "Capabilities that must survive").
- Each world keeps its visual identity: its wallpaper grammar, the Home widgets, the Tankoban fan, the Theatre film strip, the Biblio reading desk and drop caps, and the Vault door.
- Hemanth's durable rule: the world pills are the one-app illusion and must never be dropped for a "clean" frame.

## Ground truth (live walk, 2026-09-24)

This section is evidence from eight isolated Lanista sessions of the 13:38 build, driven by keyboard and mouse against a copy of a real profile.

**What ships and works:**

- Gallery-profile catalogue tabs: Theatre Movies, Shows and Anime.
- A virtualised episode ledger with Absolute and Seasons order, Go to episode, Next Up and visible descriptions.
- Tankoban genre pages.
- The Extensions Sources order view.
- The Theatre Library ledger.
- Home world widgets.
- `ScrollGlide` wheel motion.
- `KeyboardSpatialNavigator`.
- `ShellBackPolicy` as the single owner of Escape.
- The carousel already opens on the volume being read; `Shift`+wheel and `PgUp`/`PgDn` step ten volumes.

**Defects this design closes:**

- `KeyboardAction` paints focus as a 1px line at 28% gold. Focused gold buttons show no change at all.
- From a cold Theatre, the first `↓` focuses TopBar "‹ Home". Repeated `↓` reaches the Quit button.
- Discover walls (Theatre, Tankoban and Biblio) and Library walls are fixed-height scrollers nested inside the page. The keyboard cannot enter the Discover wall from its filter.
- The tab bar scrolls away.
- Escape from world content goes straight to Home. Escape on Home quits.
- Back from a search result returns to the world, and the query is lost.
- Leaving a detail page resets the world's scroll position.
- The mouse Back button and `Backspace` are unhandled in the shell.
- There are three search implementations and no search on Home. The Keyboard Guide advertises `Ctrl+F`, which the worlds don't bind. `Enter` in `SearchSurface` is a window-wide shortcut that opens the Top Match whatever is focused.
- There are five distinct top-bar frames.
- The taskbar bubble overlaps content on at least five surfaces.
- Hover and keyboard focus render as two separate highlights.
- Discover walls, Library walls and search grids render at about 270×405 px posters, six per row at 1920 px.
- The One Piece episode ledger has a sticky header about 350 px tall, rows 156 px tall, a "Season 1" heading while in Absolute order, and "AVAILABLE" on every row.
- Chapter Mode shows pages of ten.
- The manga header hides Ratings & Reviews behind the mode switch.
- The Vault hero draws two titles at once.
- Theatre detail pages have no backdrop art and clip their facts.
- The Featured carousel pops in late and causes layout shift. The TopBar can show a placeholder clock on its first frame.
- Entering Tankoban → Comics and pressing `↓` produced a GUI stall longer than 10 s (single observation).

## Interaction contract

These rules are implemented in shared components so that every page inherits them. Values are given at design scale: a 1920 px-wide window with `Theme.margin` = 54.

### R1. One focus look

- Tiles, cards and episode rows show a 3 px `Theme.gold` ring, scale to 1.05 over 200 ms with `cubic-bezier(.2,.7,.2,1)`, and add a lift shadow (0, 22, 42 blur, black 55%).
- Controls show the 3 px ring only.
- Gold-filled buttons show a 3 px dark gap (`#06070b`) and then a 3 px gold ring, and scale to 1.04.
- The ring never changes between worlds. A world may tint only the lift shadow.
- With reduced motion, the scale and lift are dropped and the ring stays.
- Owner: `KeyboardAction` replaces its "quiet aura". `CataloguePosterCard`, `ContinueTile`, the tab pills and ledger rows use the same look.

### R2. Hover is focus

- When the pointer moves over a focusable item, that item takes focus without scrolling the page.
- A stationary pointer never takes focus back after keyboard movement.
- The first key after mouse use continues from the hovered item.
- Only one item is highlighted at a time. No separate hover highlight exists on focusable items.
- A click activates the item directly. It does not require a prior focus step.

### R3. One scroller per page

- A world page, detail page or utility page has exactly one vertical scroller.
- Discover, Library and search grids are laid out in the page's own flow. They page in more items when focus or scroll comes within two rows of the end, using a skeleton row sized to the real tiles.
- Independent panes may keep their own scroll: Vault's folder rail, the readers and the players.

### R4. Rows park and focus is never off-screen

- When focus enters a new row or section, the page scrolls so the section heading parks 14 px below the chrome. The chrome is the top bar (96 px) plus the pinned tab row (64 px, where present).
- Grids park one row at a time.
- Horizontal rails keep the focused tile at least one tile away from the rail's edge. They show ‹ › edge buttons on hover, and each click scrolls 75% of the rail's width.
- Focus may only land on an item that is fully visible after parking.
- The wheel never moves focus. After a wheel scroll, the next key lands on the nearest fully visible item in the direction pressed.

### R5. Chrome is entered on purpose

- `↓` never moves focus into the top bar or the system controls.
- `↑` from the first content region moves to the pinned tab row, and `↑` again moves to the current world pill.
- Minimize, fullscreen, wallpaper and Quit move into one system menu at the far right of the top bar. Quit asks for confirmation.
- The account control and Theatre's Stremio control stay beside the system menu.

### R6. Back

- `Esc`, `Backspace` (outside text fields), the mouse Back button and gamepad B all mean Back.
- Back goes one step and restores the exact prior item, with its scroll offset, tab, filters and search query.
- The Back ladder inside a world:
  1. Any content goes to the pinned tab row, on the current tab.
  2. The tab row goes to the world's top (the Featured primary action).
  3. The world's top goes to Home.
  4. Back on Home goes to Home's top, and then does nothing.
- On layers (detail, search, utility and sheets), Back closes the layer and restores the item that opened it.
- Owner: `ShellBackPolicy`. Its final `"quit"` becomes `"homeTop"`. Every layer records one return point when it opens.

### R7. Region memory

- Returning to a row lands on the last item focused in that row.
- Re-entering a world restores its tab, scroll and focus. The retained world instances keep this state.
- Detail pages reopen on the episode or chapter last used.

### R8. Search entry

- Typing a letter or digit anywhere outside a text field opens search with that character in the field. This applies on Home and in the worlds; readers and players are excluded.
- `/` and `Ctrl+F` also open search.

### R9. Hint strip and shortcuts

- A 42 px pill, centred bottom, shows 3–4 keys for the focused item. Examples: `Enter` · Details, `Space` · Resume, `Esc` · Tabs, `[ ]` · Tabs, `A–Z` · Search.
- The strip drops to 40% opacity while the mouse is in use, and can be turned off in Settings.
- `[` and `]` switch tabs from anywhere in a world.
- On Home, `1`, `2` and `3` enter Tankoban, Biblio and Theatre.
- The Keyboard Guide lists only bindings that exist on the current surface.

### R10. Motion and stability

- Opening or closing a layer: 180 ms opacity fade plus an 8 px rise.
- Switching worlds: a 220 ms cross-fade over the persistent wallpaper.
- Every late-loading block (the Featured carousel, rails, walls, detail heroes) reserves its final geometry with a placeholder. Content never shifts after first paint.
- The TopBar clock is correct on its first frame.
- Reduced motion replaces every transition with an instant change.

### R11. Target sizes

- Poster walls and search grids use the gallery profile: 148×222 tiles with 12 px corners and 13 px titles, about eleven per row at 1920 px.
- Wide tiles and episode stills are 300×169.
- Clickable icons are at least 40×40.
- Interactive labels are at least 13 px.
- No interactive element may sit under the taskbar pill.

## The shared frame

One top-bar component with three densities, always drawn over the persistent wallpaper:

- **World:** today's TopBar, with the clock, the world pills, search, and the account/Stremio/system controls. It slims to 64 px once the page scrolls past 40 px. The tab row pins beneath it.
- **Detail:**
  - Left slot: a Back pill that names its destination: "‹ Theatre", "‹ Search · one piece", "‹ Magic", "‹ Home".
  - Centre: the compact world pills.
  - Right: search, account and the system menu.
  - Used by title pages, search, genre and See-all pages, and Continue See-all.
- **Utility:** the Detail frame plus a kicker ("Colosseum · Store"). Used by Vault, Extensions, Settings, Downloads, the Keyboard Guide and Sync.

Detail pages wash the title's backdrop or cover art over the wallpaper, as the manga and genre pages do today. Theatre detail pages stop rendering on black.

### Taskbar

- A 36 px pill, bottom-left.
- It hides after 24 px of downward scroll and returns on any upward scroll, on reaching the page end, or on a key that targets it.
- Pages reserve bottom padding equal to the pill plus 16 px, so no control can sit beneath it.
- Expanded, each icon shows a text label.

## Search

One search surface, Siaran's design, with a scope set by where it opens:

- **Field:** Fraunces, 22 px, 72 px tall. It carries the gold ring while focused.
- **Scope line** under the field: "Searching Theatre · Esc closes" in a world, "Searching Tankoban, Biblio and Theatre · Esc closes" on Home.
- **Top Match:** one focusable card sized to its content, with cover, kind, year, synopsis and one line of availability.
- **Results:** rails grouped by kind, at gallery size.
  - Theatre: Series, Movies.
  - Tankoban: Manga, Comics.
  - Biblio: Books, Audiobooks, plus Biblio's existing series and alternate-source results.
  - Home: one group of rails per world, each headed by the world's name.
- **Empty field:** recent searches with remove, "Try a genre" chips and Surprise me. These already exist.
- **Keys:**
  - `↓` from the field enters the Top Match.
  - `Enter` on a focused result opens that result.
  - `Enter` in the field opens the Top Match, which the hint strip names ("Enter · open One Piece"). `Enter` does nothing until the Top Match exists.
  - `Esc` in a non-empty field clears it. `Esc` in an empty field closes search.
  - Typed characters always return focus to the field.
- **Back from a result** returns to search with the query, results, scroll and focused result restored.
- **Frame:** search uses the Detail frame. The wallpaper stays, under a 90% dusk wash.
- **Code:** `SearchSurface` and `BiblioSearch` converge on one component. The window-wide `Enter` `Shortcut` is removed.

## Page specifications

### Home

- The widgets stay: Universe hero, Tankoban fan, Theatre film strip, Biblio reading desk, Vault door.
- A search pill sits beside the clock: "Search · or just start typing". The Update glyph stays.
- `↓` moves widget to widget, parking each widget whole under the top bar. `←` and `→` move within a widget.
- Home's first focus is the Universe hero's primary action. It is visible because of R1.

### World shell (Theatre, Tankoban, Biblio)

- Order: Featured (with a placeholder at its final size), Next Up, Continue, the tab row (which pins), then tab content.
- Entering a world focuses Featured's primary action. `↓` follows the order above and never enters the top bar.
- Discover:
  - The wall joins the page scroll at gallery size.
  - The NOW BROWSING picker, the lens (Movie/Series, Manga/Comics, Books) and the filter form the wall's header. That header parks when the wall takes focus.
  - Biblio's wall spans the full content width.
- Library tabs:
  - The ledger counts remain filters.
  - The filter bar is split into labelled groups: Sort · Type · Status in Theatre, and the equivalent groups in Tankoban and Biblio. No two chips share a label.
  - The library search stays.
- Tankoban "Your Collection" tiles without covers show the title set in type instead of a black card with a speech-bubble icon.

### Theatre series page

- Detail frame with the backdrop washed in.
- Title, facts (they wrap and are never clipped), synopsis with "More", and cast (photos when available, initials otherwise).
- Actions, unchanged: Watch/Resume S·E, Library, Ratings & Reviews, Notifications.
- Initial focus is the primary action. `↓` goes to the episode ledger, landing on the Next Up or last-watched episode.
- Episodes: see "Long lists".

### Theatre movie page

- Detail frame with the backdrop washed in, facts that wrap, synopsis with "More", cast, and the More Like This rail. Actions are unchanged.

### Manga series page

- Detail frame. The existing masthead stays: title, Library bookmark, facts, synopsis, the Tankoban Mode / Chapter Mode switch and the language selector.
- Ratings & Reviews moves so the mode switch no longer hides it.
- **Tankoban Mode:**
  - The carousel stays, and opens on the volume being read.
  - The first card aligns to the left content margin, not the screen centre.
  - Wheel acceleration:
    - One notch steps one volume.
    - Notches less than 90 ms apart double the step each time (1, 2, 4, 8), capped at 10.
    - The step resets after 250 ms without a notch.
    - `Shift`+wheel steps 10.
    - Touchpad pixel deltas map to steps at one volume per 120 px, with the same acceleration.
  - The docked "Vol. N · Download · Read Vol. N" bar and Select mode are unchanged.
- **Chapter Mode:** see "Long lists".

### Comics series page (`ComicSeries`)

- Detail frame with the banner hero washed in.
- The filter and sort bar parks as a section header.
- The release table becomes a ledger (see "Long lists"): collections group first, single issues after. Read and Download show on the focused row.
- The page's own Back and window controls are removed in favour of the frame.

### Biblio book page

- Detail frame, replacing the grey glass bar. The cover and Biblio's drop-cap synopsis stay.
- The primary button states the truth: "Read", "Continue · p. N", or "Get this book".
- The torrent list moves into a collapsed "Sources · N" disclosure. Owned editions are listed first.
- `Enter` on the primary button opens the reader at your place. Back from the reader returns to the book page with focus on the primary button.

### Genre, See-all and Continue See-all pages

- These take the Detail frame. Genre pages keep their collage hero, chips and "Most read" cards unchanged.

### Vault

- Utility frame.
- The folder rail remains an independent pane. `←` and `→` cross between the rail and the grid.
- The existing `/` and `Ctrl+F` in-Vault search behaviour is kept.
- The hero cross-fade draws one title at a time.
- Keep: filter and sort, Identify, the context menu, and the Continue and Next Up rails.

### Extensions

- Utility frame.
- The pane tabs become world-tab-style pills.
- A focused well reorders with `Alt+↑` and `Alt+↓`, matching the mouse arrows.
- Keep: the Sources order view, Browse (Top / New / Rising), Installed, Configure, Remove, the toggles and Install from a link.

### Settings

- Utility frame.
- Adds these preferences, which are currently scattered or missing: wallpaper, hint strip on/off, reduced motion, default world on launch, and account and device.
- Keep: Explicit Content, and Ratings & Reviews conversion.

### Downloads

- Utility frame.
- Cards without covers show the title set in type instead of flat colour.
- Keep: the per-world shelves, arriving-play and Read actions.

### Keyboard Guide

- Opens as a sheet over the current page, listing that surface's real bindings. Back closes it and returns focus.

## Long lists

This section covers the Theatre episode ledger, manga Chapter Mode and comic issues. All three share one ledger component.

- **Sticky bar:** one line, 56 px tall. It holds:
  - Order (Absolute | Seasons, shown only when the absolute mapping is complete, as today).
  - The season picker (a dropdown past ten seasons, as today).
  - "Go to" with a number field.
  - The count.
  - A "Next: N" jump.
  - The heading names the current range truthfully. In Absolute order it never says "Season 1".
- **Rows:** 96 px tall. Each has:
  - A 128×72 still or thumbnail.
  - The number and the title.
  - A two-line synopsis where one exists.
  - A progress bar.
  - Status shown only when it differs from the default: Downloaded, Watched, Unaired or Downloading.
  - Play/Read and Download on the right.
  - Specials stay pinned last.
- **Range strip:** on the right edge, labelled 1–100 · 101–200 and so on.
  - The mouse clicks or drags it.
  - `PgUp` and `PgDn` move one screen; `Home` and `End` go to the ends.
  - Typing digits while the ledger is focused opens Go-to with those digits.
- **Opening position:** the Next Up or last-used row is focused and parked.
- **Images:**
  - Load only for rows that remain on screen for 150 ms. Requests for rows scrolled past are cancelled.
  - Decode at display size.
  - Persist on disk after first load.
  - Show the number, set in type, until the image arrives.
  - The same fix applies to Theatre episode stills. Today they decode at full resolution (`TheatreSeries.qml` sets no `sourceSize`) and have no disk cache.
- **Chapter Mode gate:** the continuous list replaces the pages of ten only when all of these hold on Hemanth's connection, measured with One Piece (about 1,195 chapters):
  - After scrolling stops anywhere, 90% of visible thumbnails appear within 2 s, and all of them within 5 s.
  - A fast fling from chapter 1 to chapter 900 issues no thumbnail requests for the rows passed.
  - Returning to a chapter already viewed shows its thumbnail on first paint.

  Until the gate passes, Chapter Mode keeps the pages of ten, but adopts the sticky bar, the row design and a "Go to chapter" field.
- **Chapter grouping:** chapter headings use numeric ranges ("Chapters 1–10"). There is never volume grouping.

## Journeys

- **First use (cold shell):** Home is visible and its first focus is the Universe hero's primary action, with the gold ring. The Keyboard Guide is reachable from the taskbar and from `?` in the hint strip.
- **Normal use:**
  1. Type "one" anywhere. Search opens with "one".
  2. Type "piece". Press `↓` to reach the Top Match, then `Enter`. The series page opens on the Next Up episode.
  3. Press Back to return to the results, with the query intact. Press Back to return to where search was opened.
  4. The world pills switch worlds from any of these pages.
- **Interruption:** a world switch, a player session, or a reader session. Returning restores the prior page, scroll and focus (R6, R7).
- **Recovery:**
  - A slow catalogue shows skeletons at final geometry.
  - A failed load shows a line explaining what failed and a Retry button, and focus goes to Retry.
  - Search with no results offers "Try a genre".
- **Completion:** Back from any depth climbs to Home's top and stops. Quit is explicit and confirmed.

## Acceptance criteria

Each criterion is observable in a Lanista session on the assembled app with a real-profile seed.

1. From a cold Theatre, `↓` pressed 20 times never focuses a TopBar or system control. Every focused item is fully visible, and `automationFocusedObjectFullyVisible` is true after each press.
2. The focused item shows the R1 ring. A pixel check of the focus frame finds 3 px of `#f0c44a` at full opacity.
3. Hover: moving the pointer onto tile B while tile A holds focus moves focus to B, and exactly one highlighted item exists.
4. Discover, Library and search grids have no nested scrollers. A wheel scroll anywhere moves the single page scroller.
5. After scrolling Theatre Discover 3,000 px, a tab pill in the pinned tab row is visible and clickable.
6. Back ladder: content → tab row → Featured → Home → Home top. A further Back leaves the app running.
7. Search → result → Back restores the same query, results and focused result. This holds in Theatre, Tankoban, Biblio and on Home.
8. Home search returns grouped results for Tankoban, Biblio and Theatre, and nothing from Siaran.
9. The world pills are visible on every page type in scope, and absent in the three readers and the player.
10. The taskbar pill never overlaps an interactive element on any page type in scope. It hides on scroll-down and returns on scroll-up.
11. No layout shift: the Featured carousel's bounding box does not move between first paint and loaded.
12. The TopBar clock is correct on its first captured frame.
13. The episode ledger shows at least 7 rows at 1080 p. Go to episode 1,071 focuses and parks row 1,071.
14. Carousel: a continuous fast wheel flick of 2 s or less reaches a volume numbered 80 or higher, starting from Vol. 1 of One Piece.
15. Chapter Mode: the continuous list ships only when the thumbnail gate above passes.
16. The Keyboard Guide lists no binding that fails on the surface it describes.

Performance runs as a parallel track, following the profiling plan already on record: QML compilation, one shared pre-blurred wallpaper, per-frame bindings, nested scrollers and retained worlds. The Tankoban Comics stall of more than 10 s is reproduced and explained there. The proposed target for scroll and focus segments is under 5% of frames over 20 ms on Hemanth's machine; the profiling run confirms or revises it.

## Phasing

1. **World shell**, drawing on whichever parts of the Preflight prototype meet the contract. R1–R5, R7, R9–R11 in `WorldPage`, `KeyboardAction`, the tab bars and Discover/Library. Theatre first, then Tankoban and Biblio.
2. **Back and search:** the R6 policy and return points, the unified search component, Home search and type-anywhere.
3. **Shared frame:** the Detail and Utility densities and the taskbar pill, applied to every title and utility page.
4. **Long lists:** the ledger component for episodes and comic issues, and image loading. Chapter Mode's continuous list follows once the thumbnail gate passes. Carousel acceleration.
5. **Polish:**
   - Overlaps: the manga header, the Vault hero and the utility kicker.
   - Settings additions.
   - Downloads covers.
   - An honest Keyboard Guide.

## Capabilities that must survive

- **Episodes:**
  - Absolute and Seasons order.
  - Go to episode.
  - Per-episode progress.
  - Next Up, above Continue.
  - Visible descriptions.
  - Per-episode Play and Download, and full-season download.
  - The season dropdown for more than ten seasons.
  - Specials last.
- **Manga and comics:**
  - Tankoban Mode and Chapter Mode.
  - Language selection.
  - Volume select and download.
  - Chapter-page download (becoming range download once the list is continuous).
  - Collections before issues.
  - Resume centring.
- **Worlds:**
  - The NOW BROWSING picker, lenses and filters.
  - Customize rows and Customize shelves.
  - See all.
  - Genre pages and the genre index.
  - Top 10 numerals.
- **Libraries:**
  - Ledger counts as filters.
  - Sort, type, airing and progress filters.
  - Library search.
  - The ⋮ menu: Resume, Details, Mark watched, Remove.
- **Utilities:**
  - Vault: the folder rail, Identify, the context menu and the filters.
  - Extensions: source order, Configure, Remove, reorder, Install from a link.
  - Downloads: the per-world shelves.
  - The Stremio door, Ratings & Reviews, account and device, and wallpaper.

## Discarded alternatives

- **A horizontal episode rail (from the mock).** It fails at One Piece scale and hides descriptions.
- **A volume grid replacing the carousel.** The carousel is a signature feature, and wheel acceleration solves the reach problem.
- **A numbered volume track under the carousel.** It added screen furniture; acceleration does the same job.
- **Chapter headings grouped by volume.** The metadata is unreliable (Locked 8).
- **Home search including Siaran.** It would duplicate every title.
- **A quiet focus aura.** It was proven invisible in the live walk.

## Evidence

- The feel audit page, 2026-09-24: screenshots, findings and the ranked changes.
- Reference prototypes: the Siaran TV-OS prototype and the Theatre halfway mock, both in the Brotherhood workspace.
- The Lanista sessions from 2026-09-24 (tags `designwalkA` through `designwalkH`), under `artifacts/lanista-sessions/`. This path is local and untracked.
