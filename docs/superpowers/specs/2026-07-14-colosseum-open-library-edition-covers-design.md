# Colosseum Open Library Edition Covers Design

**Date:** 2026-07-14

**Owner:** [Agent 1 (Codex), comics]

**Status:** Approved design; implementation pending

## Objective

Fill missing collected-edition thumbnails in the 688-series comics catalog using exact ISBN matches from Open Library. Preserve the existing source-honesty rule: cover availability must never imply that an edition is downloadable.

## Scope

This change extends the offline catalog build under `scripts/comics_brain/`. It does not add an app-login requirement, an API key, a runtime metadata service, or another download source.

GetComics remains the preferred cover source and the only v1 download source. Open Library is consulted only for editions whose GetComics enrichment produced no cover.

LibraryThing, Apple Books, Google Books, torrents, and local cover-image bundling are out of scope.

## Architecture

### Cover provider

A focused Open Library provider accepts batches of normalized ISBNs and calls the Books API with `format=json&jscmd=data`. The response is converted to an edition-id-to-result map:

- A matched record with a `cover.large` URL becomes a successful Open Library cover result.
- A missing record or a record without `cover.large` becomes a completed miss.
- A transport or malformed-response failure is not recorded as a completed miss and remains retryable.

The provider owns request construction, response parsing, retry/backoff, and polite request pacing. Catalog mutation remains outside the provider.

### Checkpoint

Open Library results live in a separately versioned `isbn_covers` checkpoint namespace. Each edition result records:

- `done`
- `cover`, nullable
- `provider`, set to `openlibrary` for hits and misses

Hits and genuine misses are checkpointed so weekly rebuilds and interrupted foreground runs do not repeat completed work. A checkpoint-version change may invalidate only the ISBN-cover namespace; it must not invalidate proven GetComics hero or edition results.

### Catalog merge order

The emitted edition cover follows this precedence:

1. Exact GetComics cover already recorded for the edition.
2. Exact ISBN cover returned by Open Library.
3. No cover.

The Open Library pass runs after GetComics edition enrichment so it does not spend requests on editions that already have artwork. Applying the checkpoint is deterministic and produces the same catalog whether the run completes at once or across restarts.

Series hero selection keeps its existing precedence. Once edition covers are merged, the current first-covered-edition fallback may use an Open Library cover when no GetComics hero exists.

## Data and reader contracts

Open Library writes only the existing edition `cover` field. It does not change `available`, `getcomics_post`, edition identity, ordering, or bibliographic metadata.

No QML data-model expansion is required. `ComicDbLedger.qml` continues reading `edition.cover`; the corrected generated catalog supplies the missing URLs through the existing lazy-loaded `ComicsDb.js` path.

## Failure behavior

- Temporary HTTP failures and rate limits use bounded retry with exponential backoff.
- A failed batch is left incomplete so a later run can retry it.
- A valid response with no matching cover is checkpointed as a miss.
- One malformed record cannot discard valid records from the same response.
- Existing GetComics covers and download fields survive every Open Library failure unchanged.
- A fully offline rebuild can still emit the catalog from existing checkpoints; unresolved editions remain blank rather than receiving speculative URLs.

## Command-line behavior

The existing full-catalog build remains the primary entry point. Open Library enrichment participates in its checkpointed workflow and respects the existing bounded-run mechanism used by tests and foreground recovery.

Progress reporting distinguishes Open Library hits, misses, and retryable failures. The final summary reports edition-cover totals without conflating them with GetComics download totals.

## Testing

Pure Python tests cover:

- ISBN normalization and batch request construction.
- Successful parsing of a large cover URL.
- Exact edition mapping for multi-ISBN responses.
- Genuine misses versus retryable request failures.
- GetComics-cover precedence over Open Library.
- Open Library filling only previously blank covers.
- Checkpoint persistence, restart skipping, and namespace-only version reset.
- Download fields remaining unchanged after cover enrichment.
- Hero fallback seeing the newly enriched edition cover.

An integration rebuild must prove:

- 688 series and 5,469 editions remain present and ranked.
- Downloadable counts are unchanged from the pre-cover catalog.
- The number of editions with covers increases.
- A known ISBN-backed edition such as an early *Invincible* collection receives a real Open Library cover URL.
- `comics_db.gen.js` is redeployed to `Colosseum/qml/` and remains lazy-loaded through the comics page.

The native MSVC build must finish with `BUILD_OK` and exit code 0. Hemanth will perform final visual verification in the app.

## Non-goals

This work does not guarantee that Open Library has every cover, alter the blank-cover visual treatment, introduce fuzzy title matching, fetch alternate-edition artwork, or create a multi-provider runtime cascade.
