# Theatre Movies/Shows — IMDb Index Catalogue Design

**Date:** 2026-08-02

**Status:** Drafted from the locked 2026-08-02 brainstorm; awaiting Hemanth's review.

**Amends:** `2026-08-01-theatre-harbor-depth-catalogue-design.md`. The page anatomy, hover-only
rating, See-all, customization, extension placement, and explicit-content contracts of that spec
**remain in force**. This spec replaces its §5.1–5.2 shelf inventories, §6.1 data architecture,
and §7 ranking semantics for Movies and Shows. The Anime tab and Theatre landing are untouched.

## 1. Why this exists (the failure being fixed)

The shipped Movies/Shows shelves guessed from Cinemeta preview fields and had no vote counts:

- **Hidden Gems** showed Shawshank/Godfather/Dark Knight (a "current buzz" number was used in
  place of vote counts, inverting the shelf); **All-Time Greats** rendered a single title.
- **Korean Drama** showed X-Men '97, Avatar, Korra (their *production credits* contain Korean
  animation studios; verified live: Avatar = `United States, South Korea`).
- **British Television** showed Ted Lasso and Game of Thrones (`United States, United Kingdom`
  co-production credits).
- **Animated Series** mixed The Simpsons with One Piece and Re:Zero.

Harbor's real engine (verified in `harbor-main/src/lib/feed/`) gates every quality shelf on a
**vote count** and classifies international rows by **original language**
(`with_original_language`), both from TMDB. Keyless Harbor degrades to plain "Top X" Cinemeta
rows. We go one better: bundle the missing numbers from IMDb's public datasets and render the
rich shelves keyless.

## 2. Locked decisions (2026-08-02 brainstorm)

1. **Foundation:** a bundled offline SQLite index baked from IMDb's public datasets — the
   movies/shows twin of `data/mal_catalog.db`. Keyless forever; no TMDB/Trakt/accounts.
2. **Quality trio (Harbor's):** Top Rated · Hidden Gems · Cult Classics. *All-Time Greats*
   (movies) and *All-Time Great Series* (shows) are retired.
3. **International shelves classify by original language,** never production-credit country.
4. **British Television is dropped** (no honest keyless signal separates British from American
   English-language TV; Harbor has no such row either).
5. **Anime lives only in the Anime tab.** Japanese-language animation is excluded from every
   Movies/Shows shelf. Animated Movies/Series mean Western animation.
6. **Language shelves:** Japanese / Korean / French Cinema fixed; Spanish, Italian, German,
   Swedish, Danish Cinema join the existing Movies daily-rotation pool as guests.
7. **Depth model:** index-driven shelves are deep (See-all pages hundreds), stable day-to-day,
   and work offline. Live rows stay live: Top 10, Currently Airing, Recently
   Released/Premiered.
8. Everything already ratified and untouched: landing page, Top 10 first, genre boxes last,
   hover-only `★` rating, See-all pattern, Harbor-parity customization, extension placement,
   global Explicit Content policy.

## 3. The bundled index

### 3.1 Bake pipeline

`scripts/theatre_brain/build_imdb_db.py` (new; follows `anime_brain/build_mal_db.py`
conventions) downloads at **bake time only** — the artifact ships, the dumps do not:

| source (datasets.imdbws.com) | size (gz) | provides |
|---|---|---|
| `title.ratings.tsv.gz` | ~9 MB | rating, **votes** |
| `title.basics.tsv.gz` | ~214 MB | type, title, years, runtime, genres, isAdult |
| `title.episode.tsv.gz` | ~52 MB | episode counts for series |
| `title.akas.tsv.gz` | ~485 MB | original-language derivation |

Output `data/imdb_catalog.db` (gitignored, like `mal_catalog.db`). One table `title`:

```
tt TEXT PK · type TEXT (movie|series|mini) · title TEXT · startYear INT · endYear INT
runtimeMin INT · genres TEXT(JSON) · rating REAL · votes INT · episodes INT
origLang TEXT ('' = unknown) · isAnime INT
```

Inclusion filter: `titleType ∈ {movie, tvSeries, tvMiniSeries}`, `numVotes ≥ 1000`,
`isAdult = 0` (adult titles never enter the artifact — same posture as the SFW-only anime
bake). Indexes on (type, votes), (type, rating, votes), (type, startYear), (type, origLang).
Target artifact ≤ ~25 MB; the vote floor is the dial if it runs large.

### 3.2 Original-language derivation (bake time)

Per title, first signal wins:

1. `title.akas` row with `isOriginalTitle = 1` carrying a language code;
2. script detection on `originalTitle` — non-Latin scripts are definitive (Hangul→ko,
   Kana→ja, Cyrillic→ru, …);
3. Latin-script tie-break from akas region/language evidence (e.g. an original title that
   matches the FR-region aka → fr);
4. otherwise `origLang = ''` (unknown).

**Honesty rule:** language shelves include only titles with a known matching `origLang`;
International Cinema includes only known non-`en`. Unknown never qualifies.
`isAnime = (origLang = 'ja' AND genres contains Animation)`.

### 3.3 Posters and identity

The index carries **no artwork**. Cards keep the existing metahub-by-tt poster URL and the
existing gradient/title fallback. Item identity is the `tt` id; detail routing is unchanged.

## 4. Shelf inventories (ratified lineups)

### 4.1 Movies

| shelf | source | recipe |
|---|---|---|
| Top 10 | live | unchanged |
| Recently Released | live | Cinemeta recency + index votes floor (drop if known and votes < `RECENT_VOTE_FLOOR`) |
| Top Rated | index | rating ≥ `TR_RATING`, votes ≥ `TR_VOTES`, sort rating |
| Hidden Gems | index | rating ≥ `HG_RATING`, `HG_VOTES_MIN ≤ votes ≤ HG_VOTES_MAX`, sort rating |
| Cult Classics | index | year ≤ 1999, rating ≥ `CC_RATING`, `CC_VOTES_MIN ≤ votes ≤ CC_VOTES_MAX` |
| Under Two Hours | index | runtimeMin ≤ 120, votes floor |
| Documentary Movies | index | genre Documentary |
| Animated Movies | index | genre Animation AND NOT isAnime |
| International Cinema | index | origLang known and ≠ en |
| Japanese / Korean / French Cinema | index | origLang = ja/ko/fr, NOT isAnime |
| 2020s → 1970s | index | year window, sort votes |
| Daily rotation ×6 | index | existing genre/runtime/era recipes **+ language guests** (es, it, de, sv, da) |
| Extensions · genre mosaic | — | unchanged |

### 4.2 Shows

| shelf | source | recipe |
|---|---|---|
| Top 10 | live | unchanged |
| Currently Airing | live | unchanged (Cinemeta status) |
| Recently Premiered | live | recency + index votes floor |
| Top Rated | index | series thresholds, anime-excluded |
| Hidden Gems | index | **new for shows** — series vote band |
| Cult Classics | index | **new** — year ≤ 1999 series, vote band |
| Long-Running Series | index | episodes ≥ `LR_EPISODES`, sort episodes |
| Limited Series | index | `type = mini` — exact, no season guessing |
| Drama · Comedy · Crime and Mystery · Science Fiction and Fantasy · Documentary Series | index | genre recipes |
| Animated Series | index | Animation AND NOT isAnime |
| Korean Drama | index | origLang = ko, NOT isAnime |
| ~~British Television~~ | — | **dropped** |
| Extensions · genre mosaic | — | unchanged; Shows have no rotation |

Anime exclusion applies to **every** Movies/Shows shelf, including Top 10 and live rows
(filtered at the item level when the index knows the title).

### 4.3 Thresholds are named constants with an eyes-on gate

All `*_RATING` / `*_VOTES*` values live as named constants in `TheatreCatalogRules.js`, seeded
from Harbor's shape but **calibrated on IMDb's scale against real output** (IMDb votes run
~100× TMDB's: Shawshank has 3.2M). Starting points — movies: Top Rated 8.0/≥200k; Hidden Gems
7.4/10k–100k; Cult Classics 7.2/10k–250k. Series: Top Rated 8.2/≥100k; Hidden Gems
7.5/5k–75k; Cult Classics 7.5/5k–150k. The calibration gate (§7) decides final values, not
these numbers.

## 5. Engine shape

- **`ImdbCatalog`** (native, mirrors `MalCatalog`): `ready()`,
  `titleCatalog(query, offset, limit)` — allowlisted keys only (`type, order(rating|votes|year),
  ratingMin, votesMin, votesMax, yearFrom, yearTo, runtimeMax, genre, lang, notLang,
  excludeAnime`), every value bound, limit clamped; rows come back card-shaped for the
  existing mappers. Registered like `MalCatalog`, passed into Theatre the same way.
- **`TheatreApi`**: index shelves publish local-first exactly like the Anime tab (instant,
  offline-capable); live rows keep their current transport. The Cinemeta candidate-pool
  ranking engine and its **popularity fallback are deleted**. Full-meta enrichment remains
  only for detail pages.
- **See-all**: index-shelf pins page the index by offset/limit (deep, instant); live-shelf
  pins keep Cinemeta paging. Same grid, same back contract.
- **Customization**: unchanged mechanics. New keys (`cult-classics`, shows `hidden-gems`)
  appear in default position; retired keys (`all-time-greats`, `british-television`) are
  already ignored by saved orders per the existing applyCustomization contract.
- **Explicit content**: unchanged policy; additionally `isAdult` titles never enter the
  artifact.

## 6. States, failure, offline

- **Index present (normal):** index shelves paint immediately from disk; live rows fill as
  they answer. One slow live row never blocks the page.
- **Index missing/corrupt:** index shelves are *omitted* (never faked); Top 10 + live rows +
  genre boxes still render; one quiet page-level line notes the reduced catalogue.
- **Offline:** index shelves fully render (posters fall back to gradient cards); live rows
  keep last-session data or collapse quietly.
- **Stale artifact:** the bake is a deploy/data-vault action (like the MAL db); the runtime
  never downloads dumps.

## 7. Acceptance criteria (all against REAL data — fixtures prove wiring, never truth)

1. Movies **Top Rated** contains Shawshank/Godfather/Dark Knight; **Hidden Gems** contains
   **no** title with votes ≥ 500k and none of those three.
2. **Cult Classics** is entirely pre-2000 with ≥ 20 titles.
3. Every index shelf renders ≥ 20 titles (no one-title shelves) or is omitted.
4. **Korean Drama** = Korean-language series only: All of Us Are Dead in; X-Men '97, Avatar,
   Korra, Solo Leveling out.
5. **Japanese/Korean/French Cinema** contain only that original language, live-action only.
6. **British Television** appears nowhere, including previously saved row orders.
7. **Animated Series/Movies**: Simpsons-class in, One Piece-class out.
8. No anime title on any Movies/Shows shelf; Shows **Top Rated** has Breaking Bad, not
   Attack on Titan.
9. **Limited Series** contains only true miniseries (Chernobyl-class).
10. Movies daily rotation surfaces language guests across a simulated week; same UTC day
    stable.
11. **Recently Released/Premiered** carry no zero-vote shovelware when the index knows the
    title; no fabricated freshness claims.
12. Deep See-all: an index shelf pages past 100 items; live shelves page as today.
13. Offline boot renders index shelves; app boots with no QML errors; all existing Theatre,
    Discover, customization, and explicit-policy regressions stay green.
14. **Eyes-on gate:** the real title list of every quality/language/animation shelf is
    produced and reviewed (Hemanth's eyes) before the work is called done.

## 8. Non-goals and deferred

- **Deferred:** an honest British-TV signal; airing-status derived from the dataset; other
  regional shelves (Nordic noir etc.) beyond the rotation guests.
- **Non-goals:** TMDB/Trakt/keys/accounts (never); changes to the Theatre landing, Anime tab,
  Top 10 mechanics, genre pages, detail pages, or Tankoban/Biblio; award discovery; blurbs.
- **Discarded with reasons:** production-country classification (proven false-positive
  machine — the X-Men '97 bug); credits-*contain* country matching (same); popularity-as-
  votes fallback (inverted the quality shelves); keeping All-Time Greats (a second Top Rated).
