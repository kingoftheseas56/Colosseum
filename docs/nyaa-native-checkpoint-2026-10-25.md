# Native Nyaa and Worker checkpoint — 2026-10-25

The user chose native Nyaa search and retained the Tankoyomi/Tankorent Worker migrations. LibGen, AudioBookBay and GetComics remain outside this pass. Nyaa no longer depends on Render or Workers; no paid hosting upgrade, master merge, installer or release was performed.

## Change

- Production Nyaa search directly issues GET requests to the fixed `https://nyaa.si/` RSS endpoint, preserving category `3_1`, seeder ordering, bounded query families and per-query provenance. Hosted JSON response parsing was removed from this source.
- Native parsing retains uploader trust, coverage, ranking and tracker-bearing magnets. Replies are capped at 1 MiB, queries at eight and 512 characters each, RSS items at 100 and XML nesting at 64. Redirects are rejected, deadlines are independent, malformed XML/DTD documents fail closed, and hashes and canonical Nyaa torrent URLs are validated before metainfo retrieval. Cancellation and usable partial results remain supported.
- Defaults generation 19 restores the original native Nyaa manifest URL from either prior hosted built-in URL. Identity, opt-in, order, timestamps and custom URLs are preserved. A fresh install still disables Nyaa.
- Torrent retrieval, hash verification, file indexing, identity storage, libtorrent and payload downloads remain native. The dedicated source-failure signal remains separate from acquisition failures.
- Tankoyomi and Tankorent production routing and Worker source were untouched. The unused Nyaa hosted service code/deployments were not deleted or modified in this pass.

## Verification

- Extension first-run/migration Qt suite: 53 passed. Coverage includes generations 16/17/18, each prior built-in and custom URL, enabled/disabled states, persisted generation 19 and preserved ordering/identity/timestamps.
- Focused native RSS/metainfo transport harness passed: request origin/query/method, strict RSS validation, split text/CDATA and uploader preference, malformed/DTD/deep/oversized feeds, HTTP/redirect errors, partial results, independent deadlines, cancellation and duplicate/late completion. Existing native metainfo URL/body/deadline checks remain included.
- Existing volume-matching logic harness passed. Its synthetic RSS fixtures now contain canonical torrent links and required counts; production validation was not relaxed to accept invalid fixtures.
- Production Windows `colosseum_runtime_ready` build passed with real libtorrent. Executable SHA-256: `2C108E37CF7BF03737889809314D76DEFEE3E5CC058406D60FE899898E3A3636`. QML fingerprint: `1d6a08e55c64219e885f880c9260c71887fbd35e01259ba733afc89dd77a8cb6`.
- A fresh isolated profile ran the real Main/series/Sources UI offscreen, continued without an account and enabled Nyaa. Only One Piece series/Vol. 1 catalogue metadata was supplied through the existing service. No provider rows, RSS responses, torrent bytes or mappings were supplied by the driver or copied from the Render proof.
- The app driver exited 0 and the inspected screenshot showed 11 live sources. Native verification persisted hash `12873efd06ddd2dde985b23a6c0f044e9d0788ab`, file index `43`, path `One Piece (Digital) (1r0n)\One Piece v001 (2003) (Digital) (1r0n) (f).cbz`, evidence `metainfo_exact_filename`, parser `arc18.1`.
- Read-only profile inspection confirmed that mapping as verified, four torrent records, 923 indexed file records, 118 volume mappings and search state `error_class=none`, `result_count=11`. The installed Nyaa entry used `colosseum://well/nyaa` at generation 19.
- No payload download action ran. Acquisition state remained `none`; acquisition-failure signals and LocalDownloads jobs/items/active counts/bytes remained zero.
- Luna receiving checks reran Tankoyomi's 11 Worker tests and Tankorent's 13 Worker tests successfully. Both public health and manifest endpoints returned HTTP 200 with their expected contracts/capabilities and revisions `0.2.1` and `1.0.2` respectively. Their earlier real-app proofs remain in the separate Worker receipts; those app flows were not rerun in this pass.

## Remaining qualification

The separate fresh-profile live catalogue route, Linux/Android runtime and installers were not qualified here. MangaSin and LectorXD remain disabled pending usable upstream access and complete live source qualification; LectorXD also lacks a qualified title-search route. Tankorent's previously observed PirateBay HTTP 429 remains an upstream availability limitation. Those gates are not closed by restoring Nyaa.

Existing Vault, Linux account and other branch changes remain preserved. Unrelated primary-checkout changes were left untouched. This is a source/runtime checkpoint, not a 1.1.9 v2 release.
