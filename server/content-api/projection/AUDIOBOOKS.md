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
of War. The checked-in fixture contains no fabricated records and is the test
input for both the Python/Node and Qt C++ harnesses.

Each bake refuses existing output and emits an adjacent
`*.manifest.json` containing source and output SHA-256, snapshot identity,
rights/provenance per table and field, and exact row counts. The SQL exporter
refuses a modified or incorrectly licensed bake and emits `*.report.json`,
the bounded statement size and all publication row counts. It produces an
offline SQL artifact only. A publisher must review it, import to the
content API's `LIBRARY` binding, and only then advance library readiness /
revision through the separately owned publication transaction. No import,
Cloudflare deployment, upload or publication is performed here.

The CC0 core contains factual work/recording/person/series records and
relationships, with `recording_asins`/`recording_isbns` retaining all source
recording identifiers. The single convenience ASIN/ISBN on a recording is a
stable representative, not an authority for merging editions. Ambiguous
identifier lookups fail closed. Original source series number text is
preserved; only plain numeric/decimal values acquire sortable positions.
The SQLite includes a local title/people FTS5 index. None of the publisher's
AGPL code is copied here, and no chapters, summaries, descriptions, cover
image bytes, or CC BY-SA-derived expressive content is imported.

Colosseum's application provides the `AudiobookCatalog` context object,
defaulting to bounded per-query content API requests. A local SQLite can be
enabled explicitly with `COLOSSEUM_AUDIOBOOK_CATALOG_PATH` for development or
offline verification; it does not download or replace a user's library.
Consumer QML screens and user-to-work identity matching belong to subsequent
slices. The actual hosted D1 import, server availability, storage budget and
Cloudflare publication are **not** qualified by this local pipeline.

Focused tests:

```sh
node --test tests/catalogue-audiobooks.test.mjs
python -I -m unittest discover -s tests -p 'test_audiobooks_*.py' -v
```

Native harness: build through the lane-shared `build.cmd` wrapper, then run
CTest `colosseum.audiobook_catalogue_harness` or the built harness directly.
