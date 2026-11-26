# Vinyl catalogue sources — lean design

Status: shape decided by Hemanth, 2026-11-27. Author: Agent 0 (Claude). Supersedes the earlier "combined catalogue"
draft of the same day (a cross-source merge layer), which Hemanth rejected as spaghetti: **every source must serve one
function; no source without a job.** Evidence: `Brotherhood/agents/vinyl-combined-catalogue-providers-20261127.md`,
`C:\b\vinyl-s0-temp\discovery\coverage.json`.

## Two surfaces, kept separate (Hemanth: "don't confuse discover with explore")
Same split as Theatre (Discover poster wall vs Movies/Shows/Anime shelf tabs) and Biblio (Discover vs Explore):
- **Discover** — one endless catalogue grid (the shared house `DiscoverBrowser`), a **genre filter**, and an index if
  there is indexing to do. **No rows.**
- **Explore** — editorial shelf rows with the row-index sidebar. **Vinyl has no Explore tab today** (tabs are Home,
  Discover, Library); where it lives is a separate decision. The rows below are specified so they are ready for it.

## Sources and the one job each does
| Source | Job | Where |
|---|---|---|
| YouTube Music | Plays anything tapped (resolve on tap); New releases; YouTube's own chart | Base for both |
| Last.fm | Endless depth: deep songs/albums/artists per genre tag | Discover |
| Wikipedia (MediaWiki API) | Filter values (genre, era, "#1s", "Year-end") and editorial rows from sourced chart lists (incl. Billboard facts) | Discover filter, Explore rows |
| Kworb | "This week, worldwide": Spotify, Apple Music, iTunes, Shazam, Deezer charts, balancing YouTube's India/Latin America skew (Hemanth chose B) | Discover "This week" list, Explore chart rows |
| JioSaavn | Indian-language depth (existing) | Discover, Language filter |

**Not catalogue sources** (kept only where they already work elsewhere, or dropped): MusicBrainz (no cross-source
merge, so no identity matching needed here; still used by Home/artist pages), ListenBrainz (Home radio, artist pages),
Apple RSS (Kworb covers it), TheAudioDB (artist art only), ReccoBeats, FreqBlog, MusicAtlas (junk or key quotas).
**Billboard.com is never fetched**: PMC's terms forbid robots, scraping, aggregation, reproduction and derivative
works; its robots.txt blocks AI agents by name. Billboard chart facts come only from Wikipedia's sourced lists.
**No merge layer**: each list comes from one source; no blended cross-source ranking.

## Discover (endless grid)
- Rail: one source, "Vinyl". Tabs: Songs · Albums · Artists.
- List filter: **This week** (Kworb worldwide + YouTube chart, interleaved by rank, de-duplicated by normalised
  artist+title) · **Popular** (Last.fm genre/global charts, endless) · **New** (YouTube Music new releases).
- Genre filter: the existing genre list (Last.fm tags), plus "All". Language filter: All, English, Indian languages
  (JioSaavn when an Indian language is chosen).
- Index: only if it helps the current list (e.g. A–Z jump for artists); otherwise none.
- Cards and tap behaviour unchanged (YouTube resolves on tap). Paging: endless via Last.fm/JioSaavn cursors; "This
  week" ends at the chart length.

## Explore rows (ready for when Vinyl has an Explore tab)
From Wikipedia chart lists, proven live via the MediaWiki API: "Every #1 of 2025" (Hot 100 #1s), "Global #1s",
"The year-end 100" (Year-End Hot 100), per-genre "#1s this decade" (e.g. Mainstream Rock #1s of the 2020s, 144 songs),
"#1 hits of the '90s". From Kworb: "This week on Spotify", "Most streamed ever". Page titles are discovered through the
API's list/category pages, never guessed; attribute Wikipedia (CC BY-SA); lists refresh weekly.

## Fetching and politeness
- Kworb: a small fixed page set (global + user's country; songs/albums/artists), at most once a day, parsed in the
  provider process, cached 24 h; parser fails closed (log once), "This week" then falls back to YouTube's chart.
- Wikipedia: official API with a descriptive User-Agent, cached 7 days.
- Last.fm: existing key file (`COLOSSEUM_VINYL_LASTFM_KEY_FILE`); a missing key logs once and the message says the
  catalogue key is missing, not "couldn't reach the catalogue".
- No UI-thread work: all fetching and parsing in the provider process.

## States
Loading/empty/error use the house browser's existing states. A list whose source fails shows the retry state for that
list only; other lists keep working. Offline: last cached pages, marked stale.

## Acceptance
- Discover shows one source, no rows; Songs/Albums/Artists × This week/Popular/New × Genre all populate for the
  24-artist benchmark genres; Popular pages endlessly.
- "This week" contains both Kworb and YouTube entries, no duplicates.
- No request to billboard.com anywhere in the code.
- Explore row data functions return the listed Wikipedia/Kworb rows (tested), ready for an Explore surface.

## Ledger
**Decided by Hemanth (2026-11-27):** one catalogue front instead of per-service sources; every source serves one
function, no spaghetti; Kworb in (B); Discover = endless grid + genre filter + index, no rows; editorial rows belong
to Explore; use Billboard only legitimately (Wikipedia).
**Calls by Claude:** source/job table above; "This week" interleave + de-dup; JioSaavn kept for Indian languages;
MusicBrainz/ListenBrainz/TheAudioDB stay out of the catalogue; daily Kworb, weekly Wikipedia caching.
**Open (Hemanth's):** where Vinyl's Explore lives (new tab vs part of Home).
**Left out:** configuration page, shared Worker, merge layer, mood, reviews.
