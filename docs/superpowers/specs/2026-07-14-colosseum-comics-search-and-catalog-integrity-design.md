# Comics Search and Catalog Integrity Design

**Date:** 2026-07-14  
**Owner:** [Agent 1 (Codex), comics]  
**Status:** Approved by Hemanth

## Goal

Expose all 688 ranked western-comics series in Tankoban global search, make the GCD comic-series page opaque black like Theatre, and prevent ambiguous GCD volume numbers from attaching unrelated GetComics issues or collections.

## Confirmed failures

- Tankoban global search currently fans out only to AniList manga and GetComics tags. The lazy-loaded `ComicsDb` catalog is used by Top Comics and Explore, but not by `WorldSearch`.
- The pitch-black regression test covers `ComicSeries.qml` and `MangaSeries.qml`, but the full-catalog route opens `ComicSeriesPage.qml`, whose translucent background reveals Tankoban World.
- 322 editions across 100 series use a `Series #N`-style GCD title. Saga demonstrates that number overlap is not identity: `Saga #1` attached Annihilation Saga issue 1, while `Saga #2` through `#6` attached Usagi Yojimbo Saga books.
- Exact ISBN metadata provides the missing canonical bibliographic titles: for example, ISBN `9781632150783` is `Saga — Book One`, ISBN `9781607069317` is `Saga: Volume Three`, and ISBN `9781582406985` is `The Walking Dead, Book Two`.

## Design

### 1. Local catalog lane in global search

`WorldSearch.js` imports the existing `.pragma library` `ComicsDb.js`. A pure local search function filters `ComicsDb.rankedSeries()` by normalized title tokens and maps hits into the existing SearchSurface result contract. The result carries `data.locg = true` and the existing `locgId`, so `Main.qml` continues to open `ComicSeriesPage` without modification.

Tankoban search remains a three-lane fan-out: AniList manga, local GCD comics, and GetComics tags. Local GCD hits win normalized-title deduplication over GetComics tag hits because the GCD record carries the canonical catalog identity. The catalog payload remains lazy: `WorldSearch` reads `ComicsDb` only if Tankoban World has already called `setData`; it does not import `comics_db.gen.js` at root startup.

### 2. Opaque GCD series page

`ComicSeriesPage.qml` receives the same base stack as Theatre: an opaque `#000000` fill, 0.5-opacity art, and the black 0.5/0.78/0.95 gradient. The existing background regression test adds `ComicSeriesPage.qml`, ensuring every live Tankoban series-detail route is checked.

### 3. ISBN canonical identity before GetComics matching

The Open Library exact-ISBN batch response records `title`, optional `subtitle`, and cover, not only the cover. The pipeline preserves GCD source truth in `title` and adds `display_title` only when exact-ISBN metadata supplies a more descriptive identity. UI rows render `display_title || title`.

For ambiguous `Series #N` editions, GetComics matching uses the ISBN-derived canonical title. A result must satisfy both:

1. the requested series occurs as a true title phrase without unrelated lexical ownership (for example, `Usagi Yojimbo Saga` cannot satisfy series `Saga`); and
2. the canonical collection phrase/number matches after harmless packaging normalization (`book`, `volume`, `vol`, `hardcover`, and number words).

The exact-ISBN cover is authoritative for this ambiguous cohort. A GetComics cover may be used only after the GetComics post passes the canonical identity test.

### 4. Fail closed and repair stale checkpoint data

Any uncertain match produces `available: false`, `getcomics_post: null`, and no active download control. The 322 ambiguous editions are invalidated in the enrichment checkpoint and reprocessed with canonical ISBN metadata. Existing unrelated local downloads are not deleted automatically, but the rebuilt catalog can no longer advertise or redownload them.

## Tests and acceptance

- Pure QML/JS search tests prove all 688 loaded rows are searchable, local results route through `locg`, and GetComics duplicates lose to the local catalog record.
- The background regression test fails without `ComicSeriesPage.qml`'s opaque Theatre stack and passes after the fix.
- Python matcher tests reproduce and reject Annihilation Saga and Usagi Yojimbo Saga, while accepting canonical Saga Book/Volume matches.
- Open Library tests prove exact ISBN title/subtitle extraction and `display_title` application.
- Catalog audit reports 688 series / 5,469 editions, zero known Saga false attachments, and no ambiguous active match that violates the canonical-title predicate.
- The deployed `qml/comics_db.gen.js` matches the repaired source catalog and the MSVC build exits 0 with `BUILD_OK`.

## Out of scope

- Torrent fallback and Tankoban Mode manga work.
- Deleting previously downloaded user files.
- Replacing GCD as the bibliographic source or hiding unavailable editions.

