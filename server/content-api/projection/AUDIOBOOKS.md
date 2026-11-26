# AudioSilo CC0 audiobook catalogue, slice C

This is a reproducible data pipeline and query seam, not a deployment. The pinned
input is the independently checked AudioSilo 2026-11-16 SQLite snapshot
(`data-v2026.11.16-82ea38c-30a9e1e`). Use only an immutable, checksum-verified
source. The two command-line inputs are explicit so developer machine paths do
not enter the repository or the application.

```sh
python -I projection/audiobooks_bake.py --source /path/to/pinned/meta.sqlite --output /path/to/bake/audiobooks-cc0.sqlite
python -I projection/audiobooks_publication.py --source /path/to/bake/audiobooks-cc0.sqlite --output /path/to/bake/audiobooks-cc0.sql
```

For a deterministic small fixture, add `--fixture` to the first command and
use a fresh output path. That selects 30 real works, including Dune and Rhythm
of War and at least one commercial English recording for each editorial
narrator (selected by work id, then recording id). The checked-in fixture
contains no fabricated records and is the test
input for both the Python/Node and Qt C++ harnesses.

Each bake refuses existing output and emits an adjacent
`*.manifest.json` containing source and output SHA-256, snapshot identity,
rights/provenance per table and field, and exact row counts. The SQL exporter
refuses a modified or incorrectly licensed bake and emits `*.report.json`,
the bounded statement size and all publication row counts. It produces an
offline SQL artifact for the semantic Worker backend. The public-static
publisher described below consumes the verified bake directly. The
SQL exporter performs no import, deployment, or upload.

The CC0 core contains factual work/recording/person/series records and
relationships, with `recording_asins`/`recording_isbns` retaining all source
recording identifiers. The single convenience ASIN/ISBN on a recording is a
stable representative, not an authority for merging editions. Ambiguous
identifier lookups fail closed. Original source series number text is
preserved; only plain numeric/decimal values acquire sortable positions.
The SQLite includes a local title/people FTS5 index. None of the publisher's
AGPL code is copied here, and no chapters, summaries, descriptions, cover
image bytes, or CC BY-SA-derived expressive content is imported.

The v2 Discover contract adds factual work genres; the recording's exact
release date, publisher, cover URL pointer and ingestion timestamp; and
materialized dramatization signals. Only a known GraphicAudio publisher,
a direct GraphicAudio link, or an explicit Full Cast narrator credit
(including named credits such as "Full Cast featuring Joan Walker") qualifies
a recording as dramatized. Multiple ordinary narrator names or "Various"
alone are not evidence. These exact signals and their counts,
the three editorial narrator picks, and shelf counts **as
of the pinned snapshot date** are recorded in the bake manifest. The
served New releases row dynamically excludes future calendar dates. Five
static shelves appear in order: New releases, Fully dramatized, then
Narrated by each of the three baked narrators. All shelf and genre pages
are 20 recording items long. The genre mosaic has four distinct existing
cover URLs per genre; categories without four real pointers are omitted
rather than filled with fabricated covers. A multiple-series work does
not choose one numbering system for its item card. Cover URLs are third-
party pointers only: the projection never mirrors image content.

### Editorial narrator rows

Hemanth selected these Discover rows on **2026-11-17**, in this exact order:

1. Scott Brick (`scott-brick`)
2. Simon Vance (`simon-vance`)
3. Robin Miles (`robin-miles`)

They are not ranked by recording volume. `NARRATOR_ROWS` in
`audiobooks_bake.py` is the ordered editorial list. `narrator_shelves` retains
rank (1–3), person id, the name from `people`, and `english_recordings`.
That count and the narrator shelf include only English (`language='en'`)
**commercial** recordings: `librivox_id IS NULL OR librivox_id=''`. Nonempty
LibriVox ids (including zero) are excluded; other catalogue rows still retain
those recordings. The projection preserves the factual `librivox_id` field
so hosted and local shelf queries use the same rule.

To change the rows, edit `NARRATOR_ROWS` in the desired order and update the
editorial attribution/date in its comment and manifest `narrator_selection`,
this list, and the tests' expected ids/names. Regenerate the fixture with
`--fixture` to a fresh output, replace its SQLite and manifest together, and
run the focused Python/Node tests and native harness. Regenerate the full
bake separately before publication; an old bake lacks the commercial filter
field. The bake fails with the offending id if any pick is missing from
`people` or has zero commercial English recordings; it never silently
substitutes a different narrator. The manifest records the ordered picks,
their commercial English counts, and the editorial attribution.

Colosseum's application provides the `AudiobookCatalog` context object,
defaulting to bounded per-query content API requests. A local SQLite can be
enabled explicitly with `COLOSSEUM_AUDIOBOOK_CATALOG_PATH` for development or
offline verification; it does not download or replace a user's library.
Consumer QML screens and user-to-work identity matching belong to subsequent
slices. The actual hosted D1 import, server availability, storage budget and
Cloudflare publication are **not** qualified by this local pipeline.

### Public static assets

Prepare the complete verified bake, then pass its output to the existing public
publisher.

```sh
node projection/audiobook_static.mjs /path/to/audiobooks-cc0.sqlite /path/to/fresh-audio-assets YYYY-MM-DD
node projection/public_static.mjs /path/to/i01 /path/to/content /path/to/fresh-public /path/to/public-report.json /path/to/previous-public --audiobooks /path/to/fresh-audio-assets
node projection/verify_public_static.mjs /path/to/fresh-public /path/to/public-report.json /path/to/verification.json /path/to/previous-public
```

Pass each known retained artwork source as an additional previous-publication
argument to both commands. The public index advertises a versioned audiobook
root. The report records the exact bake hash and logical row counts. The
audiobook graph contributes to the public generation SHA-256 and the total file
count. The existing LIBRARY source revision and compatibility manifest retain
their meaning for older clients.

Discovery, search availability, and person listings use the supplied UTC
publication date. Publishing a new snapshot advances that date. Work details
retain every recording, including future and undated entries. Search uses
SQLite's indexed title and people tokens, work eligibility, and BM25 ranking.
Slugs retain exact-series priority and the single unambiguous typo fallback.
Work, person, series, ASIN and ISBN lookups use string keys. Ambiguous identifiers
return an empty result. Recording and publisher cover fields remain URLs.
The client loads selected hash-verified shards through the existing static
resolver and may reuse those shards offline. Older publications continue to use
the semantic RPC. Static reads use the asset route without a guest session.

Focused tests:

```sh
node --test tests/catalogue-audiobooks.test.mjs
python -I -m unittest discover -s tests -p 'test_audiobooks_*.py' -v
```

The provenance acceptance test uses a separate field-level CC0 allowlist from
the qualification report, requires coverage of every output column, and rejects
expressive fields even if their containing source record has a CC0 tag. A tiny
synthetic source additionally checks named Full Cast evidence, editorial
order, commercial-only counts/shelves, missing or empty picks, expressive-field
omission and refusal of non-CC0 source rows.
The reproducibility check regenerates the checked-in fixture from the pinned
source and requires byte equality, not just matching logical rows.

Native harness: build through the lane-shared `build.cmd` wrapper, then run
CTest `colosseum.audiobook_catalogue_harness` or the built harness directly.
