# Vinyl — Song page and Album page design

Date: 2026-10-07
Arc: 62 Vinyl
Status: brainstorm locked by Hemanth 2026-10-07; specification under his review (first review notes applied the same day)
Workflow: `brotherhood-brainstorming` (Claude runtime)
Author: Claude (Opus 5.5)

## 0. How to read this

Three kinds of statement appear below, and each is marked.

- **[H]** — decided by Hemanth during the brainstorm.
- **[C]** — proposed by Claude and shown to Hemanth in the closing ledger; he locked without objecting.
- **[P]** — not decided in the brainstorm. Claude's default for the planner, to be confirmed, never to be treated as a product ruling.

Mocks:

- Song page: `Preflight-Architect/arcs/62-vinyl/prototypes/62-vinyl-listening-room-thin-data/song-page.html`
- Album page: `Preflight-Architect/arcs/62-vinyl/prototypes/62-vinyl-album-page/index.html` (built by Dots with Hemanth, version 3)

Where a mock and this text disagree about look, spacing or proportion, the mock wins. Where this text states a behaviour a mock does not show, this text wins. Section 9 lists every place the mocks are known to differ from this text.

This specification replaces the "Current thin-data mock direction" section of `Preflight-Architect/arcs/62-vinyl/experience/DESKTOP-LISTENING-ROOM.md` for these two screens. The earlier three-pane "Listening Room" thesis in that document was rejected by Hemanth on 2026-10-06.

## 1. Promise and scope

**Promise.** Put a record on and stay with it. The song page lets you sink into the song that is playing: its words, what they mean, its video. The album page lets you take in the record as an object: its tracks, who made it, its editions, what surrounds it.

**In scope.** Two screens and the bottom player bar's behaviour on them.

**Out of scope.** Vinyl Home, Discover, Library, search, the artist page, the queue drawer, the empty state of the player bar.

**Design target.** The multi-catalogue Grand Music Database, not the first catalogue. **[H]** What data exists first limits what ships first; it does not change this design.

## 2. Shared house rules

- One calm vertical composition per screen. Text sits straight on the blurred backdrop; no boxes, cards or panels around text. **[H]** ("everything bleeding in")
- The backdrop is the album's cover, blurred and dimmed. With no artwork, it is Colosseum's own wallpaper. **[H]**
- Colours and type come only from `Colosseum/qml/Theme.qml`. Artwork and country flags are the only exceptions. **[H]**
- Play, pause, previous, next and the seek bar exist only in the bottom player bar. **[H]**
- No artist photographs anywhere. **[H]**
- Something a song or album lacks is shown at most as a shaded control. No sentence, badge or panel explains absence. **[H]**
- No descriptive sub-line under any heading. No emoji.

## 3. Navigation

- Clicking the song in the bottom bar opens the **song page** for the song that is playing. **[H]** On the album page the whole bar does this: the artwork and any empty part of it, as well as the title. The play, previous, next, seek and tool buttons keep their own jobs. **[H]** (live, 2026-10-07: "when we click the bottom hud controller when in album view it should open the song view")
- Clicking the poster or the album's name opens that song's **album page**. **[H]**
- Clicking a track on the album page plays it and stays on the album page. **[P]** (the album mock already behaves this way)
- The song page always shows the song that is playing. When the song changes, the page changes with it. **[P]**
- The song page has a visible **Back** control, top left, that returns to the album view the user came from. It does not change what is playing or where the song has reached. **[H]** (live, 2026-10-07: "there is no back from the song page")

## 4. Song page

### 4.1 Layout

Three places side by side, and the poster never leaves the middle one. **[H]**

| Left | Middle | Right |
| --- | --- | --- |
| Lyrics | Poster, then song title, then artist | Annotations |

- At rest only the middle is occupied.
- The poster keeps the artwork's own shape: square for an album cover, taller for a film poster. **[H]**
- Lyrics are on the left and annotations on the right. **[C]**
- The side columns are as tall as the poster. Lines fade at the top and bottom of the lyrics column.

### 4.2 Controls in the bar on this page

Left to right: **Lyrics**, **Video**, **Annotations**, a small gap, **Credits**, **Queue**, the quality chip. **[H]** Each button is an icon with its name on hover; the quality chip is text. **[C]**

The first three appear in the bar **only on the song page**. **[H]** One a song lacks is shaded and does nothing. **[H]**

### 4.3 Lyrics

- **Lyrics** shows and hides the lyrics column.
- The current line is the brightest and sits a little above the middle of the column; lines already sung are dimmest.
- Clicking a line jumps the song to that line. **[H]**
- A single click waits a quarter of a second before jumping, so a double-click never moves the song. **[C]**
- A line that has annotations carries a small dot. **[C]**
- Lyrics without timing are shown as plain text: no bright line, no jumping. **[P]**

### 4.4 Annotations

- Double-clicking or right-clicking a line opens the annotations column showing **every** note for that line. **[H]** The line turns gold while its notes are open.
- **Annotations** in the bar switches **auto-annotation** on and off. **[C]** While on, the column stays open and follows the song. **[H]**
- In auto mode one note is shown at a time, set larger. It stays up until the song reaches the next line that has one. **[H]**
- The note chosen for a line is the shortest among those that are well upvoted. **[H]** The artist's own verified note, when one exists, is chosen ahead of it. **[C]** A note too long to read before the next annotated line arrives is not auto-shown; it still appears when the line is opened by hand. **[C]**
- Opening a line by hand while auto mode is on shows that line's full notes; auto mode resumes at the next annotated line. **[P]**
- Turning **Lyrics** off closes the annotations column too. **[P]**

### 4.5 Video

- **Video** plays the official music video where the poster was, in a wider frame. **[H]**
- Switching between audio and video keeps the listener's place in the song. **[H]** Exact musical alignment is not promised; some videos are extended versions. **[H]**
- While the video is up, the lyrics and annotations columns are not shown. **[H]** (Lyrics beside a video is deferred.)

### 4.6 A song with nothing extra

The page is the poster, the title, the artist, and three shaded buttons in the bar. **[H]** Nothing else is added to fill the space.

## 5. Album page

### 5.1 First screen

- Left: the cover, the album title, the artist (a link to the artist page), one plain line of facts — year, type, number of tracks, total length — and three actions: **Play**, **Shuffle**, **Save**. **[H]** for the layout, **[C]** for the facts line and the three actions.
- Right: the track list of the edition being shown. **[H]**
- The track list must be reachable without scrolling. **[H]**
- While a track list is longer than the screen, the left column stays in view beside it and scrolls away once the list ends. **[C]**

### 5.2 Track rows

- Number, title, length.
- The track's own artists on a second line, **only when they differ from the album's artist**. **[H]**
- The playing track has a gold number, a bright title, and a thin gold line in its row that advances with the song. **[H]**
- An edition with more than one disc has a small disc switch above the list and shows one disc at a time. **[C]**

### 5.3 Sections below, in this order

**[H]** for the six sections; **[C]** for the order.

1. **About** — a short paragraph about the album.
2. **Credits** — role label above names, three columns on a wide window. The main roles (production, writing, engineering) show first; **all credits** opens the rest in place. **[H]** Names are links.
3. **Videos** — a rail of the album's official videos, each a still and the song's title. Choosing one plays that song and opens the song page with the video up. **[P]**
4. **Editions** — every version of the album together (original, reissues, anniversary and deluxe editions), each with its own small cover, name, year, label, country, format and track count. **[H]** Listed by year, original first. **[H]** The one being shown is marked. A long list shows the first few, then **all editions**. **[C]**
5. **More by this artist** — a rail of the artist's other albums: cover, title, year.
6. **Information** — label above value: released, label, catalogue number, barcode, country, format, tracks, length, for the edition being shown. The barcode is drawn as a barcode with its digits beneath; the catalogue number is set large; the country has its flag, or a plain globe mark where a place has no flag. **[H]** A field the edition lacks is left out.

### 5.4 Choosing an edition

Choosing an edition swaps the title, cover, backdrop, facts line, track list, About and Information to that edition, in place, and returns to the top. **[C]**

The same song on two editions is two different entries, as YouTube Music treats it. **[H]** (review, 2026-10-07) So:

- a song already playing keeps playing when the edition on screen changes;
- its row is marked as playing only on the edition it is playing from;
- a play is recorded in history against the edition it came from.

For recommendations it is the opposite: every edition of an album counts as **one album**, and the same recording on two editions as **one song**. **[H]** (review, 2026-10-07) History shows the edition; taste does not split on it.

For the planner: every track queued from an album page carries its edition. The listening history contract already stores the edition with each play when the queue entry has one (`ListenContext` holds the queue occurrence and its release context), so this needs no backend change.

### 5.5 Sidebar index

- The album page has a sidebar index of its six sections, in the same manner as the app's other sidebars. **[H]**
- It is the app's existing component, not new code: `Colosseum/qml/RowIndexSidebar.qml`, the table of contents already used by Theatre (`TheatreCatalogPage`), Biblio (`BiblioExplorePage`) and Tankoban (`TankobanComicsTab`, `TankobanMangaTab`). **[H]** (review, 2026-10-07) The album page supplies its six sections as the rows.
- Each row carries its own icon, as the app's other sidebars do. **[H]** (live, 2026-10-07: "the index sidebar in the album pages needs icons brother. just like the main app")
- Its look and behaviour come with it: the collapsed rail that opens, the glass, marking the row in view, scrolling to a chosen row, keyboard reach, and leaving out a row whose section is absent. The plain text list in the album mock is a stand-in for it.
- It appears once the reader has scrolled past the first screen, so the first screen remains cover and tracks. **[C]** The component already supports being placed in the page's flow and pinning once reached (`flowHost`).

### 5.6 Controls in the bar on this page

**Queue** and the quality chip only. **[C]** (On the song page, **Credits** opens a popup above the bar. The popup keeps one fixed size however many credits there are, and scrolls. **[H]** live, 2026-10-07: "when there are too many credits in the credits sections, we don't increase the pop us size") No Lyrics, Video or Annotations. **[H]** The bar's buttons are never used as a page index. **[H]** (He chose the sidebar over that.)

## 6. States, interruptions and recovery

- **Waiting for a source.** The play button in the bar becomes a spinner and the quality chip becomes two plain words ("Finding a source"). Nothing on either page moves or explains. **[C]** (Claude's 2026-10-06 mock; Hemanth kept these states through his own revisions of it)
- **A source fails mid-song.** The same spinner; the seek bar holds its place; the chip reads "Trying another source". **[C]** Trying the next source automatically is **[H]**; resuming at the same point is **[C]**.
- **The song ends while notes are open.** The annotations column follows the new song: closed if it was opened by hand, still open if auto mode is on. **[P]**
- **Lyrics or annotations arrive after the page is open.** The shaded button becomes live. Nothing opens by itself. **[P]**
- **No artwork.** Backdrop is the Colosseum wallpaper **[H]**; where the cover would be, the album's name is set on a plain sleeve. **[C]**
- **Leaving and returning.** Whether the lyrics column and auto-annotation are on is remembered for the session. **[P]**

## 7. What the screens need from the backend

Only what makes the contract implementable. Contract names refer to `Preflight-Architect/arcs/62-vinyl/backend/`.

| Need | Where it comes from | Gap found while writing this |
| --- | --- | --- |
| Playing song, position, waiting and failure states | BA2-C transition facts (Wave 4, slice B) | none known |
| Timed lyric lines, click-to-jump target | BC1 | none known |
| Notes per lyric line, lazy bodies | BC3 | the picker needs each note's vote count and an artist-verified flag; whether BC3 carries them is unchecked |
| Official video for a song, switch at same position | BF2, BF3, playback window (Wave 4, slice A) | none known |
| Album, editions, discs, tracks, per-track artist credit | BG1 | joining separately filed versions into one Editions list is Grand Music Database work |
| Credits by role | BF1 | "main roles first" needs an ordering of roles; none exists |
| Label, catalogue number, format per edition | BG1 `Release` | `Release` carries barcode and country but not label or catalogue number |
| Saved state for **Save** | BA5 | BA5 saves release groups, not a specific edition |
| Editions counting as one album for recommendations | BE1, BE derivation, BE3 | already so by construction: those contracts know only Recording, Release Group and Artist, never an edition. It holds only as far as the catalogue groups editions into one album; where an anniversary edition is filed as its own album it counts as a second album until the Grand Music Database joins them. That join is the same one the Editions section needs. A remaster filed as a separate recording likewise counts as a different song until linked. |

## 8. Acceptance

Observable on the finished screens.

1. From any Vinyl page, clicking the song in the bar opens its song page; clicking the poster there opens the album page.
2. On the song page with lyrics off, only the poster, title and artist are in the room.
3. With lyrics on, the poster has not moved or changed, and the lyrics are to its left.
4. A single click on a line moves the seek bar to that line. A double-click or right-click on a dotted line opens that line's notes on the right and does not move the seek bar.
5. With auto-annotation on, the note on the right changes only when the song reaches a line that has notes.
6. For a song with no lyrics, video or annotations, the three buttons are shaded and the page shows no message about it.
7. The video replaces the poster and resumes from the same position the audio had reached.
8. On the album page, the first track is visible without scrolling at 1280x720 and at 1600x900.
9. A track credited to someone other than the album's artist shows that credit; a track credited to the album's artist shows none.
10. The playing track's row has a progress line that moves in step with the bar's seek bar.
11. The six sections appear in the stated order, with no box around any text.
12. Choosing an edition changes the cover, backdrop, track list, facts and Information, and returns to the top.
13. Editions are in year order.
14. The sidebar index is absent on the first screen and present below it.
15. On the album page the bar has no Lyrics, Video or Annotations button. On the song page it has all three.
16. Nowhere on either page is there a second set of playback controls (pause, previous, next) or a second seek bar, and no artist photograph. The album page's own **Play**, **Shuffle** and **Save** (section 5.1) are not a second set: they start the album, they do not control the song.
17. The song page has a visible Back control that returns to the album view without changing playback.
18. On the album page, clicking the bar's artwork or empty surface opens the song page; clicking Play or Next there does not.
19. The Credits popup is the same size with six credit groups as with thirty.

## 9. Known differences between the mocks and this text

Prototype (Colosseum branch `dots/vinyl-pages-qml-prototype` at `810202b1`, reviewed 2026-10-07), not yet built:
- the globe mark for a country without a flag (section 5.3); only the United States flag is drawn.
- the no-artwork state (section 6): wallpaper backdrop and the plain sleeve.
- the poster's shape follows a fixture's name, not the artwork's own proportions (section 4.1).


To be brought into line in the next mock pass.

Album page mock (version 3):
- (version 3 only; fixed in version 4) the bar still shows Lyrics, Video, Annotations and Credits. Text: section 5.6.
- version 4 has a plain text index beside the sections; the real one is the app's `RowIndexSidebar`. Text: section 5.5.
- (version 3 only; fixed in version 4) Credits has no **all credits**. Text: section 5.3.
- only the United States flag is drawn; version 4 adds the globe mark for places without one. Text: section 5.3.
- Editions has four entries and no **all editions**. Text: section 5.3.
- every fact on the page is unverified; Dots could not reach MusicBrainz.

Song page mock:
- lyric lines are grey bars and the notes are short stand-ins written for the mock; no lyric or annotation text is reproduced.
- playback is simulated.

## 10. Non-goals

- No panel, list or sentence about what a song or album lacks.
- No second transport anywhere.
- No per-row singers on the song page; no track list on the song page.
- No genre pills, ratings, awards or "more like this".
- No autoplay or radio controls on either page.

## 11. Deferred, with reasons

- **Related live performances.** Shape already decided by Hemanth: found by a YouTube search ranked by our own algorithm, offered as a list or drop-down, with its own Continue Watching in Vinyl and deliberately loose sync. Not an instant switch. Later version.
- **Lyrics beside a playing video**, and tuning the player for Vinyl specifically. Later version.
- **Radio, autoplay, recommendation surfaces.** Separate work.
- **Search ordering of an album's separately listed versions.** Belongs to the search design.
- **How likes and play counts are displayed across editions.** Settled for the algorithm: editions collapse into one album and one song (section 5.4). Still to design in the history and stats screens: whether a like made on one edition is shown on the other, and whether "most played" adds editions together on screen. Claude's recommendation: yes to both, to match what the algorithm does.
- **Artist page, Home, Discover, Library.** Not part of this brainstorm.

## 12. Discarded alternatives

- **One page for both song and album** (Claude). Rejected by Hemanth: video, lyrics and annotations are song-level and deserve their own screen, and the album page needs the room for rich detail.
- **Lyrics opening inside the playing track's row** (Claude). Fell with the one-page idea.
- **Three boxed columns with source panel, edition switch and play count on the song page** (Claude, first mock). Rejected as too busy.
- **"Up next" as the song page's always-present content** (Claude). Not taken; a thin song's page stays bare.
- **Lyrics drawn over a frosted poster** (Claude). Replaced: lyrics go beside the poster.
- **Holding a line to open notes.** Replaced by double-click and right-click.
- **The three song buttons under the song title** (Claude). Replaced: they live in the bar.
- **The bar's buttons as a page index on the album page.** Replaced by the sidebar index, to match the rest of the app.
- **Artist photograph, or a collage of other covers, when an album has no artwork** (Claude). Rejected: extra work, and artist images are not wallpapers.
- **Editions ordered by "different track list first"** (Claude). Rejected as over-engineering a simple list.
- **A Harbor-style full-width hero with tracks below the fold.** Not taken: an album has many things to press, so the track list stays on the first screen.

## 13. Self-review

- Every Locked item from the closing ledger appears in sections 2 to 6. Every Constraint is in section 2. Every Deferred item is in section 11 and nowhere else.
- No behaviour is stated without an **[H]**, **[C]** or **[P]** mark. The **[P]** items are the ones the brainstorm did not reach; they are defaults for the planner and need Hemanth's confirmation before they are treated as rulings.
- Flows cover first arrival, normal use, interruption (source loss, song change, edition change), recovery, and the bare case.
- Acceptance items are observable without reading code.
- Section 7 names backend gaps and does not design their fixes.
- Not covered, by scope: keyboard reach. The app's keyboard contract is owned elsewhere; the plan must bring both screens under it.
