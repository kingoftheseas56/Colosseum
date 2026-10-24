# Tankoyomi Workers checkpoint — 2026-10-23

**Status: partial checkpoint; release held.** The Worker migration is deployed and exercised through the real Windows app. MangaSin and LectorXD remain disabled candidates because live source qualification failed. No 1.1.9 v2 package or release was created.

## Change and provenance

- Base: GitHub master `b8c08970dd01b262483b1cca71cc37cdc46fe2f8`, including the Vault background fix. Existing unrelated working-tree changes were preserved in their original checkout.
- MangaSin implementation and parser fixture were reviewed from `preflight/tankoyomi-mangasin-20261022` at `8291453c55e21b2d08493d92eeb694f737d78fea`. Source-specific native crypto changes were not imported; the Worker supplies a generic, tested CryptoJS-compatible decrypt helper.
- LectorXD direct-series/chapter parsing follows the pinned [HaruNeko adapter](https://github.com/MikeZeDev/haruneko/blob/88ce1fa5e2e08a00949d2f00619c4bb1e17de29f/web/src/engine/websites/LectorXD.ts). Its title search deliberately reports unsupported: no verified route was found.
- Production resources contain the bootstrap manifest only. The Worker imports all 12 provider implementations; local JavaScript execution remains available to existing fixture harnesses.
- The app uses the existing canonical identity, language policy, settings overlay, download and reader owners. It caches validated remote inventory and retains valid inventory if refresh fails. House-default generation 15 migrates the old built-in Tankoyomi URL while preserving user enablement, order and custom URLs.

## Deployment

- Service: [colosseum-tankoyomi](https://colosseum-tankoyomi.colosseum-watchparty-relay.workers.dev/healthz).
- Manifest revision: `0.2.1`; contract version: `1`.
- Deployment ID: `4d078a43e401488b959a06e5c6fab32e`.
- Built Worker bundle SHA-256: `EC99EF88571E2263E014823564661F16E8EFE296F7C3A6E3890DCE8A408E8FC3`.
- Public health and manifest returned HTTP 200. The published manifest matched the checked-in manifest exactly after JSON normalization.
- Only the dedicated new service was deployed. Other Workers, addons, release feeds and releases were not changed.

The Worker returns metadata; media bytes use the app's existing public-HTTPS image transport. Provider metadata requests and redirects are host-restricted, with size, duration and request-count limits. The public endpoint has no account requirement. Production traffic/quota qualification remains outside this local checkpoint.

## Verification performed

- Native Windows `colosseum_runtime_ready` build succeeded. Executable SHA-256: `AB006DBAC6F5AE2E9A3688DD89DFD88E7E8B661B18C4522781AD0E9969B87D3F`.
- Worker unit tests: 11 passed, 0 failed; includes actual production provider imports, host/redirect restrictions, bounded reads, timeout, request budget and fixed crypto vector.
- Extension inventory, JavaScript runtime, MangaSin parser and LectorXD parser contract scripts passed.
- Native Worker transport harness passed, including its 45-second deadline case. After a URL-format warning correction, rebuilt fast cases passed again.
- Configuration-store harness passed, including remote inventory replacement preserving the persisted user overlay.
- Fresh native CTest: 2/2 passed (Worker transport, including the 45-second deadline, and service configuration). The service test covers asynchronous refresh, persisted enabled/order overrides, new inventory discovery, and rejection of invalid JSON/registry, oversized, redirect and non-200 responses without replacing the cache. A separate receiving rerun of the service harness passed. An initial run without Qt DLLs on PATH failed to start; the corrected environment passed.
- Extension first-run/migration Qt tests: 25 passed, 0 failed.

Actual app tests used an isolated profile, the real `Main.qml`, the native executable, its existing chapter page, downloader and comic reader, with an offscreen rendered window. They were agent-run tests, not human acceptance or installer tests.

| App path | Observed result |
| --- | --- |
| English / WeebCentral / One Piece chapter 1 | Worker returned 1,196 chapter rows; native downloader completed 57 pages (18.7 MB); real reader became ready and rendered a chapter image. |
| Spanish route / NiAdd / One Piece | Downloaded 18 pages (4.4 MB) and rendered in the reader. Visual inspection showed English dialogue, so this is transport proof, not Spanish-content qualification. Its existing chapter-number extraction also selected a date-derived number; it is not chapter-1 proof. |
| Spanish / ZonaTMO / One Piece capítulo 0 | Worker returned 1,239 chapter rows; native downloader completed 25 pages (3.8 MB); real reader became ready. A further cached-reader run rendered dialogue visibly in Spanish. |
| MangaSin enabled alone | After the startup splash disappeared, the chapter page visibly rendered the HTTP 502 error with zero chapters and no cross-language/provider fallback. Upstream Worker response identified HTTP 403. |
| LectorXD enabled alone | After the startup splash disappeared, the chapter page visibly rendered the HTTP 502 error with zero chapters and no cross-language/provider fallback. The Worker reported unsupported title search. A separate direct-series request received upstream HTTP 403. |

The initial blocked-provider captures occurred behind the splash and were rejected as visual proof. Corrected runs waited for `bootSplash.visible == false` and checked the actual rendered error Text before capture.

## Remaining qualification

1. MangaSin needs a usable upstream path from Workers and a complete live search → chapters → images → app reader check before default enablement.
2. LectorXD needs a verified title-search implementation, usable upstream access and the same complete live check. Direct-series fixtures alone do not satisfy the app's title discovery path.
3. Existing provider availability is mixed. Metadata checks succeeded for several English/Spanish/French routes, while other Portuguese/French routes returned upstream failures or empty chapters. A page-URL response alone is not reader qualification; complete provider parity is not established.
4. The app still logs the pre-existing `Main.qml:1991` `universeHallLayer` reference error and some poster-fetch failures; these files were not changed by this checkpoint.
5. No Linux build/runtime or account acceptance was rerun here. Integrated Windows/Linux qualification and the 1.1.9 v2 release are later checkpoints. Only configurable addon systems Tankoyomi and Tankorent move to Workers; standalone Nyaa, LibGen, AudioBookBay and GetComics integrations and downloads remain native.

## Spanish source follow-up — 2026-10-25

**Status: parser improvement verified; live source enablement blocked.** Both registrations remain disabled. This follow-up does not change the deployed revision `0.2.1` or publish a release.

### MangaSin

- The actual production Worker handler, run locally with normal upstream requests, returned 20 search results, 837 One Piece chapters and 56 HTTPS page URLs for chapter 1. The current chapter key and ordinary reader-page parser still work. Image payloads were not fetched in this check.
- Two spaced requests to the deployed Worker returned HTTP 502 with `Upstream returned HTTP 403`. Direct requests succeeded. This isolates the failure to the hosted access path, but does not establish which upstream IP, runtime or edge policy caused it.
- A source-provided fanmade row numbered zero sorts first and returns no pages. Chapter 1 was selected explicitly for the metadata check; source numbering and ordering were not silently changed.

### LectorXD

- Updated direct-series chapter parsing to the Astro ChapterList contract in [HaruNeko commit f00b48e](https://github.com/MikeZeDev/haruneko/blob/f00b48e4e2c48252d419e73cb2041baa127a0ca8/web/src/engine/websites/LectorXD.ts). Serialization follows the pinned [official Astro serializer](https://github.com/withastro/astro/blob/5978993531ba3f02a4c639d223f810e80a1fa7d3/packages/astro/src/runtime/server/serialize.ts).
- Reads only serialized chapter data, without executing upstream scripts. Parsing is bounded to 1 MiB, JSON depth 32 and 8,000 chapters. Unrelated fields are ignored; canonical chapter paths are validated, deduplicated and sorted. Legacy anchors remain supported when no ChapterList island exists; malformed explicit props fail instead of falling back.
- Title search remains explicitly unsupported: catalogue pagination does not establish a title-search protocol. Normal direct catalogue access returned a Cloudflare block page with HTTP 403, and the deployed direct-series request also received upstream HTTP 403.

### Verification and release boundary

- MangaSin and LectorXD parser contracts passed. All 15 Worker tests passed, including production-provider API routing, Astro fixtures, upstream denial handling and unsupported-search behavior. A 1,200-chapter fixture with unrelated metadata also passed.
- Fresh isolated-profile runs of the actual Windows app visibly rendered HTTP 502 with zero chapters for each source, after the startup splash disappeared. Screenshots were inspected. These are error-path checks against the existing deployment, not successful reader checks for the parser follow-up.
- Neither candidate has completed hosted search → chapters → images → actual app reader qualification. MangaSin requires usable Worker access; LectorXD additionally requires a verified title-search implementation. Keep both disabled until those checks pass.
