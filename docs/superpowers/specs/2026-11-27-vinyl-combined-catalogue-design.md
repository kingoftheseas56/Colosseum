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
   - MusicBrainz: identity only (via the identity memory).
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
