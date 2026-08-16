# Stremio Extension and Debrid Playback Support Implementation Plan

> For agentic workers: REQUIRED SUB-SKILL: use superpowers:executing-plans to execute this plan task-by-task. Each task must be completed and verified before the next task begins.

**Goal:** Make Colosseum support every enabled Stremio stream extension that follows the standard manifest and stream-resource contract, including extensions configured for debrid services, without embedding provider-specific authentication or APIs in Colosseum.

**Architecture:** Colosseum remains a generic Stremio client. It installs and stores the configured add-on manifest URL, requests streams from every enabled stream-resource add-on, normalizes Stremio Torrent and Direct rows, and routes Torrent rows to the existing StreamServer path or Direct rows to the existing mpv HTTP path. The add-on owns Real-Debrid, TorBox, AllDebrid, Premiumize, Debrid-Link, and other provider credentials.

**Tech Stack:** QML/JavaScript, Qt/C++, mpv, Node.js contract tests, PowerShell smoke tests, existing Stremio manifest and stream-resource endpoints.

## Global Constraints

- The existing commit c752caf13cb45bfbb6b4f6b75046eb073db54e09 is the Direct/Torrent playback baseline. The implementation must extend and verify it, not duplicate or replace it.
- Provider-specific API clients, OAuth flows, scraping, API keys, and service account storage are out of scope. Colosseum consumes the configured add-on manifest URL.
- “All extensions” means all enabled, manifest-driven stream add-ons, including the current curated stream rail and arbitrary community add-ons installed by manifest URL. Provider logo mappings are not provider integrations.
- Removable stream extensions remain user-controlled. They must not all become enabled automatically on first run.
- A Stremio row with infoHash is Torrent. A row with url is Direct. The app must not silently reinterpret one as the other.
- Configured manifest URLs can contain provider-specific path segments or other sensitive configuration. Preserve the URL needed for requests, redact it from logs and diagnostics, and show only safe extension identity in the UI.
- Player 2 is retired and is not part of this implementation. Do not modify qml/player2, qml/player2host, or their tests.
- Existing unrelated worktree changes must remain untouched. Work directly on master per Brotherhood Rule 28; do not create a worktree or side branch.

---

## Current Extension Coverage Baseline

The implementation starts from the current curated catalog in qml/ExtensionsCatalog.js:

| Catalog group | Current entries | Required behavior |
|---|---|---|
| Featured | Torrentio | Installable, user-enabled, configured manifest supported |
| Stream rail | Comet, MediaFusion, AIOStreams, Peerflix, NoTorrent, WebStreamr | Each can be enabled independently and queried through the generic stream path |
| Extras | Meteor for the Weebs | Included when its installed manifest advertises stream resources |
| Core/non-stream | Cinemeta, Anime Kitsu, OpenSubtitles and other catalog/subtitle add-ons | Must remain unaffected by stream filtering |
| Community | Any valid installed manifest | Same generic behavior without a provider-name switch |

The live manifest matrix must record what each current manifest advertises at verification time. It must distinguish Direct rows, Torrent rows, configuration-required manifests, unavailable endpoints, and extensions that no longer expose streams. The matrix must never infer support from an extension name or logo.

## File Map

- qml/AddonClient.js — manifest URL normalization, stream endpoint construction, stream-row normalization, and enabled-extension fan-out.
- native/engine/ExtensionsStore.cpp and native/engine/ExtensionsStore.h — installed-manifest persistence, first-run enablement, configured-manifest identity, and safe metadata exposure.
- qml/ExtensionsPage.qml and qml/ExtensionsSources.qml — configure-required state, configure/install guidance, and safe display of configured extensions.
- qml/ExtensionsCatalog.js — curated extension inventory only; no provider-specific playback branches.
- qml/PlayerPage.qml — primary Direct/Torrent playback and reconnect behavior.
- tests/addon_direct_stream_contract_test.mjs — Direct/Torrent/header contract.
- tests/stremio_extension_matrix_test.mjs — multi-extension manifest and endpoint behavior.
- tests/fixtures/stremio/ — deterministic manifests and stream responses with no real credentials.
- tests/auto/extensions/tst_extensions_first_run.cpp — first-run and enablement behavior.
- tests/extensions_catalog_test.mjs, tests/extension_worlds_derivation_test.mjs, and tests/extension_reorder_world_test.mjs — catalog and extension-world regression coverage.
- tests/test_player_p0_parity.ps1 and tests/test_player_adjacent_extensions_p0.ps1 — playback and adjacent-extension smoke coverage.
- docs/encyclopedia/extensions.md — extension lifecycle, configuration, and stream request contract.
- docs/encyclopedia/player.md — Direct/Torrent routing, headers, reconnects, and supported limitations.
- docs/research/stremio-debrid-extension-matrix.md — dated live coverage evidence and provider-neutral results.

## Task 1: Establish the Extension Matrix and Baseline Evidence

**Files:**

- Create docs/research/stremio-debrid-extension-matrix.md.
- Read qml/ExtensionsCatalog.js, native/engine/ExtensionsStore.cpp, qml/AddonClient.js, and the existing extension tests.

**Steps:**

1. Record the current curated manifest URLs and classify each entry as featured, stream, extra, core, or non-stream.
2. Record the first-run state for each seeded entry and the rule that removable stream wells remain disabled until the user enables them.
3. Record the exact generic stream contract already implemented by c752caf: infoHash/fileIdx for Torrent, url for Direct, nested request headers, response-only header exclusion, and headerless retry cleanup.
4. Add a matrix row for arbitrary community manifests so the plan is tested against capability, not a hardcoded provider list.
5. Run node tests/extensions_catalog_test.mjs, node tests/extension_worlds_derivation_test.mjs, and node tests/extension_reorder_world_test.mjs. Preserve the existing known failure in tests/test_player_p0_parity.ps1 as a baseline until its unrelated assertion is separately addressed.

**Verification:** The matrix names every current curated stream entry, states its expected enablement behavior, and separates known baseline failures from new work.

## Task 2: Make Configured Manifest URLs Requestable Without Losing Add-On Identity

**Files:**

- Modify qml/AddonClient.js.
- Create tests/stremio_extension_matrix_test.mjs.
- Create deterministic fixtures under tests/fixtures/stremio/.

**Interfaces and behavior:**

- Add one internal stream-endpoint resolver that accepts a stored transport URL, a Stremio type, and an item id.
- A root manifest URL such as https://host/manifest.json must resolve to https://host/stream/movie/id.json.
- A configured path URL such as https://host/configured-state/manifest.json must resolve under the same configured-state prefix.
- A configured URL with query parameters must retain the parameters required by that add-on while appending the stream path correctly.
- The resolver must reject malformed URLs before issuing a network request and must not log the raw configured URL.
- loadStreams must use the resolver for every enabled stream extension; no extension-name conditional may be introduced.

**Steps:**

1. Write failing contract cases for root, configured-path, configured-query, malformed, and non-stream manifest URLs.
2. Add fixture manifests for Torrentio, Comet, MediaFusion, AIOStreams, Peerflix, NoTorrent, WebStreamr, Meteor, and one arbitrary community add-on. The fixture names are labels only; the response payloads must exercise the generic contract.
3. Stub the transport layer in the Node test so endpoint construction, request count, and safe error behavior are deterministic.
4. Implement the resolver and route all stream requests through it.
5. Add a regression assertion that disabling one extension prevents its request while all other enabled stream extensions still receive the request.

**Verification:** node tests/stremio_extension_matrix_test.mjs passes with all endpoint forms, no raw configured URL appears in captured logs, and one disabled extension produces zero requests.

## Task 3: Handle Configuration-Required Extensions as a First-Class State

**Files:**

- Modify native/engine/ExtensionsStore.cpp and native/engine/ExtensionsStore.h only where installed-manifest metadata or safe display state is required.
- Modify qml/ExtensionsPage.qml.
- Modify qml/ExtensionsSources.qml.
- Modify qml/ExtensionsCatalog.js so resource-less configuration-required manifests remain visible in the Theatre extension surface.
- Extend tests/auto/extensions/tst_extensions_first_run.cpp and the relevant extension-world Node tests.

**Steps:**

1. Add fixture coverage for behaviorHints.configurable and behaviorHints.configurationRequired.
2. Preserve both flags through manifest slimming, install, update, reload, and restart.
3. Expose a safe UI state for “Configure required” without displaying the configured URL or credential-bearing path.
4. Keep Configure ↗ routed to the add-on’s configure endpoint using the existing external configuration model.
5. Add an explicit install path for the final configured manifest URL through the existing manifest install input. The UI copy must explain that the configured manifest URL is what Colosseum needs to install and query.
6. Ensure a configuration-required extension is not treated as broken merely because its unconfigured base manifest returns no playable streams.
7. Verify that updating a configured add-on preserves its enabled state and configured transport URL rather than replacing it with the unconfigured catalog URL.
8. Make catalogue installs pass through the manifest preview so a configuration-required base manifest opens Configure instead of being installed as a dead-end row.

**Verification:** C++ extension tests and Node extension-world tests prove flags and configured identity survive install/update/restart; QML smoke confirms the user can see that configuration is required, open Configure, and install the resulting manifest without exposing credentials.

## Task 4: Verify Generic Direct/Torrent Playback on the Production Player

**Files:**

- Audit and, if the contract test is red, modify qml/PlayerPage.qml.
- Extend tests/addon_direct_stream_contract_test.mjs.
- Extend tests/test_player_p0_parity.ps1 and tests/test_player_adjacent_extensions_p0.ps1 where the existing player smoke contract is missing.

**Required contract:**

- Direct rows call the mpv HTTP path with the row URL and request headers.
- Torrent rows call the existing StreamServer path with infoHash/fileIdx.
- Nested behaviorHints.proxyHeaders.request is flattened into request headers.
- Response-only proxy headers are ignored.
- Legacy flat header maps remain accepted.
- Array-shaped headers are rejected.
- A later headerless Direct load clears headers from the prior Direct load.
- Retry and wake/reconnect use the same Direct/Torrent decision as initial playback.
- No Direct row is sent to the torrent engine or StreamServer.

**Steps:**

1. Confirm the existing primary PlayerPage Direct/Torrent contract before touching playback code.
2. Exercise initial load, retry, wake reconnect, and header cleanup for both Direct and Torrent rows.
3. Keep Download, Cast, and long-term Continue behavior explicitly outside this task unless the existing primary player path already consumes the same playable URL contract. Do not claim expiring provider URLs are durable files.
4. Keep the existing unrelated player-parity failure isolated and report it separately.

**Verification:** node tests/addon_direct_stream_contract_test.mjs passes; the targeted PowerShell playback checks pass; any remaining failure is identified by assertion and is not relabeled as an extension-support success.

## Task 5: Prove Multi-Extension Fan-Out and Result Isolation

**Files:**

- Modify qml/AddonClient.js only where required by the matrix tests.
- Extend tests/stremio_extension_matrix_test.mjs.
- Add or extend tests/fixtures/stremio/stream-responses/.

**Steps:**

1. Create fixture responses where one add-on returns Direct rows, one returns Torrent rows, one returns both, one returns malformed rows, and one returns no results.
2. Assert that every enabled stream add-on is queried independently for the same type and id.
3. Assert that one timeout, malformed response, or HTTP error does not discard valid results from other add-ons.
4. Assert that stream rows retain source add-on identity, stream kind, file index, subtitles, quality metadata, and request headers through aggregation.
5. Assert that duplicate rows from separate add-ons remain distinguishable until the existing ranking/deduplication layer makes an intentional decision.
6. Assert that non-stream add-ons never enter the stream request fan-out.
7. Assert that configuration-required add-ons use their configured transport URL and do not fall back silently to the unconfigured catalog URL.

**Verification:** The matrix test passes with mixed Direct/Torrent/error results and demonstrates that one extension cannot overwrite or suppress another extension’s playable rows.

## Task 6: Validate the Curated and Community Extension Set Against Live Manifests

**Files:**

- Update docs/research/stremio-debrid-extension-matrix.md.
- Do not store credentials, configured manifest URLs, response bodies containing tokens, or personal account data in the repository.

**Steps:**

1. From a controlled local session, inspect each current curated stream manifest: Torrentio, Comet, MediaFusion, AIOStreams, Peerflix, NoTorrent, WebStreamr, and Meteor when it advertises stream resources.
2. For each available extension, record the manifest resources, configuration flags, endpoint shape, and whether the returned sample contains Direct rows, Torrent rows, or both.
3. Use only user-provided configured add-on URLs for debrid playback smoke. Do not create provider accounts or call provider APIs from Colosseum.
4. For each configured add-on that returns a playable row, verify one Torrent path and one Direct path where available. Direct playback must include the request-header case when the add-on supplies headers.
5. Verify one arbitrary community add-on installed by manifest URL to prove the implementation is not limited to the curated catalog.
6. Mark unavailable, retired, configuration-required, or non-stream manifests as such with the date and observed reason. Do not turn an unavailable live service into a code failure.
7. Capture only redacted endpoint shapes and behavioral results in the matrix.

**Verification:** Every current curated stream entry has a dated evidence row; every available configured extension either plays through the generic contract or has a concrete, reproducible incompatibility recorded.

## Task 7: Update Encyclopedia Documentation and Code Fingerprints

**Files:**

- Update docs/encyclopedia/extensions.md.
- Update docs/encyclopedia/player.md.
- Update docs/research/stremio-debrid-extension-matrix.md.
- Refresh only the required encyclopedia index/state files through scripts/code_encyclopedia.py.

**Documentation requirements:**

- Explain that a Stremio extension, not Colosseum, owns debrid provider authentication.
- Document the configured-manifest URL lifecycle and the safe display/logging rule.
- Document the enabled-extension fan-out and the Direct/Torrent row contract.
- Document request-header normalization and the fact that response-only headers are excluded.
- Document first-run stream-extension enablement and the configure-required state.
- Document the limits around expiring Direct URLs for Download, Cast, and Continue.
- Include the dated live extension matrix and distinguish fixture coverage from live provider coverage.

**Verification:** Run the extensions and player encyclopedia checks; both report CHECK OK. Review the generated index/state diff to confirm it contains only accepted fingerprints for files intentionally changed in this work.

## Task 8: Full Verification, Independent Review, and Handoff

**Files:**

- No additional source files unless verification exposes a defect; the configuration-required visibility fix is scoped to qml/ExtensionsCatalog.js.
- Review the complete scoped diff and the matrix evidence.

**Steps:**

1. Run node tests/addon_direct_stream_contract_test.mjs.
2. Run node tests/stremio_extension_matrix_test.mjs.
3. Run node tests/extensions_catalog_test.mjs.
4. Run node tests/extension_worlds_derivation_test.mjs and node tests/extension_reorder_world_test.mjs.
5. Run the targeted extension C++ tests through the repository-supported test command.
6. Run tests/test_player_p0_parity.ps1 and tests/test_player_adjacent_extensions_p0.ps1, recording pre-existing failures separately.
7. Run git diff --check and the encyclopedia checks.
8. Request an independent cross-substrate review against this plan and the Definition of Done before committing. The reviewer must check generic extension coverage, configured-manifest preservation, Direct/Torrent routing, credential boundaries, and first-run enablement.
9. Commit only the implementation, tests, documentation, matrix, and required fingerprint updates. Do not stage unrelated existing worktree changes.

## Definition of Done

- Every enabled standard Stremio stream add-on is queried through one generic path; no provider-name switch is required.
- The current curated stream entries and one arbitrary community manifest are covered by deterministic fixtures and live evidence where available.
- Configured manifest path/query state survives installation and update and is used to build stream requests correctly.
- Configuration-required extensions have a clear UI state and a documented path from external Configure to installation of the final manifest URL.
- Direct rows reach mpv with request headers; Torrent rows reach the existing StreamServer path; mixed results from multiple extensions remain isolated.
- Initial playback, retry, wake reconnect, and header cleanup obey the same Direct/Torrent contract.
- Stream extensions remain disabled by default when the existing first-run policy requires user enablement.
- No provider API, credential, OAuth, or scraper code is added to Colosseum.
- Deterministic tests, targeted C++ tests, player smoke checks, encyclopedia checks, and independent review all pass, with unrelated baseline failures called out precisely.
- The repository documentation explains what “debrid support” means here: configured Stremio debrid add-ons can supply playable Direct/Torrent streams, while the add-ons remain responsible for provider authentication.
