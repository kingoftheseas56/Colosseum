# D1 no-account native integration evidence

Date: 2026-10-14. Scope: one optional Tankoban per-series semantic read, not complete no-account catalogue delivery or release approval.

## Implementation and defaults

The existing `TankobanCatalog` owns async guest issuance and `GET /v1/tankoban/series/{malId}`. Existing valid local SQLite series remain authoritative. Remote results enrich only misses, in an in-memory cache limited to 32 series, with five-minute revalidation on subsequent requests and explicit `requestSeries(id, true)` refresh. Failed revalidation preserves last-good session facts; successful source-hash changes invalidate cached older revisions. No new persistent store, identity authority, database-pack download, full-table pagination or Neon dependency was added.

Responses require API version 1, true logical readiness, a source hash, compatible shapes, at most 2,000 volume rows and 512 KiB. Requests use 15-second timeouts, prohibit automatic redirects, and cancel previous generations. Authentication/revision errors permit one lease refresh, then fail. `ready()` still describes the local complete database; a cached remote series does not make the entire catalogue ready.

Both existing QML series consumers seed the existing Tankoban volume service. With remote enrichment enabled, an explicitly supplied positive integer MAL ID may request volume facts without a local MAL metadata row. Existing `mal:` identity and explicit override behavior are preserved. Title-only missing or ambiguous identity remains unresolved; no AniList ID substitution occurs.

`COLOSSEUM_CONTENT_API_URL` defaults to an empty string. A configured build value must be an HTTPS origin. No production endpoint was enabled, no cloud resources were changed, and the previous catalogue downloader was not cut over by this preparatory slice. That transport and remaining IMDb/MAL/Comics/Tankoban parity operations still require their own qualified replacement before release cutover.

## Actual verification

A focused standalone CMake target compiled only `TankobanCatalog` and its existing expanded QtTest, using MSVC 2022 and Qt 6.11.1 Core/Sql/Network/Test. Build invocation: `cmake --build <isolated focused build> --config Release --target tst_tankoban_catalog`. This was not a full application build or a test of the release branch executable.

The final test process was launched with `Start-Process -Wait -PassThru -WindowStyle Hidden` and Qt's `-o <temporary log>,txt` output. Root process exit was 0. Final results: **16 passed, 0 failed, 1 skipped**, 9,515 ms. Tests cover disabled defaults, actual local HTTP guest/read requests, offline cached reads and failed refresh, incomplete/unknown/oversized results, late-response cancellation, bounded revision renewal, local authority, 32-entry capacity and source-revision invalidation, plus existing synthesis/overlay/numeric ordering tests.

`tests/test_manga_series_catalogue.ps1` passed using the real `MangaSeries.qml` component and fake existing-owner seams. A temporary copy of the same harness loading the real `TankobanSeriesView.qml` also returned `MANGA_SERIES_CATALOGUE_OK`. Additional component cases prove explicit MAL-ID requests with absent local metadata, unchanged namespace, unmatched title-only refusal, wrong-series notification refusal, and matching result delivery to the existing volume service. These are offscreen component runs, not full application or physical-input qualification.

The compiled `tst_tankoban_catalog.exe actual_worker_contract` hook was rejected before execution with **"blocked by policy"**. One retry after Hemanth's explicit authorization, through the same normal `Start-Process` tool path, received the same rejection without further explanation. No native process started, so there was no process exit, test result or native-to-Worker request. The selected binary's focused source files match native commit `f7d56928` and candidate `031d4c87`; neither was exercised by these rejected launches. No bypass or alternate launch was attempted. The local Miniflare/proxy fixture was stopped and its listener removed. The ordinary fixture run skipped this hook; combined native-to-Worker behavior remains unverified.

The explicit nine-file native/QML/test patch passed read-only `git apply --check` against the clean scoped 1.1.9 checkout at `d7f86f4411300f5df840377eaca7ff5859ae2879`. Final owned-file `git diff --check` passed. This proves textual patch compatibility only; the release checkout was not changed, compiled, packaged or integrated, and current master was not merged wholesale.

## Activation and completion gates

Before enabling the endpoint: verify the free-plan hosted Worker/D1 capacity and dependency readiness, complete real guest/read runtime qualification, and confirm the intended production guest rate-limit binding. Broader no-account catalogue parity, old transport removal, distinct 1.1.9 variant metadata/updater targeting, fresh-profile full-app offline behavior and packaging remain separate outstanding work. Optional account restoration remains a later edition after the existing Neon account migration is verified.
