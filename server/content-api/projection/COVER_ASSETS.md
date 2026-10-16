# Local owned-cover preparation

`cover_assets.py` prepares local WebP objects and a resumable manifest. It does not
create R2 resources, upload, change DNS/cache rules, mutate source databases, or
publish catalogue data. Pillow is required; the verified local Python 3.11 runtime
already supplied Pillow 12.3.0. No package installation was needed.

## Inputs and separate steps

Run from this directory. Paths below are placeholders for operator-selected files.
The JSON input is an array of source URL strings. Alternatively, `--source-sqlite`
opens a source database with `mode=ro` and `query_only`, and reads present `cover`
and `source_cover` columns from `series`, `curated_series`, `curated_edition`, and
`curated_source`. Both inputs can be supplied together. The inventory deduplicates
actual URL strings and preserves table/column or JSON-index provenance; counts
are unique URLs, never a sum of the source table row counts.

```powershell
python cover_assets.py inventory --input-json urls.json
python cover_assets.py inventory --source-sqlite source.sqlite
# Only after source policy permits fetching and provider stops have cleared:
python cover_assets.py fetch --input-json urls.json --root prepared --manifest prepared/manifest.json --origin https://covers.example.org --allowed-host source.example.org --max-downloads 20
python cover_assets.py verify --root prepared --manifest prepared/manifest.json
python cover_assets.py transform --root prepared --manifest prepared/manifest.json --input catalogue.json --output catalogue-owned.json
```

`inventory` is the dry run and performs no HTTP requests. The HTTPS owned origin
must be supplied explicitly, with no path, query, fragment, or credentials. Each
permitted fetch/redirect hostname must be supplied with repeated `--allowed-host`
options; exact host matching applies. This is a technical allowlist, not a rights
or legal determination. Copyright/permission and allowed-origin policy are supplied
externally by the operator. Theatre/IMDb requires separate measurement/authorization.

The default fetch run is sequential, at least one second between source downloads,
with ceilings of 100 downloads, 64,000,000 download bytes, 10,000 retained objects,
and 256,000,000 retained object bytes. Individual downloads are limited to 8,000,000
bytes, 20 seconds across DNS/redirects/body, and 20,000,000 decoded pixels. All are
configurable CLI limits. Failed downloads conservatively reserve their entire
per-request byte allowance. Redirects are capped at four. No original files are
retained; one bounded download and its decoded image are held in memory, encoded,
and discarded. Existing hash objects, including superseded versions, count against
storage ceilings. There is no automatic garbage collection or source deletion.
Do not assume the whole inventory fits these limits, or that every WebP will be
15-25 KB. Budget for measured sizes.

Every DNS result must be public (including each redirect); mixed public/private DNS
is rejected. The socket connects to the validated IP without a second resolution,
and HTTPS still validates the original hostname. Proxy environment variables are
not used. HTTPS downgrade redirects, embedded credentials, encoded response bodies,
non-image/challenge responses and unexpected status codes are rejected. A system
DNS timeout stops the run; its daemon resolver cannot block process exit. The
`--allow-loopback-test` switch permits loopback addresses only for explicit tests;
it never admits private LAN or link-local destinations.

429, 403, 503, Cloudflare challenge markers, and non-image responses stop the run.
A server Retry-After delta or HTTP date is persisted as `retry_not_before`; reruns
make no requests until it passes. Missing/invalid Retry-After uses a conservative
one-hour stop. No automatic sleep-and-retry loop is used. These are stop records,
not proof the provider is safe to resume when the time expires. Other per-URL
errors are recorded and the bounded run may continue.

By default, a verified complete URL is skipped with zero HTTP requests. Changed URL
strings are new work. For unchanged URLs whose content may have changed, explicitly
use `--ttl SECONDS` or `--revalidate`; this fetches the source again rather than
assuming URL identity means immutable content. A failed refresh retains the prior
verified asset and records `refresh_error`. A corrupt prior object fails closed.
429/challenge resume gates apply even with explicit revalidation.

## Manifest and publisher consumption contract

The JSON manifest schema is:

```json
{
  "version": 1,
  "origin": "https://covers.example.org",
  "assets": {
    "https://source.example.org/original.jpg": {
      "status": "complete",
      "original_url": "https://source.example.org/original.jpg",
      "provenance": [{"kind": "json", "index": 0}],
      "key": "covers/<64 lowercase SHA256 hex characters>.webp",
      "owned_url": "https://covers.example.org/covers/<hash>.webp",
      "sha256": "<hash of actual WebP bytes>",
      "bytes": 12345,
      "width": 261,
      "height": 392,
      "fetched_at": 1792100000.0,
      "final_url": "https://source.example.org/original.jpg",
      "etag": null,
      "last_modified": null
    }
  }
}
```

Incomplete entries have `failed` or `stopped` status and an `error`, with no owned
asset claim. Only `complete` entries can be consumed. Image scaling preserves
aspect ratio, applies EXIF orientation, fits within 264x392, and never upscales.
Paths use SHA256 of encoded bytes, not source URL hashes. Object and manifest writes
use same-directory temporary files, flush/fsync and atomic replacement. A crash
between object and manifest leaves a reusable immutable object. A single operator
process must own an output root/manifest; concurrent writers are unsupported.
Treat this directory as generated output, never point it at source material.

Publisher integration uses these two functions, in order:

```python
from cover_assets import verified_urls, transform_publication
verified = verified_urls(manifest_dict, local_asset_root)
publication = transform_publication(original_publication, verified)
```

`verified_urls` reads and verifies every completed local object's byte count,
content hash, path, public URL, WebP format and dimensions. It rejects a corrupt
completed entry; it omits incomplete entries. Pass only its result to the pure
`transform_publication` function. The transform recursively replaces known
`cover`, `poster`, `source_cover` string values and `covers` array strings. At the
same object level it appends original URLs to the existing `coverCandidates` array,
deduplicated and preserving order. Unmapped values, incomplete covers, existing
fallbacks and the input object remain unchanged. Non-string coverCandidates are
rejected when a rewrite would occur. Unrelated URL fields are never rewritten.
The app must receive and retain those original candidates through its existing
poster policy. No private token, proxy URL or arbitrary source policy is inferred.

The transform CLI verifies objects itself and requires a new output file. Fetch
rejects a manifest path matching an input path or an existing unknown manifest.
Source SQLite is never written. The manifest is local preparation evidence only:
`complete` means bytes are ready locally, not uploaded or publicly accessible.
Gate hosted publication on successful upload and actual hosted verification; do
not label the whole catalogue as CDN-backed when only a subset is complete.

## R2 storage-only deployment plan (pending operator setup)

After Hemanth enables R2 and chooses the HTTPS custom domain, rebase manifest URLs
by preparing with that exact origin (existing mismatched-origin manifests fail
closed). Review the unique inventory, local verification and actual object/byte
counts first. Verify current Cloudflare quotas/prices before choosing upload caps;
this code neither promises zero cost nor assumes the proposal's historic limits
are current. Current limits must be checked at each hosted operation; local tests do not verify hosted behavior.

Upload only verified `covers/<hash>.webp` objects, with `Content-Type: image/webp`
and `Cache-Control: public, max-age=31536000, immutable`. Keep an upload receipt keyed
by hash so unchanged keys need no new writes. Bind the bucket to the chosen custom
domain and configure appropriate edge caching separately. The image path must go
directly to R2 through that custom domain, with no Worker/proxy in the route. A
catalogue Worker may serve metadata but must not serve these image bytes. Preserve
original provider fallbacks until hosted checks succeed. Do not upload the local
manifest's provenance/error logs as public objects by default.

Before hosted publication, GET a sample of uploaded objects, check byte hashes,
MIME type and dimensions, fetch again to measure cache behavior, and record actual
`cf-cache-status: HIT` plus route/Worker invocation evidence. Cold/warm native-app
network, timing and fallback acceptance remain separate checks. Local fixture
success proves none of those hosted or app-runtime properties.

## Local tests

```powershell
python -m unittest discover -s server/content-api/projection -p test_cover_assets.py -v
```

Run that command from the repository root. Fourteen tests use only temporary files,
loopback HTTP and synthetic Pillow images. They cover deduplicated readonly SQLite
inventory, aspect ratio/no upscaling, content hashes, no-fetch resume, explicit
refresh, originals preserved, idempotent pure transforms, mismatched hashes,
CLI no-overwrite, redirect/host/private-address policy, pinned DNS, DNS deadline,
byte/pixel/storage/download caps, challenge stops and persistent Retry-After.
No provider harvest, R2 operations, cloud changes, or full application builds are
part of this test run.
