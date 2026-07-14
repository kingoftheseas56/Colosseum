# Colosseum Full Comics Catalog v1 Design

**Date:** 2026-07-14
**Owner:** [Agent 1 (Codex), comics]
**Status:** Locked for implementation

## Outcome

Colosseum's Tankoban comics shelf will expose the complete ranked catalog that can be grounded in the local Grand Comics Database extract: 688 ranked series and all 5,469 selected collected editions. Every real edition remains visible even when GetComics has no downloadable file. Download actions exist only for editions with a verified signed GetComics `/dls/` route.

The catalog ships as a generated QML JavaScript library, not SQLite. It is imported and ingested only when `TankobanWorld.qml` is created by the existing lazy world Loader, so the root `Window` does not synchronously parse the multi-megabyte payload during app startup.

## Locked scope

- Input ranking: all 982 records in `scripts/comics_brain/rco_popularity.json`.
- Catalog membership: the 688 records for which the run-pinned GCD selector returns editions.
- Catalog contents: all 5,469 selected editions. Empty ranked runs and the wider 24,341-edition browse remain out of scope.
- Catalog source: the local GCD SQLite dump and extracted `comics_catalog.db`.
- Download source: GetComics only in v1.
- Torrent fallback: compiled support from `a62a128` remains dormant in the ledger until a separate v2 design.
- Delivery: `comics_db.json` as the readable source artifact and `comics_db.gen.js` as the app artifact.
- No long per-edition descriptions are emitted.

## Data model and identity

The generated root remains:

```json
{
  "built": "2026-07-14",
  "pipeline": "GCD run-pinned catalog + GetComics v1 enrichment",
  "series": []
}
```

Each series retains rank, title, RCO year and slug, plus:

```json
{
  "rank": 1,
  "title": "Invincible",
  "year": 2003,
  "slug": "Invincible",
  "locg_id": "gcd-1812",
  "publisher": "",
  "cover": "https://...",
  "editions": []
}
```

`locg_id` remains the field name consumed by the current `ComicsDb.js`/`ComicSeriesPage.qml` path, but full-catalog rows without a legacy LOCG seed receive a stable `gcd-<series-id>` value. When two ranked aliases fall through to the same largest same-named GCD run, both keys gain a deterministic `-r<rank>` suffix so neither overwrites the other in `ComicsDb`'s index. This is an internal catalog key, not a claim that the number is an LOCG identifier. The existing 20 seed-map rows may retain their real LOCG IDs and publisher/cover metadata. Every one of the 688 rows therefore has a clickable, unique series-page key.

Edition identity remains the GCD collected-edition issue ID in `locg_comic_id`; it is stable and already used as the downloader/reader `chId`. Edition records contain only title, format, collects, ISBN, pages, publication date, ID, cover, availability, GetComics post, creators, and source. `description` is omitted from the full payload.

## Stage 1: full offline catalog selection

`gcd_app_data.py` becomes a reusable library and full-catalog entry point. Its pure boundaries are:

- `select_ranked_series(ranked, edition_loader)` preserves RCO order, drops only records with no editions, and produces one series row per successful run.
- `catalog_series_id(gcd_series_ids)` returns the stable `gcd-<id>` key.
- `build_catalog(...)` queries GCD using the existing `(name, year)` pin and reprint-provenance selector, enriches the first 20 rows from `comics_seed_map.json` when available, and emits exactly the source schema above.

The existing `_full_catalog.staging.json` is accepted as a reusable stage-1 input so the network phase need not re-query the 6.2 GB dump. A normal weekly run can regenerate it from the two local databases.

`build_full_catalog.py` is the single foreground/scheduled entry: it runs stage 1, then invokes the resumable GetComics pass. `--reuse-staging` is the fast local/development path; the default weekly path refreshes staging from GCD.

All artifact writes are atomic: write a sibling temporary file, flush/close it, then `os.replace`. An interrupted build must leave the last valid JSON/JS pair readable.

## Stage 2: resumable GetComics enrichment

`gcd_getcomics_enrich.py` is split into pure match/checkpoint logic and injected network/sleep boundaries. Its persistent checkpoint is `comics_db.enrich.checkpoint.json`, keyed by stable series ID and edition GCD ID. A completed no-match is stored explicitly, so restarts do not repeat known misses.

Checkpoint shape:

```json
{
  "version": 6,
  "hero_version": 2,
  "heroes": {
    "gcd-1812": {"done": true, "cover": "https://..."}
  },
  "editions": {
    "1170952": {
      "done": true,
      "cover": null,
      "getcomics_post": null,
      "available": false
    }
  }
}
```

Checkpoint edition version 6 identifies the strict collection matcher. It requires a contiguous normalized series-name phrase, a contiguous ordered edition-title phrase after removing harmless packaging words, every distinctive collection-title word, and an exact standalone requested volume number. A number appearing only inside a multi-volume or issue-run range is not accepted because the existing downloader selects one post/part and cannot prove that it will ingest the requested book. This prevents embedded-name collisions such as `The Boys`/`Anansi Boys`, extra-title collisions such as `Invincible`/`Invincible Universe`, named collections resolving to similarly worded specials, and one omnibus volume resolving to another. Hero matcher version 2 independently invalidates hero results when series matching changes while preserving completed edition work; an edition matcher-version change does the inverse. Stale looser decisions therefore cannot silently survive a quality correction in either phase.

The pass order is mandatory:

1. Visit all 688 series and resolve a hero cover with a series-level GetComics query.
2. Checkpoint after each hero result, including misses.
3. Visit every edition in ranked/reading order, skipping every checkpointed ID.
4. Search and rank GetComics posts using normalized title overlap and the series-name gate.
5. Fetch the selected post and mark `available: true` only when a signed `https://getcomics.org/dls/...:<signature>` link is present.
6. Checkpoint after each edition and atomically refresh `comics_db.json` plus `comics_db.gen.js` after each completed series.

HTTP retries use bounded exponential backoff with deterministic injection for tests. HTTP 404 is a completed miss. HTTP 429 and transient 5xx/network errors retry with increasing delays. A configurable polite inter-request delay defaults to 0.5 seconds. Foreground execution prints phase, rank, progress, hits, misses, retry notices, and checkpoint path with `flush=True`, so it is suitable for the weekly scheduled job and honest manual monitoring.

On a completed run the checkpoint remains valid as a warm cache for the next weekly build. Entries are keyed by stable source IDs, so new catalog records are enriched while unchanged records are skipped.

## Hero-cover fallback

Shelf art is independent from edition downloadability. A series cover resolves in this order:

1. series-level GetComics hero query result;
2. the first edition cover found by edition enrichment;
3. the existing `comics_seed_map.json`/legacy series cover;
4. the existing `PortraitTile` titled gradient fallback.

The final fallback is already a deliberate rendered card with a caption, not an empty image box. The generated series row may therefore carry an empty cover only after all real-image sources miss; the app still renders a non-empty titled tile. No unrelated edition cover or invented bibliographic image is attached.

## Generated JavaScript and deployment

The emitter writes:

```javascript
// GENERATED by the full comics catalog pipeline. Do not edit by hand.
.pragma library
var data = { /* compact JSON */ };
```

The exact generated file is copied to `Colosseum/qml/comics_db.gen.js`. The readable JSON remains in `scripts/comics_brain/comics_db.json`; SQLite is never bundled.

## Lazy app integration

`Main.qml` must no longer import `comics_db.gen.js` or call `ComicsDb.setData` from root `Component.onCompleted`.

`TankobanWorld.qml`, already created only when the Tankoban world Loader first activates, imports both `ComicsDb.js` and `comics_db.gen.js`. Its `Component.onCompleted` ingests the generated object before computing the comics row and logs the loaded series count. The comics shelf model is a property updated after successful ingest, avoiding a one-time `ready()` binding race.

`ComicsDb.js` continues to index all series and return rank-ordered shelf rows. `ComicSeriesPage.qml` receives the existing `locg:<key>` route, looks up the record in `ComicsDb`, and renders `ComicDbLedger.qml` without live source resolution.

## Honest unavailable state

In `ComicDbLedger.qml`:

- a downloaded edition remains readable;
- an edition with `available && getcomics_post` exposes the GetComics download action;
- an edition without that verified source remains a normal bibliographic row but exposes no download icon, hover verb, pointing cursor, click action, queued state, or torrent search.

The dormant native torrent entry points are not removed. Only the ledger branch added for Step 2 is disconnected for v1.

## Verification contract

Pure Python tests prove:

- ranked selection preserves order, drops only empty series, keeps every edition, and assigns stable IDs;
- generated rows omit long descriptions;
- match ranking rejects wrong-series results;
- signed `/dls/` detection is the sole availability gate;
- checkpointed hits and misses are both skipped on restart;
- hero pass completes before the first edition call;
- retry/backoff and atomic output behavior are deterministic under injected fakes.

App tests prove:

- `Main.qml` has no generated-catalog import or root ingest;
- opening Tankoban through the existing lazy Loader produces `ComicsDb: loaded 688 series` and no QML creation error;
- `ComicsDb.rankedSeries()` returns all 688 rows and series lookup reaches the ledger;
- an unavailable edition has no active download action, while an available edition still calls `Comics.downloadIssue`;
- all `font.pixelSize` assignments touched by this work remain integers.

The final native build runs `native/build-msvc.bat` after terminating any running `colosseum.exe` by PID and must end with `BUILD_OK`, exit 0. Hemanth performs the final eyes-on shelf and ledger pixel check because the Qt/D3D surface is not trustworthy through headless capture.

## Definition of done

- The source artifact contains 688 series and 5,469 editions in RCO rank order.
- Every edition is retained; unavailable records do not expose a v1 acquisition action.
- Every available record has a stable GetComics post and verified signed `/dls/` evidence from enrichment.
- The enrichment can be interrupted and resumed without repeating completed heroes, hits, or misses.
- The 688 hero pass runs before edition enrichment and no shelf tile renders visually empty.
- `qml/comics_db.gen.js` is compact, deployed, and loaded only on first Tankoban activation.
- The shelf opens every generated series and the ledger renders its editions.
- Python tests, QML/lazy-load gate, full MSVC build, scoped diff review, and Hemanth eyes-on handoff are complete.
- Only comics/catalog files are staged; A2/A5 work remains untouched.
