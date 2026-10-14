# Compact D1 projection receiving evidence — 2026-10-14

## Scope and takeover

The interrupted GLM session left both named worktrees clean: server
`codex/content-api-free` at `fcdc931c`, native candidate
`codex/d1-119-no-account` at `dfb4b8c9`. Its earlier no-account gate, updater
repairs, progress-preservation tests and staging token rotation remain intact.
There were no newly saved compact-projection edits from the quota-exhausted
session. Those existing commits were inspected for continuity; their runtime
and release claims were not requalified here.

Codex implemented local projection preparation and independent full-content
verification. Sol 6.1 supplied separately owned query-contract and metering
slices. This checkpoint does not change the native candidate, existing Tankoban
checkpoint, staging service or personal state. No R2, Neon, Biblio migration,
Feria, Vinyl, GitHub REST, full import or release activation was performed.

## Full local artifact

- Revision: `ed0e35e84fd6ca770e33d7956ac85cc8b6e382f2204c31cf5a09c3b0bd54d933`
- SQLite SHA256: `03ed7590275ac41c1bdcfb73c5e42a0f3b5b856e97be94442860ac2cf0066f77`
- Indexed SQLite size: **192,040,960 bytes / 183.14453125 MiB**.
- Ordinary projection/payload rows: **424,274** across 11 tables.
- Payload pages: **24,293**, containing **118,887,774 UTF-8 bytes**.
- Largest complete encoded envelope: **131,023 bytes**, below the 131,072-byte cap.
- Source records independently verified: **1,094,498 non-Tankoban records** from
  every family in the frozen source schema, including duplicates and empty
  families. All four input hashes remained unchanged.
- **20/20 representative full-population SQL/fact contracts** matched the source
  oracle; **37/37 local tests** passed with production-source checks enabled.
- SQLite integrity check passed. Detail ownership, nested mirror associations,
  curated source relationships, complete envelopes and continuation were checked.

All source scalar fields remain in query rows or typed detail records. MAL tag
and classification membership equivalence is checked with multiplicity before
sharing storage; axes and baked counts remain represented. Comics statistics
and curated coverage are merged into roots. Detail children are grouped into
revision/entity/page payloads. The 1,415 canonical issue records whose parents
are absent from searchable source roots are preserved and explicitly reported.

The same-environment repeated-build test is byte-identical. Revision hashing
includes exact builder bytes; cross-platform CRLF normalization is not qualified.
Generated source databases, artifact and complete provider receipts remain
outside Git. The manifest remains `hostedQualified: false`: a tiny schema/cost
fixture does not qualify the full artifact as a deployed service.

## Hosted qualification and failures

The reviewed total SQL allowance for this entire lane was **10,000 reads and
1,500 writes**, including earlier failed attempts. The account was already on
Workers Free. Before probing, exact-date, account-wide
`d1AnalyticsAdaptiveGroups` for UTC 2026-10-14 reported 27,794 reads / 56,760
writes across all databases, with unknown analytics lag. This was not inferred
from trailing-24-hour selected query shapes or publisher reservations.

| Attempt | Executed statements | Reads | Writes | Outcome |
|---|---:|---:|---:|---|
| Original reservations | 1 / 184 | 1 | 3 | CREATE consumed one read; zero-read reservation rejected |
| Revised reservations | 56 / 184 | 73 | 167 | Byte comparison rejected altered dollar signs in a synopsis |
| Corrected transport, 8-root sample | 174 / 174 | 223 | 389 | All status, meter and ordered-value checks passed |
| **Total** | **231** | **297** | **559** | **Within the original lane allowance** |

The second failure was local transport composition, not a D1 data limitation:
JavaScript replacement-string substitution converted `$$` into `$` while
embedding statement JSON. A literal-preserving composition and an actual Node
regression now protect dollar substitution patterns, Unicode, backslashes,
quotes and newlines. The exact readback guard caught the corruption before
further SQL. No attempt was blindly retried; reservations and plan digests were
reviewed before each bounded correction.

Final approved plan digest:
`ebf9b20368f2cf437a8c981e8b9ff3176424e5bc5cbe6aa5af81e680e21fe8e4`.
Its reservation was 7,584 reads / 1,193 writes, plus the prior 74 / 170 actual
costs. It covered every ordinary table/index, up to 8 entity rows, 20 membership
rows per membership table, 2 eligible payload pages of at most 4 KiB and the
actual external-content Comics trigram schema. Every ordered scalar result
matched; payload TEXT bytes remained exact. Integral JSON number representation
was normalized without changing strings, booleans, nulls or nonintegral values.

The three probes used separate, owned disposable databases. All three cleanup
receipts show deletion and verified absence. The final fixture DELETE returned
200; a subsequent list contained the two original databases and the exact-ID
GET returned database-not-found error 7404. List `total_count` still lagged at 3,
so that metadata alone was not used as absence proof. Management create/delete
operations expose no SQL row meters and are not reported as measured zero.

The post-cleanup account-wide UTC-day analytics reported **28,091 reads /
57,319 writes**. The three disposable database bins exactly reconciled to
297 / 559; both original database bins stayed unchanged. The arithmetic also
matches the pre-probe total plus all attempts. General provider analytics lag
remains unknown; this receipt does not establish future spare capacity.

Local complete-receipt SHA256 identities:

- Attempt 1: `a3c59707af9e7a9c21f0ded130535529220cb609c64a6873329354115ae8836c`
- Attempt 2: `7b5f75b6882bd93c5506d2ef3018f9ce7b6a9420b53b82a25525edb3d0feb6db`
- Final: `f868ef849c2bf7083675769a8ef5b16f7797342b453f87e9a38b3f5148b6172b`

## Measured insert weights

These are provider `rows_written` per individual INSERT with indexes already
present. The full row counts come from the local manifest, not a hosted import.

| Table | Full rows | Sample inserts | Measured writes per insert |
|---|---:|---:|---:|
| IMDb title | 61,585 | 8 | 8 |
| IMDb genre | 143,615 | 20 | 1 |
| MAL anime | 20,000 | 8 | 5 |
| MAL manga | 20,000 | 8 | 6 |
| MAL classification | 127,417 | 20 | 2 |
| Comics series | 22,710 | 8 | 3 |
| Comics curated | 1,134 | 8 | 6 |
| Curated genre | 1,821 | 20 | 1 |
| Curated format | 1,695 | 20 | 1 |
| Payload | 24,293 | 2 | 2 |
| Source meta | 4 | 4 | 1 |

The samples confirm the structural data/index model, including clustered
`WITHOUT ROWID` membership primary keys counting as one base structure. Applying
those weights to all ordinary rows models **1,238,169 writes**. This remains an
extrapolation, excluding FTS, schema, receipts, verification and cleanup. At an
unchanged 60,000-write publisher cap, ordinary data alone needs at least
**21 publisher-days**; this is not a total migration ETA or permission to raise
the cap.

The actual trigram fixture used 1 read / 2 writes for CREATE, 8 / 8 for populating
8 titles, 8 / 0 for readback and 1 / 0 for MATCH. Larger FTS maintenance, merges,
updates and deletes remain unmeasured. Provider SQL execution durations were
0.1073–0.7469 ms on this tiny fixture, served from SIN/APAC. These are neither
network response time nor full-catalogue/Worker/native loading performance.

## Acceptance boundaries and next gate

Full import remains held for Astra review of the projection, source contracts
and cost evidence. The new publisher still needs its own immutable manifest
and checkpoint namespace, primary-key cursors for `WITHOUT ROWID` tables,
byte/parameter-aware chunks, atomic data-plus-receipt recovery and actual meters
for controls, bookkeeping and verification. Preserve the existing Tankoban lane
and finish its separate remaining import/guest/native qualification when its
daily budget permits.

Twenty fixtures cover representative IMDb filters/exclusions/prefix, MAL manga
selected-population Bayesian ranking and facets, escaped all-word Comics search
and curated raw facts/coverage. They are not the complete native consumer matrix.
Native curated houseScore/order, VaultKit normalization, QVariant/QML conversion,
MAL anime and remaining APIs, nested native presentation, short-search fallback
and long ASCII/Unicode LIKE compatibility remain gates.

The next release work is paired semantic endpoints and native consumers,
per-capability readiness, durable cache/offline behavior, cancellation and
adversarial pagination/quota checks. Legacy catalogue acquisition is disabled
only after that matrix passes. The real guest→Worker→native journey, fresh-profile
operation without bulk catalogue databases, installer progress preservation and
final 1.1.9 package remain unverified here. No package, signing, feed activation
or release was performed.
