# Vinyl combined catalogue — design

Status: approved shape (Hemanth, 2026-11-27). Author: Agent 0 (Claude). Evidence:
`Brotherhood/agents/vinyl-combined-catalogue-providers-20261127.md` (live probes, 24 English artists).

## Promise
Open Vinyl's Discover and browse **one** music catalogue — the best of every service, ranked by Colosseum, with a
plain reason on each card — without knowing or choosing which service it came from. The same idea as Tankoyomi,
applied to the catalogue instead of download sources.

## Scope (first version)
- Discover's per-service sources are **replaced** by one source, **Vinyl** (Hemanth: option B).
- Colosseum computes the ranking from service evidence (the Brainz rule, 2026-10-07): services say what is popular,
  trending, new or related; Vinyl decides the order and writes the reason.
- Artist pages gain **Essentials** (most-played songs and albums from ListenBrainz popularity).
- One app-wide **identity memory** (MusicBrainz IDs) replaces per-request MusicBrainz lookups.
- Out of scope now: a configuration page (Hemanth: only if needed, later), Home changes, a shared Cloudflare Worker,
  personal listening statistics, mood mixes, reviews, ReccoBeats/FreqBlog/MusicAtlas.

## Inclusion rule and evidence (Hemanth, 2026-11-27)
A service joins only if it **adds** to the catalogue (artists, albums, genres, metadata) beyond the base. Base =
**YouTube Music** (ruling R17, 2026-10-07: Vinyl's main catalogue). MusicBrainz is **enrichment only** (R18): used
where it matches, never awaited, never required. Measured 2026-11-27 (`C:\b\vinyl-s0-temp\discovery\coverage.json`):
| Service | What it adds |
|---|---|
| YouTube Music | Base: official discographies, every reissue/compilation (Miles Davis 1,685 entries), playback |
| Apple RSS | Current hits: 74 of Apple's top 100 are absent from Last.fm's top 1,000 -> Trending |
| MusicBrainz | Release dates and album types (Last.fm has neither); 24-66 extra albums per artist; 2,227 curated genres; rock-tagged albums 372,451 vs Last.fm 44,820. Search can pick the wrong release, so match by artist MBID + title + track count, never top hit. Throttled (503) - non-blocking |
| Last.fm | Long-run popularity (listeners/playcount), genre tags, album description |
| ListenBrainz | Per-artist most-played (Essentials), similar artists |
| TheAudioDB | Album mood, genre/style, art, year (no descriptions on the free key) |
| JioSaavn | Indian-language depth (5 Nov probe) |
Excluded: ReccoBeats (junk similarity), FreqBlog, MusicAtlas (key quotas).

**Kworb is the chart layer** (Hemanth, 2026-11-27: "THE aggregator of charts"). kworb.net aggregates iTunes,
Spotify, Apple Music, YouTube, Shazam and Deezer charts per country for songs, albums and artists, plus artist
rankings and Spotify all-time stream totals. It has **no genre charts**. Static HTML, no API; its FAQ invites using
the data and says pages are static so spikes can't overload the server; robots.txt allows all. Use: each install
reads a small fixed set of pages (global + the user's country, per service, songs/albums/artists) **at most once a
day**, parsed in the provider process, cached 24 h; a shared Worker cache can replace this later. Kworb's earlier
rejection (5 Nov) was our prototype's serial YouTube matching before display, not Kworb's data; cards now show first
and YouTube resolves on tap. Kworb supersedes the separate YouTube/Apple chart fetches below, which remain only as a
fallback when Kworb is unreachable or its page layout changes (parser fails closed, logs once).

**YouTube Charts and Kworb do different jobs** (Hemanth asked "kworbs or this?", 2026-11-27): YouTube Charts (via
ytmusicapi `get_charts`, global + per country: top songs / top videos playlists, top 40 artists; the charts.youtube.com
site itself is reCAPTCHA-guarded, do not scrape it) is the YouTube side — official and instantly playable with no
matching. Kworb supplies every other service (Spotify, Apple Music, iTunes, Shazam, Deezer). YouTube alone skews to
India/Latin America (its global #1 song and artist on 2026-11-27 were Guzarish and Alka Yagnik); "Popular worldwide"
needs both. "Biggest movers" / "Highest debut" are computed by Vinyl from stored week-over-week snapshots.

**Vinyl's own genre charts, Billboard-style** (Hemanth pointed at billboard.com's genre charts as "what we wanted",
2026-11-27). Billboard is not a source (no API; terms forbid reuse). Vinyl computes: per genre (Pop, Hip-hop, Rock &
Alternative, Country, R&B, Latin, Afrobeats, Electronic, Metal, Jazz, ...), songs ranked by combined chart strength from
YouTube Charts + Kworb positions, genre from Last.fm/MusicBrainz tags; each entry shows last week, peak, weeks at #1,
weeks on chart, from Vinyl's own weekly snapshots (columns fill in as history accrues; first weeks show "New").
"Greatest of all time" per genre from long-run listening (ListenBrainz/Last.fm playcounts, Spotify totals via Kworb).
Surfaced in Discover as a **Charts** entry in the List filter alongside Popular/Trending/New/Essentials.

**Billboard via Wikipedia, never billboard.com** (Hemanth: "prove we can reliably use billboard to enrich our filters
and explore's editorial rows", 2026-11-27). Proven: billboard.com's terms (pmc.com/terms-of-use) forbid robots,
crawlers, scraping or aggregating its content or data, reproduction and derivative works, and limit use to personal
non-commercial; its robots.txt blocks AI agents by name (ClaudeBot, anthropic-ai, GPTBot, ...). Do not fetch
billboard.com. Chart facts are taken from Wikipedia's sourced chart lists via the official MediaWiki API (CC BY-SA,
attribute Wikipedia), proven live: "List of Billboard Hot 100 number ones of 2025" (12 #1s), "Billboard Year-End Hot
100 singles of 2025" (full 100), "List of Billboard Global 200 number ones of 2025", "List of Billboard Mainstream
Rock number-one songs of the 2020s" (144 songs). Exact page titles vary by chart: discover them via the API's
category/list pages, never guess. Uses: (1) filters — "Billboard #1s", "Year-end top 100", per-genre #1s by year or
decade (Rock, Country, Rap, Dance, Latin, ...); (2) Explore editorial rows — "Every #1 of 2025", "The year-end 100",
"Rock's #1s this decade", "#1 hits of the '90s". Lists refresh weekly; entries resolve to YouTube on tap.

Charts (Hemanth's links, 2026-11-27, now fallback): **YouTube Charts global** (top songs/videos playlists + 40 top artists via
ytmusicapi `get_charts("ZZ")`, no key) and a **Vinyl global chart** computed from Apple's free per-country RSS feeds
(US, GB, CA, AU, IN, ...; Apple's "Top 100: Global" playlist needs the paid Apple Music API, and the RSS has no global
feed) feed Popular/Trending. **Billboard Global 200 is excluded**: no API, terms forbid scraping and reuse. Charts
cover only the top ~100-200 songs, mostly pop/hip-hop, so genre depth, New and Essentials still come from Last.fm tags,
MusicBrainz and ListenBrainz.

## What the user sees
Discover keeps today's house browser layout (left rail, Songs · Albums · Artists tabs, filter row, card grid,
"Now browsing" corner):
- The rail shows **one** source, "Vinyl". The corner reads "Now browsing **Vinyl**" with the lens as its subtitle
  (e.g. "Popular").
- Filter row, in this order: **List** (Popular · Trending · New · Essentials), **Genre** (today's genre list, plus
  "All"), **Language** (All, English, and today's Indian languages: Tamil, Telugu, Malayalam, Kannada, Hindi, Punjabi,
  Bengali, Marathi).
- Each card keeps today's caption and gains one reason line in plain words, e.g. "Top 10 on 3 charts this week",
  "Rising fast this week", "Out this month", "A classic of metal". Never a service name, never "#7 on Apple".
- Tapping a card behaves exactly as today (song plays via YouTube resolve on tap; album/artist open their pages).
- Artist page: a new **Essentials** shelf (top songs, then top albums) placed with the existing house shelf
  components, above the discography, when the artist has ListenBrainz popularity data; absent otherwise.

## How it works (technical shape for the builder)
Lives in the existing music engine (`scripts/vinyl/provider.py`) and the existing adapter
(`qml/vinyl/VinylMusicDiscoverAdapter.qml`). No new process, no server.

1. **Adapters return evidence, not pages.** Per service, a function returns candidate cards with raw signals:
   - Last.fm: chart rank (global, per genre tag), tag list, listeners/playcount.
   - YouTube Music: trending/new-release rank (existing `get_charts`/`get_explore` paths), playable video id.
   - Apple RSS: most-played rank (existing endpoint).
   - ListenBrainz: per-artist popularity (`/1/popularity/top-recordings-for-artist`,
     `top-release-groups-for-artist`) for Essentials and for de-duplicating canon.
   - JioSaavn: Indian-language charts/editorial (existing `_jio_page`) — used only when a Language filter selects
     an Indian language, or blended at low weight under "All".
   - MusicBrainz: identity, release date, album type, genres — enrichment via the identity memory, never awaited.
   - TheAudioDB: album mood and genre/style (enrichment; existing artist-art integration).
2. **Identity merge.** Candidates merge into one card by, in order: MusicBrainz ID (from the identity memory) → ISRC
   → normalised artist + title (existing `_catalogue_match_title` rules). If unsure, keep two cards rather than merge
   two different recordings.
3. **Ranking per List.**
   - Popular: weighted sum of normalised chart presence across services (more services = stronger), long-run
     playcount (Last.fm/ListenBrainz).
   - Trending: rank movement / presence on "this week" charts (YouTube trending, Apple most-played) outweighs
     long-run popularity.
   - New: release date within ~60 days (albums: MusicBrainz release-group date when known), ranked by Popular score.
   - Essentials: per genre, highest long-run listening (ListenBrainz/Last.fm playcount), older than ~2 years.
   Weights are constants in one place, documented; scores from different services are normalised to rank
   percentiles before combining (never raw scores).
4. **Reason line.** Generated from the strongest evidence of that card (templates, plain English, no service names).
5. **Paging.** Each request asks every contributing service for its next slice (existing per-service cursors kept in
   one composite cursor), merges, ranks, de-duplicates against items already shown, returns 50. Order is stable for
   the same inputs (deterministic tie-break by identity key).
6. **Identity memory.** One local table (artist and recording → MBID, with "not found" entries and their retry time),
   shared by Discover, Home, and artist pages. All MusicBrainz calls go through one app-wide queue at 1 request/second
   with back-off on 503; reads never block the UI thread.
7. **Caching.** Per-service evidence cached with each service's existing TTLs; merged pages cached ~1 hour.

## States and recovery
- First open: "Vinyl" selected, Songs, Popular, Genre All, Language All. Loading uses the house browser's existing
  loading state.
- One or more services fail: show the merged result from the rest; drop their reason lines silently. Only when
  **every** contributing service fails: the existing "Couldn't reach the catalogue. Click to try again." state.
- Missing Last.fm key: Last.fm simply contributes nothing (log once); the error message names the key only in logs.
- Offline: last cached merged pages, marked stale as today.
- Indian language selected: JioSaavn + YouTube evidence only; Genre filter hidden (as today for JioSaavn).

## Acceptance criteria
- Discover shows exactly one source, "Vinyl"; Last.fm and JioSaavn no longer appear as sources.
- For the 24-artist benchmark, Popular/Trending/New/Essentials each return ≥ 50 songs, albums and artists under
  Genre All, with no duplicate recordings on a page and none across the first three pages.
- Every card has a reason line; no reason line contains a service name.
- Killing any single service (test seam) still returns a full first page; killing all shows the retry state.
- No UI-thread work > 50 ms per page in the QML profiler; merging runs in the provider process.
- Artist pages show Essentials for artists with ListenBrainz data (e.g. Adele) and no empty shelf otherwise.
- MusicBrainz is called at most 1/s app-wide; a repeated artist lookup is answered from the identity memory.
- Existing Discover tests updated; new provider tests cover merge, ranking determinism, partial failure and paging.

## Non-goals
Configuration page; Home; a server/Worker; new providers beyond those listed; changing playback resolution.

## Ledger
**Decided by Hemanth:** combine all music catalogue APIs into one Vinyl catalogue like Tankoyomi (2026-11-27);
replace the per-service sources (option B); configuration only if necessary, later.
**Calls by Claude:** merge in-app first, Worker later; partial results when a service is down; merge order MBID → ISRC
→ artist+title, keep two when unsure; plain reason lines with no service names; lenses Popular/Trending/New/
Essentials; Language filter carries the Indian languages (JioSaavn) so they are not lost under option B; artist-page
Essentials from ListenBrainz popularity; one app-wide identity memory and MusicBrainz queue; Home untouched.
**Left out:** configuration page, shared Worker, listening stats, mood mixes, reviews, ReccoBeats, FreqBlog, MusicAtlas.
