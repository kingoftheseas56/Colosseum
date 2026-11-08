# Vinyl v1 — Home, The Crate, The Spin and Library

**Status:** design locked by Hemanth 2026-11-09 (brotherhood-brainstorming closing gate passed). Spec APPROVED by Hemanth 2026-11-09, including the section 11a assumptions. Plan: docs/superpowers/plans/2026-11-09-vinyl-v1-home-plan.md.
**Author:** Agent 0 (Claude), from the 2026-11-08/09 brainstorm with Hemanth.
**Visual reference (disposable, not authoritative):** Brotherhood repo `agents/vinyl-home-mock/index.html` at commit `9f2b302e` (Look = Widgets, Crate = Square tiles). Written decisions here win over the mock.
**Decision ledger:** `~/.claude/projects/C--Users-Suprabha-Desktop-Brotherhood/memory/vinyl-home-brainstorm-ledger.md`.

## 1. Promise and scope

Vinyl's first screen gets you to music worth hearing in one move. It shows what the world is playing this week and one pick made for you. It puts the music you keep going back to within one click, whether that's a song, an album or an artist. Albums and artists get the same respect as songs, and the catalogue is global.

**In v1:** the Vinyl world shell, the hero carousel, The Crate, The Spin, the Home tab, the Library tab, the cold start for new listeners, the five widgets at one square size, and the merged recommendation source pool that fills them.

**Unchanged in v1:** the Discover tab (Last.fm + JioSaavn, built 2026-11-05), the song, album and artist pages, the player bar and Listening Room, and playback.

**Foundation step (before any of this):** bring Codex snapshot `bb346e35a` onto master. It is the real Vinyl: the recommendation engine, the Last.fm + JioSaavn Discover, the Artist page and the five widgets. GitHub archive branch: `archive/vinyl-discover-lastfm-jiosaavn-20261107`. Master only has an older slice.

## 2. Page structure

Vinyl moves onto the shared house shell (`qml/WorldPage.qml`, house shelves and cards), like Theatre, Tankoban and Biblio. Its self-skinned shell (`qml/vinyl/VinylLiveWorld.qml` / `VinylWorld.qml`) is retired. One vertical scroll, top to bottom:

1. **Hero carousel** (persistent, above the tabs)
2. **The Crate** (persistent, above the tabs)
3. **The Spin** (persistent, above the tabs)
4. **Tab bar:** Home · Discover · Library (Home opens first)
5. **Tab content**

The player bar stays docked at the bottom. Target screen is 1280x720 (Laptop 1; the app gets about 1280x700). At that size the first screen shows the carousel and The Crate, and The Spin starts right below.

## 3. Hero carousel

Four slides, auto-advancing (about 7 s; paused while hovered), with dots to jump between them. Each slide has a large cover (round for an artist), a gold kicker, the title, a meta line, one line of story, two or three actions, and **a list beside it**. Every slide has a list; that's what makes the carousel useful, not decorative. The slide backdrop is a blur of its own artwork.

| Slide | Kicker | Actions | Side list |
|---|---|---|---|
| 1 | #1 album in the world this week | Play album · Open album · Save | Also charting: #2–#5 albums |
| 2 | #1 song in the world this week | Play song · Start radio | Top songs: #2–#5 |
| 3 | #1 artist in the world this week | Artist radio · Open artist | Top artists: #2–#5 (round thumbnails) |
| 4 | For you · picked today | Play · Open · Not for me | Also for you: 4 mixed picks (song, album, artist), each labelled with its type and reason |

- **The world's #1s:**
  - Albums come from Apple's free chart feed (`rss.marketingtools.apple.com/api/v2/<cc>/music/most-played/<n>/albums.json`, checked live 2026-11-09), aggregated across a fixed global set of storefronts (at least US, GB, JP, KR, IN, BR, NG, DE, FR, MX), with rank-weighted merging.
  - Songs and artists come from Last.fm's global charts (`chart.getTopTracks`, `chart.getTopArtists`), cross-checked with Apple's songs feed.
  - Refresh daily; the label stays "this week".
- **For you** is one album, song or artist from the recommendation pool, with a one-line reason. It refreshes as often as the engine can produce a fresh pick: at least daily, and on launch when there's new feedback. "Not for me" sends a hide signal to the existing feedback layer and moves to the next pick.
- **No "continue where you left off" slide** (Hemanth: "let's not waste a slide"). Resuming lives in The Crate.

## 4. The Crate

What you keep going back to: songs, albums, artists, mixes and stations in one grid.

- **Two rows of 8 square tiles** at house card size (about 130 px at 1280 width), with page arrows for more. Every tile is the same square, with the same two caption lines under it.
- **Balanced mix:** listening counts at album and artist level as well as song level (an album played through counts as the album, not 12 songs). Songs can't crowd out albums and artists.
- **Pin anything** to keep it in The Crate; pinned tiles show a gold dot.
- **Album tiles resume** the album at the track you stopped on. The caption says so in gold ("Resume · track 4 of 12").
- **Artist tiles start that artist's radio** on click. A small control in the middle of the record, shown on hover, opens the artist page.
- **Mix and station tiles** play the mix or station.

## 5. The Spin

Picks for right now. One row with a **Songs · Albums · Artists** switch in its header, plus page arrows.

- **Songs:** a paged grid, 4 rows by 3 columns, of compact song rows (small cover, title, artist · album). Mostly familiar songs with a few new ones.
- **Albums:** a row of album widgets, each with a gold reason line ("You stopped at track 2", "Saved, never played", "Close to your city pop").
- **Artists:** a row of artist widgets with reason lines ("Like Mariya Takeuchi", "Your top this month").

## 6. Widgets (locked: they stay)

The five widgets approved 31 Oct (`qml/vinyl/Vinyl{Song,Album,Artist,Mix,Radio}Widget.qml` in `bb346e35a`) stay. New rule: **one square footprint for every widget.** Their natural shapes made the grid uneven (tall songs, small wide radios, a square frame around a round record). Each type stays easy to tell apart at a glance:

| Type | Square form | Caption under it |
|---|---|---|
| Song | Jukebox card: chrome bars top and bottom, metal rails on the sides, the cover filling it, the glass title strip (Fraunces title + ARTIST in caps) over the cover's bottom | Artist · from <album> |
| Album | Jewel case as built (1.1:1, ridged hinge spine on the left, cover on the right behind the lid) | Title + artist, or the resume line |
| Artist | Just the grooved record filling the square, the photo as its 67% centre label; **no glass frame**, so the name moves to the caption | Name + "Artist radio" |
| Mix | A hand-written J-card (mix name in ink, "SIDE A · MADE FOR YOU", tracklist lines) standing behind its cassette | Mix name + "Made for you" |
| Station | Square radio set: handle, picture window on top, speaker, dial and knob below | Station name + "Station" |

Port from the QML's real measurements; never redraw from memory. The mock's `wSongSq`, `wAlbum`, `wArtistSq`, `wMixSq` and `wRadioSq` show the square forms.

## 7. Home tab

Seven rows, each a pick, never a catalogue dump:

1. **Mixes & radio:** personal mix, discovery mix, artist stations and genre stations.
2. **New from your artists:** recent albums and singles, newest first.
3. **Albums for you:** large editorial album cards, each with a kicker and one line of reason.
4. **Because you played <artist or album>:** albums and artists near something you played.
5. **Deep cuts from your artists:** their songs beyond the hits.
6. **Rediscover:** albums and artists you loved but haven't played in a while.
7. **Artists like the ones you love.**

Every card keeps the engine's like/hide feedback badges.

### The four rules that keep the rows full (measured 2026-11-09)

Hemanth required that 5 picked artists plus Last.fm/ListenBrainz fill all seven rows. The live probe (seeds Mariya Takeuchi, Björk, Ilaiyaraaja, Daft Punk, SZA) found:
- 590 merged similar artists;
- 192 unique albums for you;
- 48 albums per "Because you played" seed;
- 90 deep cuts per artist;
- only 7 new releases in 12 months from the 5 seeds alone (27 once their nearest similar artists count);
- Last.fm similar songs empty for 3 of 5 top songs;
- zero similar artists for "Mariya Takeuchi" on both services, but 100 under 竹内まりや.

So:

1. **Every name.** Look each artist up under every MusicBrainz alias, native script included, and merge the results. Otherwise Japanese, Korean and Indian artists silently return nothing.
2. **New from your artists widens** to each of your artists' 10 nearest similar artists when your own artists have too few releases. Releases are dated from MusicBrainz (12-month window).
3. **Mixes and radio come from YouTube Music radio / up-next plus songs by similar artists,** never from Last.fm `track.getSimilar` alone.
4. **Rediscover appears only once there is listening history.** It is hidden on day one.

A row with too few items to fill one screen width is hidden, never shown half-empty.

## 8. Library tab

One calm scroll of rows in the same house cards and widgets as Home, no sub-tabs. Assumed order: **Playlists · Saved albums · Liked songs · Artists you follow · Downloads · Recently played.** Empty rows are hidden. Library holds your saved music and history, so Home carries none of it.

## 9. New listeners (cold start)

Never a gate:
- **The Crate:** shows one optional card, "Pick a few artists you love", which opens the existing taste picker. It fills in as you play and pin.
- **The Spin:** shows global chart picks until Vinyl knows you.
- **For you slide:** uses the picked artists, or falls back to a fifth chart pick (rising artist) until any taste exists.
- **Home tab:** rows that need taste stay hidden until they can fill.

## 10. Sources and the recommendation pool

One merged, de-duplicated catalogue and candidate pool feeds every surface above:
- **YouTube Music** radio / up-next leads stations;
- **Last.fm** (similar artists, tags, charts) leads mixes and discovery;
- **JioSaavn** joins the pool as one catalogue among others;
- **ListenBrainz** (similar artists, top recordings for an artist MBID, no token needed) is the backbone and second opinion;
- **MusicBrainz** resolves identity, aliases and release dates;
- **Apple's chart feed** supplies album charts.

The existing ranker, feedback (like, dislike, hide, snooze, exclude artist), exposure rotation and profile-scoped history layer decides what is shown and played. Sources are global; none is "the Indian source" or "the Japanese source", and the listener's region is never assumed. The Last.fm key never goes into source.

## 11. States and recovery

- **A chart feed fails:** the carousel keeps the last good chart (labelled with its week). If none is cached, the slide is skipped. Three slides are fine.
- **Offline:**
  - the carousel, The Crate and The Spin show cached content;
  - plays that need the network show the existing unavailable state;
  - Library Downloads always works.
- **Missing artwork:** the widgets' built-in placeholders (artist initials on the record label, the case's dark backing).
- **"Not for me" or hide:** the card leaves at once and the row refills from the pool.
- **Pinned item becomes unavailable:** it stays pinned and greyed, and you can unpin it.

## 11a. Claude's assumptions (not explicitly ratified; confirm in review)

- Carousel timing: about 7 s per slide, paused while hovered.
- The storefront list that makes up "the world" for the album chart, and the daily refresh.
- For you's cold-start fallback (a rising-artist chart pick).
- Library row order.
- Rows with too few items are hidden rather than half-filled.
- A pinned item that becomes unavailable stays greyed until unpinned.
- The 12-month window for "new".

## 12. Acceptance criteria

1. Vinyl renders on `WorldPage` with the tab bar Home · Discover · Library; the old Vinyl shell is gone from the route.
2. At 1280x700 the first screen shows the full carousel and The Crate. The Spin's header is visible without scrolling.
3. The carousel has exactly the four slides of §3, each with its side list; no resume slide.
4. The Crate shows 16 square tiles in 2 rows; album tiles resume at the right track; artist tiles start radio; pin and unpin persist across restart.
5. The Spin's switch changes the view between Songs, Albums and Artists without a page jump.
6. All five widgets render at an identical square size; the captions line up across a row.
7. With 5 picked artists and no history, Home shows 6 rows (Rediscover hidden), each at least a screen-width full. This includes a Japanese-script artist among the picks.
8. Library shows its rows in one scroll; empty rows are hidden.
9. A brand-new profile sees no gate; the taste card is optional.
10. With the network off, Vinyl opens to cached content without errors.

## 13. Non-goals and deferred

- **Not in v1:**
  - Albums / Songs / Artists catalogue tabs;
  - endless era × genre lists and their filters;
  - a TMDB/IMDb-style catalogue (Hemanth: "some tmdb shit").
- **Deferred:** native-language-first presentation (Arc 73); universe soundtracks in Vinyl (2.0).
- **Discarded, with reasons:**
  - **Language pills as the organising axis:** language is a filter.
  - **A YouTube-copy, song-first Home:** it doesn't respect albums and artists.
  - **Continue listening above the tabs:** music isn't resumed like a film; The Crate covers return visits.
  - **No carousel:** Hemanth wants a functional "wow" start, like Theatre's.
  - **Wide labelled Crate tiles:** square tiles chosen.
  - **"Clearer cards" instead of widgets:** the widgets stay.
  - **Library merged into Home:** it overloads Home.

## 14. Build order (for the plan, not decided here)

1. Snapshot `bb346e35a` onto master.
2. Re-house Vinyl on `WorldPage` with the three tabs.
3. Merged source pool and the four rules.
4. Carousel with chart feeds and For you.
5. The Crate, The Spin and the square widgets.
6. Home rows and Library.

Each step should be independently testable on Laptop 1 at 1280x700.
