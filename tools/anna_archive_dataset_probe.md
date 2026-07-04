# Anna's Archive Dataset Probe

## Boundary

This probe is metadata-only. It does not log in, click download links, fetch
book files, or bypass browser verification. The goal is to decide whether Anna's
Archive can improve Biblio's canonical graph as a metadata source.

## Current Finding

Anna's Archive should not be benchmarked through public search UI scraping.
Official mirrors are domain-rotating and partly protected, and search pages are
noisy. The useful surface is the dataset layer:

- Official mirrors listed by Anna's Archive FAQ: `annas-archive.gl`,
  `annas-archive.pk`, `annas-archive.gd`.
- Anna's FAQ says the stable JSON API is for member fast-download URLs only.
- For search/custom metadata, Anna recommends generating or downloading their
  ElasticSearch and MariaDB databases.
- Anna's datasets page says raw data can be explored through JSON files, but
  those pages explicitly say they are not intended as an API and recommend
  mirroring code/data locally for programmatic access.

## Live Schema Sample

Browser probe succeeded on the official `.gl` mirror for:

`https://annas-archive.gl/db/aarecord_elasticsearch/md5%3A8336332bf5877e3adbfb60ac70720cd5.json.html`

The record exposes an ElasticSearch-style JSON document with:

- `id`
- `file_unified_data.cover_url_best`
- `file_unified_data.extension_best`
- `file_unified_data.filesize_best`
- `file_unified_data.title_best`
- `file_unified_data.author_best`
- `file_unified_data.publisher_best`
- `file_unified_data.edition_varia_best`
- `file_unified_data.year_best`
- additional title/author/publisher/year/cover fields from matched sources

This looks valuable for edition/file assertions, and possibly title/author
corroboration. It does not immediately prove strong series metadata.

## Decision

Anna's Archive is worth testing only through a local metadata dump or local
ElasticSearch/MariaDB mirror, not live UI scraping.

For Biblio source roles:

- Keep Goodreads dump as canonical seed.
- Keep OceanofPDF as high-weight series/order assertion source.
- Keep LibGen as high-weight ISBN/download candidate source.
- Keep Z-Library public search as low-weight discovery/title corroboration.
- Add Anna's Archive only after we can ingest local `aarecord_elasticsearch`
  JSON or a local ES/MariaDB export.

## Minimal Local Benchmark Plan

1. Acquire a metadata-only Anna dump or generated local ElasticSearch/MariaDB
   database. Do not download book files.
2. Build a local lookup index by normalized title, author, ISBN, and MD5.
3. Run the same 100-row Goodreads benchmark first.
4. Score only visible metadata claims:
   - title exact
   - author exact
   - ISBN present
   - year/date present
   - publisher present
   - extension/file-size visible
   - series/order only if present in structured fields
5. Import Anna claims into `source_assertion`, not canonical tables, until the
   benchmark proves which claim type it improves.

## Expected Role If It Works

Anna's Archive likely improves:

- edition/file assertion coverage
- title/author corroboration
- cover URL coverage
- cross-source provenance via unified records

Anna's Archive probably does not replace:

- Goodreads-derived series graph
- OceanofPDF series/order assertions
- app-facing canonical routing IDs
