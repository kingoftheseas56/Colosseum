# Stremio Sync implementation plan

Status: Approved by Hemanth for execution on 2026-09-19. Implementation is in progress.

Contract: [Approved Stremio Sync Design](stremio-sync-design.md), unchanged.

Inspection baseline: `d565df14` on `master`, 2026-09-19.

Execution branch: `codex/stremio-sync`, created from plan commit `581172a5`.

Merge endpoint: merge this branch into `master` only after Tasks 1-5, their focused reviews, repository gates, and runtime qualification pass. If that endpoint is abandoned, record a retire decision instead of leaving an active branch or worktree.

Author: [Scoped helper (Codex / Astra), implementation planning].

## Delivery boundary

Five tasks, in order. Reuse the native stores, profile lifecycle, Windows credential operations, Neon adapters/outbox, and existing QML controls. Add one Stremio-specific native owner, its small protocol/merge helpers and private persistence, one marker adapter into the existing Neon registry, and one panel. These are concrete Stremio code, not an external-provider framework. No changes to the approved design, future-provider work, replacement account system, playback architecture, or generic registries.

Done when the twelve specification acceptance criteria have deterministic coverage and the relevant isolated running-app journeys pass, including browser authentication with a designated test account. This document authorizes planning only. Execution and deployment occur in the implementation session.

## Source findings and minimum changes

| Existing code | Reuse / required change |
|---|---|
| `native/account/ProfileStoreRuntime.{h,cpp}`, `ProfilePaths.{h,cpp}` | Already own isolated local/account stores and emit `storesAboutToChange` / `storesChanged`. Bind Stremio lifetime to these events; add paths for Stremio state and the profile's Theatre addon portion. Sealed profiles cannot connect. |
| `native/account/WindowsAccountCredentialStore.{h,cpp}` | Reuse its native generic-credential operations and test-tag namespace. Its single active **Neon** slot is unsuitable for Stremio: add a separate Stremio namespace keyed by profile. Do not put a Stremio key into `StoredAccountCredential::refreshToken` or touch Neon revocation work. |
| `native/CollectionStore.h`, `ProgressStore.h`; `account/CollectionSyncAdapter.cpp`, `ProgressSyncAdapter.cpp`, `WatchStateSyncAdapter.cpp` | Reuse canonical records, portable projections, notifications and durable import callbacks. Imported writes deliberately suppress `syncDirty`; add an explicit relay receipt rather than globally removing suppression. Watch marks currently carry only `id` and `mark`, so real-action timestamps must be added. |
| `native/account/SyncEngine.{h,cpp}`, `SyncStateStore.{h,cpp}`, `SyncAdapterRegistry.{h,cpp}` | Reuse Neon reconciliation, durable outbox, owner receipts, retry and quarantine. The engine requires an account profile and speaks the Neon protocol; it cannot be the local-only Stremio transport. Add the minimum import/reconcile hook and use its asynchronous `QSaveFile` persistence pattern for separate Stremio work. Do not route Stremio secrets or addon documents through its wire outbox. |
| `native/account/ProfilePreferencesStore.*`, `ProfilePreferencesSyncAdapter.cpp`, `SyncOwnershipInventory.*`, `SyncPayloadFirewall.cpp` | Preferences have a durable profile home, but the existing adapter accepts exactly one explicit-content field. Add the `mainSyncProvider` marker to the owner and a tiny **Stremio-link** record adapter in the existing registry; do not broaden the explicit-content payload or add generic settings blobs. |
| `native/account/HistoryStore.cpp`, `HistorySyncAdapter.cpp`, `ConsumptionHistoryBridge.cpp` | History already has independent durable records, completion facts, privacy resets and sync. Its normalizer reconstructs records and drops extra fields. Add narrowly validated Stremio provenance/display fields end to end; never create `ActivityStore` playback or completion facts for an import. |
| `qml/account/AccountCenter.qml`, `AccountYourColosseumPage.qml`, `AccountActivityFormat.js` | The current recent-activity list and metrics both derive from `ProfileActivity.projectMonth()`. Merge imported History rows into the **list presentation only**; leave every metric on the existing projection. Writing `HistoryStore` alone would not display the imported activity. |
| `native/engine/ExtensionsStore.*`, `native/main.cpp`, `qml/ExtensionsPage.qml`, `ExtensionsCatalog.js`, `AddonClient.js` | Already load, fetch, validate, normalize, persist and consume ordered addons. Today one global JSON file is constructed in `main.cpp`, installs replace by manifest ID, and mutations address that ID. Scope Theatre's compatible collection to the active profile, use transport identity for configured instances, and preserve native/non-Theatre extensions. Existing `saveIndex()` silently ignores failures; provider acknowledgement requires a real persistence result. |
| `native/account/LegacyPersonalStateStorage.*`, `FirstAccountProfileCoordinator.cpp`, `ProfileAdoption.cpp`, `AccountAttachmentCoordinator.cpp`, `AccountRuntime.cpp` | Existing staged adoption, rollback, attachment receipts and directory promotion already carry media state. Extend their explicit snapshot/verification surfaces for the marker, watched timestamps and new local files; migrate the device credential separately. Do not replace the adoption process. |
| `qml/TopBar.qml`, `WorldPage.qml`, `Main.qml`, `TheatreWorld.qml`, `LibraryPage.qml`, `LibraryButton.qml` | Reuse top-bar intent signals, same-window overlays, keyboard controls and library routes. Theatre has a direct removal handler and shared LibraryButton also removes directly: both must reach the approved two-action removal flow. |
| `server/account-service/internal/account/sync_policy.go`, `sync_merge.go` | Server category and History/watch schemas are strict. Add the marker and exact schema extensions here as well as native validators. Existing Neon LWW uses HLC, which is not proof of newest viewing time; bound the activity-time comparison to affected Theatre records. Existing tables can carry these records; no database replacement is needed. |

Several live account files retain old `PRE-FLIGHT DRAFT` comments despite being wired into the application and registered tests. Actual callers and tests above establish reuse; historical labels confer no design authority.

## Execution rules shared by the five tasks

- Keep the new native code under `native/stremio/`: `StremioSync.{h,cpp}` owns authentication, connection lifetime and scheduling; `StremioCodec.{h,cpp}` holds pure wire/identity/merge functions; `StremioState.{h,cpp}` holds the private journal and its async writer. Use plain values and callbacks for tests. Do not create transport interface hierarchies, a second sync registry or a generic job system.
- Build and register new native tests in the existing `tests/CMakeLists.txt` / `native/CMakeLists.txt` graph. One new `tests/auto/stremio_sync/tst_stremio_sync.cpp` target can cover service, codec and journal cases; extend existing owner suites where the owner changes. One `tests/qml/tst_stremio_sync.qml` joins the existing `colosseum.qml` runner. These are **planned additions**, not tests that already exist.
- Read `docs/colosseum-test-verification.md` and `docs/colosseum-lanista-verification.md` during execution. Their core commands exist; their older account-test census is incomplete. `tests/CMakeLists.txt` currently registers `account_identity`, `account_adoption`, `account_attachment_coordinator`, `account_attachment_runtime`, `account_shared_pc`, `profile_activity_isolation`, `sync_engine`, `sync_core`, `sync_inventory`, `sync_adapter_registry`, `sync_protocol`, `core_sync_adapters`, `history_sync`, `profile_preferences_sync`, `consumption_history` and `privacy_policy`, with prefix `colosseum.qttest.`. Update the ledger for touched suites/new cases rather than inventing pre-existing coverage.
- Use the supported Windows build in `docs/build/windows.md`; establish baseline results once. Per task, build affected targets and run focused cases. Run the full registered `unit` gate and QML aggregate at final qualification. Keep pre-existing failures distinguishable from new ones.
- Qt tests use temporary profile roots, injected time and a loopback Stremio fixture; no live network in deterministic tests. Synthetic credentials must never share the daily credential namespace. Each regression case must fail under its named negative control, then pass restored.
- Runtime sessions use `lanista session run`, a unique pipe and tagged AppData/cache roots confirmed by `get-state`. Available operations are `ping`, `ui-query`, `qml-get`, `ui-click`, `ui-keypress`, strict-equality `ui-wait-for`, `get-state`, `log-mark` and combined grabs. No new Lanista command or event system is needed. Do not automate the daily app or seed live data.
- Add only a named QML projection of non-secret service state plus normal control `objectName`s. The planned `stremioSyncState` exposes `status`, `pendingCount`, `lastSuccessAt`, `activeProfileId`, `mergeComplete` and a monotonically advancing `completedRun`. A loopback-only fixture reports its expected records and acknowledgements independently. Wait on exact values/run counters, never sleeps or a transient `Syncing` state.
- Evidence goes under ignored `artifacts/stremio-sync/<task>/<run>/`: test results, baseline/negative-control records, session manifest, redacted fixture assertions, warning verdict and UI grabs. No credential, callback URL, configured addon URL or raw private provider response enters artifacts. No broad logging of requests on the Stremio network path.

### Task 1: Secure profile connection and durable Stremio work

Purpose: Establish a testable connection that works without Neon and cannot cross profiles or lose pending intent.

Dependencies: None.

Implementation guidance:

1. Add the three small Stremio files above. Implement bounded native requests with the app's Qt network facilities, injectable endpoint/clock/browser opener in tests, and strict production HTTPS destinations. Inspect the pinned Harbor browser/API references below for payload contracts; check returned account identity with `getUser` before saving a key. No password UI.
2. Bind an ephemeral `127.0.0.1` listener, generate a cryptographically random callback path/nonce, then open the official login URL with `appName=Colosseum` and encoded `appCallback`. Accept one bounded, correctly correlated callback (`key`/`authKey`), reject ambiguity, close on success/cancel/timeout/profile replacement, and reject late validation replies. Do not assume Stremio supports an unverified OAuth `state` parameter: the nonce belongs in the callback URL. Use a response page with no remote resources or echoed credential.
3. Extend the existing Windows credential implementation with profile-keyed Stremio save/load/delete operations. Include the existing hashed test tag in targets. A stored key is usable only with its matching profile/account binding; unsupported secure storage fails closed without a plaintext fallback. Keep raw keys out of QML and serialization.
4. Add a profile-local `stremio-sync.json` journal with version, binding generation, non-secret account identity/display name, acknowledged provider baselines, pending desired mutations, import redo/relay receipts, intentional membership differences, last success and first-merge state. Configured addon state stays in the profile-local addon file/journal only. Reuse the asynchronous serialized `QSaveFile` pattern from `SyncStateStore`; do not change its Neon-specific schema to fit Stremio. Persist an intent before sending it; keep it until both remote acknowledgement and required local receipts are durable. Coalesce replaceable progress work, not explicit removals or uncommitted import receipts.
5. Wire lifetime through `ProfileStoreRuntime` events. Invalidate requests, callbacks, queued work and timers against captured profile **and binding generation** before stores are replaced. Journal load failure pauses this connection without reseeding or discarding pending work. Retry transient failures with capped backoff; authentication failure requires reconnect. Network/persistence waits must not block playback or GUI input.
6. Persist only `mainSyncProvider` (`stremio` or unset) through `ProfilePreferencesStore` and a small `StremioLinkSyncAdapter` in the existing registry. Add its exact fixed key/payload to `SyncOwnershipInventory`, its JSON inventory/freeze files, native validation and server `sync_policy.go`. Link health and Stremio account identity remain local. Old records lacking the marker remain valid. Keep existing firewalls and add explicit rejection cases for `authKey` and configured URLs. Server acceptance must ship before a client sends the new record.
7. Establish the new test target, synthetic loopback fixture and sanitized `stremioSyncState` projection before later runtime journeys. Fixture injection is restricted to a tagged test instance and loopback endpoints; production auth still uses the official browser flow. No fake connect control appears in the product.

Behavior to preserve: Neon login/refresh/revocation, local-only use, profile sealing, existing outbox and tag isolation.

Baseline: Confirm direct `ExtensionsStore` construction and account-only `SyncEngine::start`; record existing identity/shared-PC/sync tests before changes.

Focused tests:
- Qt Test: new target covers forged/replayed/oversized callback, cancellation and late replies, vault failure, A-to-B profile switch, reconnect, malformed journal, crash-before-send/after-ack, retry backoff and marker round-trip. Extend identity, inventory and registry tests and Go policy tests.
- Qt Quick Test: not applicable to the internal connection; the state projection receives a load/read smoke case only.
- Existing harnesses: affected account/sync tests named above; existing store-isolation test.
- Negative control: disable nonce/generation validation, omit the persistence-before-send barrier, or permit `authKey` in marker payload; corresponding tests must fail.

Test seam status: test blocked today for Stremio-specific cases; this task creates and registers the prerequisite seam before dependent work.

Lanista actions: no connection UI yet; tagged fixture startup, `ping`, `get-state`, `qml-get` on the new state projection.

Completion signal: journal commit receipts and test completion; state projection reports the fixture profile and exact terminal status.

State / events / probes: no cross-profile request; fixture counts no unauthorized send; no secret-valued QML properties.

Visual evidence: not applicable to the internal service.

Regression paths: restart with queued work; local-to-account profile replacement; failed auth followed by retry; Neon credential retained.

Evidence artifacts: task-1 test and fixture receipts under the shared evidence path.

Bridge status: not applicable to the internal delivery; the named projection/fixture is the prerequisite for tasks 3–5.

Completion criterion: connection/journal contracts pass without a Neon session, and later tasks have an isolated, non-secret fixture seam.

### Task 2: Library, progress, watched state, History and Neon relay

Purpose: Make media state converge safely while preserving real viewing time and explicit deletion intent.

Dependencies: Task 1.

Implementation guidance:

1. Implement Stremio `datastoreMeta` / `datastoreGet` / `datastorePut` for `libraryItem` with bounded batching. Preserve unrelated fields from the fetched item when patching it. Distinguish library membership (`removed` / `temp`) from playback state: a temporary watch record must not silently add a title to Library. Isolate malformed records instead of failing the batch.
2. Map collection `world=theatre`, progress `kind=video`, root title IDs and exact episode IDs to existing Theatre shapes (`TheatreSeries.collectionEntry`, player progress, `LibraryApi`). History uses `movie` / `episode` and the existing `ActivityLaneHelpers.videoIdentityFor` / `Player2ActivityHelpers` identity convention, not progress's `video` kind. Preserve provider prefixes; never split all IDs on a fixed colon count or invent IMDb matches. Keep existing local resume/download fields through `CoreStateSyncProjection::mergePortableIntoLocal`. Convert Stremio millisecond positions/durations to Colosseum seconds explicitly. Where player records omit duration, add that scalar at their existing record construction points; do not introduce a playback recorder. Preserve the silent five-second write and current completion rule.
3. Decode/encode movie watched status and Stremio's anchored compressed episode-watched field with bounded decompression and stable video ordering. Reuse existing addon metadata access for the episode list. A missing/ambiguous episode map preserves raw remote state and leaves that item pending; it must never clear watched bits or turn one completed episode into a completed series. Apply imported episode state to existing per-episode progress, not a series-root manual override.
4. Add real action timestamps to local watched overrides and their existing `watch_state` payload, persistence, adoption snapshot and native/Go validation. Legacy marks remain readable with unknown time; observing them during sync must not manufacture a new watch event. Compare known real activity times, not `getUser` time, polling time, `_mtime` from a library metadata edit or newly allocated Neon HLC. Equal/missing times use a deterministic documented tie-break that preserves the acknowledged winner and cannot oscillate; undated imports must not invent a History date.
5. Reconcile the winning *current* playback/watched state using the specification rule in both directions. Keep cumulative History separate: old completion evidence must not override newer partial viewing merely because `LibraryApi` checks History. Update the narrow Theatre state projection/override path as needed; retain the existing automatic completion threshold and explicit manual action semantics. Add a Theatre-record activity-time comparator in native and Go merge paths wherever ordinary HLC would otherwise let an older, freshly relayed import overwrite newer viewing. Reuse the server's existing semantic-merge pattern and native `serverSeq` support for canonical payload updates at the same winning HLC: the envelope remains monotonic while the media payload reflects real activity. Test pending local work against this result on both devices. Preserve existing reset/delete barriers and every unrelated domain's HLC behavior.
6. First merge unions library membership; subsequent merges compare against durable baselines. `Remove from Colosseum` persists suppression of unchanged remote membership; remote removal persists the inverse intentional difference and cannot cause automatic re-add to Stremio. Do not export a Neon Collection tombstone as a provider delete. The explicit dual-removal method journals the remote removal before changing the local store; retry it after restart. A subsequent explicit add is a new membership action, not a stale snapshot. Keep unrelated Stremio progress when removing library membership.
7. Extend `HistoryStore` normalization/merge, projection, `HistorySyncAdapter`, adoption verification, native validation and Go History policy/merge with only fields needed for Stremio source, stable display identity/title and latest known date. Accept legacy records lacking optional fields and preserve provenance when an older client supplies a partial record. Respect existing retention/reset barriers and existing profile-wide privacy controls; add no Stremio category switches. Import directly into History; no `ActivityStore` deltas, synthetic sessions, hours, completed-count or active-day contributions.
8. Add one narrow provider-import receipt/reconcile entry point to `AccountRuntime`/`SyncEngine`, not public access to engine internals. Persist the Stremio redo first, apply canonical owners through durable callbacks, then reconcile their portable records into the existing Neon outbox. Mark the relay satisfied only after the Neon checkpoint is durable; replay this sequence idempotently after a crash. Local-only profiles retain canonical data for normal later adoption.
9. Observe local mutations and `SyncAdapterRegistry::remoteApplied` with captured profile identity. Compare semantic payloads/activity time and acknowledged provider state before enqueueing Stremio work. A notification alone is not a new local action. Fetch/reconcile current provider state before retried writes; when acknowledging an older in-flight operation, never erase a newer queued generation. Test Stremio → device A → Neon → device B → Stremio settles without echo. Store origin/op/account binding in private receipts; portable media fields contain no provider credential, configured addon URL or private request envelope.

Behavior to preserve: local resume paths, silent playback saves, series grouping, native completion threshold, ordinary Neon deletion, History privacy/reset semantics and existing Stats.

Baseline: prove remote imports emit no `syncDirty`; capture timestamp-free watch exports, strict History normalization and existing `LibraryApi.watchState` precedence.

Focused tests:
- Qt Test: extend core adapters, sync engine, History and consumption tests; new codec/journal cases cover movies, episode prefixes/specials, millisecond units, compressed watched bits, membership flags, malformed records, stable ties, manual unwatched, newer partial versus older completed, two devices, missing dates and crash between each receipt.
- Qt Quick Test: test the production Theatre state derivation with the conflicting progress/History combinations; reuse existing LibraryApi harness as a regression.
- Existing harnesses: `core_sync_adapters`, `history_sync`, `sync_engine`, `consumption_history`, `privacy_policy`; Go policy/merge tests with a disposable account database for integration cases.
- Negative control: relay before owner durability, compare arrival time instead of activity time, forward an inferred delete, or insert an Activity fact; each corresponding assertion must turn red.

Test seam status: available owner suites plus Task 1's new Stremio seam; update the ledger with added cases.

Lanista actions: not applicable until production connection is exposed in Task 4; test two native runtime/store instances against fixtures now.

Completion signal: durable owner and Neon outbox receipts, provider readback, quiescent request count after redundant replay.

State / events / probes: exact canonical records and baseline/redo state; unchanged Stats projection; zero automatic remote removals.

Visual evidence: not applicable to this internal integration; visible library/history proof is Task 4.

Regression paths: offline/restart, newer local action during remote apply, reconnect after partial first merge, malformed item beside a valid one, profile replacement mid-response.

Evidence artifacts: task-2 native/Go results, redacted two-device fixture trace and crash-recovery assertions.

Bridge status: not applicable to the internal delivery.

Completion criterion: media fixtures converge through real canonical stores and the existing Neon engine with the required deletion, time and Stats invariants.

### Task 3: Profile-isolated addon collection and account continuity

Purpose: Synchronize real configured addons and carry the completed local profile into a new Neon account safely.

Dependencies: Tasks 1–2.

Implementation guidance:

1. Extend `ExtensionsStore` with an explicit active-profile path for Theatre-compatible rows and a durable bulk apply callback; retain its installed-list/fetch/manifest behavior. Keep native and non-Theatre rows on their current local ownership path. Migrate the legacy Theatre portion once to the current owning profile using a recoverable migration receipt; never seed its private URLs into each future profile. Profile change clears preview/request caches and fences old manifest callbacks before publishing the new view. Brand-new profiles retain normal house defaults.
2. Use normalized transport URL as the identity of configured Stremio instances, retaining manifest ID as metadata. Update only instance-targeting callers (`ExtensionsPage`, matching/reorder helpers and addon consumers) so two differently configured URLs sharing a manifest ID survive and the intended instance is removed/reordered. Normalization must preserve case-sensitive path/query configuration. Do not expose raw configured URLs through diagnostic IDs or log messages.
3. Use `addonCollectionGet` with `{authKey,type:"user",update:false}` and `addonCollectionSet` with `{authKey,type:"user",addons:[...]}`. Preserve each entry's `transportUrl`, `transportName`, manifest and flags; validate service success and readback, not merely a non-null response. First merge retains remote order and appends missing compatible Colosseum rows in local order, deleting nothing. Later reconcile install/removal/order against the acknowledged baseline, preserving native rows. Existing `core` protection for Stremio Cinemeta must not make remote account membership/order unrepresentable: separate its required local catalogue availability from synchronized membership, in the same store, and test the distinction explicitly. Do not remove native catalogues or re-seed a removed account addon on every start. Existing content visibility preferences continue governing use/display; sync must not drop configured records silently.
4. Whole-collection writes must be serialized and based on a fresh remote read with pending explicit operations rebased onto it; re-read after writes and retain conflicts/retries until confirmed. Preserve unknown remote configuration fields. Local serialization does not provide a server compare-and-swap guarantee: test concurrent remote additions/removals/reorders, bound retries and never report success on mismatched readback.
5. Extend the **existing** adoption staging/receipt/rollback code to carry the Stremio marker, watched timestamps, private journal and local addon portion on this device. Transfer the vault entry to the destination profile only after source/target identity checks and verification; do not clear the source credential until the destination is durably usable. Resume interrupted migration idempotently. Do not upload the private files or vault entry as part of Neon attachment manifests.
6. Implement disconnect and account-switch operations against the journal generation: stop sends/callbacks, clear that device's credential and old provider work, preserve all canonical merged data/addons, update linked state, and safe-merge a newly validated replacement account. Reconnection without an existing credential re-fetches the true addon collection from Stremio; absence of local addon data on a new device must never be interpreted as remote deletion.

Behavior to preserve: native/non-Theatre extensions, source consent and visibility rules, existing account-adoption rollback, playback's current addon readers.

Baseline: global `indexPath`, ID-based `finishInstall`, protected core mutations and silent persistence failures; demonstrate two configured URLs currently collide.

Focused tests:
- Qt Test: extend extension, adoption, shared-PC and attachment suites; duplicate manifest IDs, first-merge order, post-baseline removal/reorder, core availability, failed persistence, concurrent remote mutation, profile-switch manifest reply, migration crash/rollback and vault transfer failure.
- Qt Quick Test: instance-targeted addon actions on production rows; existing extension configuration/order contracts remain green.
- Existing harnesses: `tests/auto/extensions/tst_extensions_first_run.cpp`, extension configuration contract, adoption/shared-PC/attachment suites; confirm their current registrations before running.
- Negative control: revert URL identity to manifest ID, reuse the global private addon path, or acknowledge a failed save; exact-instance/isolation/retry tests must fail.

Test seam status: existing owner tests plus Task 1 fixtures; new cases must be registered/documented.

Lanista actions: using Task 1's tagged synthetic profile/connection setup, `ui-click` actual Extensions controls; `qml-get` existing installed-view data and sanitized sync status. Drive profile transitions through existing Account UI/scenarios with a disposable account-service fixture.

Completion signal: `completedRun` advances and `pendingCount == 0`; fixture readback matches exact expected collection; adopted profile ID and store receipts match.

State / events / probes: profile A and B addon membership stay separate; non-Theatre rows unchanged; Neon captures contain marker/media only; old-binding requests stop.

Visual evidence: configured-instance rows before/after profile switch, with private configuration text masked/omitted; migration/reconnect state.

Regression paths: restart after addon removal, disconnect during collection write, different-account switch, local-to-Neon creation, failed adoption rollback and new-device reconnect.

Evidence artifacts: task-3 fixture/server assertions, migration failure results, session manifest and sanitized grabs.

Bridge status: bridge blocked today for Stremio state; Task 1 supplies the named projection/fixture. Existing click/read/wait primitives suffice thereafter.

Completion criterion: exact addon collection semantics and profile/adoption isolation pass with production store owners, including interrupted migration.

### Task 4: Theatre panel, explicit removal and imported History presentation

Purpose: Expose the complete approved experience through the existing Theatre and account surfaces.

Dependencies: Tasks 1–3.

Implementation guidance:

1. Add an official Stremio asset with its upstream provenance recorded. Insert it between Search and Profile/Device in `TopBar.qml` only for `activeMedium === "Theatre"`; use existing `KeyboardAction`/focus/navigation patterns and an attention badge around the unchanged asset. Ensure hidden retained world bars do not handle the action.
2. Add `qml/StremioSyncPanel.qml`, a same-window overlay hosted through existing `WorldPage`/`Main` signal routing. Wire Connect/Reconnect, validated account display name, last success, Sync now, first-merge result, switch confirmation and Disconnect to `StremioSync`. Expose the six approved states, useful error copy, busy handling and accessible names. Keyboard close restores focus to the icon. No category switches or provider picker.
3. Route Theatre removals from `TheatreWorld`, `LibraryPage` and `LibraryButton` through the two explicit actions. Keep the unconnected/local-only removal functional. The dual action calls the durable removal operation from Task 2; ordinary Collection disappearance never implies permission for it. Check all shared control callers so other worlds retain their current behavior.
4. Join Stremio-provenance History records into the existing recent-activity/History presentation in `AccountCenter` / `AccountYourColosseumPage` / `AccountActivityFormat`, with a Stremio label, title and latest known date. Bind to the active `ProfileHistory`, including local-only users. Deduplicate imported summary rows and preserve local activity; do not feed imported records into `projectMonth` or its metrics. No new History database or Stats screen.
5. Enable production service composition in `main.cpp` / `AccountRuntime` after dependencies are ready. Trigger bounded sync on connection, profile activation, relevant owner mutation, stale app activation and a periodic timer. Use throttling rather than an indefinitely restarted debounce for progress. Navigating away from Theatre does not lose work; no daemon runs after app exit. New-device marker plus absent credential yields Reconnect and cannot push empty defaults before the safe merge.

Behavior to preserve: Search/Profile placement, other world chrome, local library controls, unified Continue Watching, silent playback saves and existing Stats.

Baseline: capture current Theatre/Search/Profile order, removal entry points, activity list and keyboard sequence in a tagged session.

Focused tests:
- Qt Test: service panel-state transitions and first-merge counts backed by committed operations, not attempted requests.
- Qt Quick Test: new panel test covers all states/actions, authentic asset path, Theatre-only visibility, attention badge, focus/escape, two removal choices and History label without metric changes. Extend `tst_topbar_spatial_navigation.qml`, account activity-binding and Your Colosseum tests.
- Existing harnesses: `colosseum.qml` production-component runner; applicable library/extension contracts; lint touched QML.
- Negative control: show the icon in Biblio, wire both removal actions to dual-delete, or join imports into Stats projection; each must fail its test.

Test seam status: available QML runner; new feature cases are added in this task.

Lanista actions: `ui-click` Theatre's visible mode pill, `ui-wait-for` the named icon visibility, open panel, click named Connect/Sync/Disconnect/removal controls against the fixture, `qml-get` state/list counts, combined window grabs. New names are added to the ledger before replay. Resolve visible retained-page handles rather than ambiguous repeated names.

Completion signal: exact panel `status`, visibility and `completedRun` values; independent fixture acknowledgements; History row count/source and unchanged metric strings.

State / events / probes: controls match service state; remote deletion occurs only for the explicit choice; duplicate refresh causes no new writes; no key/URL in UI diagnostics.

Visual evidence: Theatre top bar, first merge result, reconnect, actionable failure, labelled History and absence in Tankoban/Biblio. Hemanth judges branding/spacing from these captures.

Regression paths: keyboard-only flow, navigate away/back, close/reopen panel, offline playback while sync fails, profile switch with panel open, reconnect and replacement-account merge.

Evidence artifacts: task-4 QML results, named runtime scenarios, warning verdict and redacted UI gallery.

Bridge status: bridge blocked until Task 1's projection and this task's control names exist; no new bridge protocol capability is required.

Completion criterion: the approved controls operate on the real native service/stores in isolated sessions and visible state agrees with durable state.

### Task 5: End-to-end qualification and handoff

Purpose: Prove the complete Stremio connection survives real authentication, restart, another device and provider failure.

Dependencies: Tasks 1–4; server policy/schema support deployed to the qualification service before testing its client.

Implementation guidance: add compact scenarios/fixtures under `tests/lanista_scenarios/` and the existing fixture conventions, update both capability ledgers and user-facing documentation, and fix only findings within the contract. Reuse the current release/deployment path. No production account, private library or daily app is a disposable fixture.

Behavior to preserve: all specification exclusions and the user's existing data, native sync and playback.

Baseline: retain pre-task owner-suite results and initial tagged-profile/Stats snapshots; compare final behavior with those baselines.

Focused tests:
- Qt Test: full new Stremio target and affected owner/Neon/adoption suites; run `ctest --test-dir native/build-msvc -L unit --output-on-failure` after the supported app build. Run Go policy/merge and disposable-database integration tests for changed schemas/comparators.
- Qt Quick Test: `colosseum.qml`, plus focused panel, account activity and top-bar cases; report existing unrelated failures separately.
- Existing harnesses: existing playback progress and extension contracts and the account create/sign-in scenarios. Do not invent a playback test result from a UI screenshot.
- Negative control: retain evidence that an echo loop, wrong-profile reply, secret payload and synthetic Stats import are each detected; restore controls before final green runs.

Test seam status: available after tasks 1–4; no missing test is silently counted as passed.

Lanista actions: run tagged, fixture-backed journeys for first merge; two profiles; provider offline/recovery; crash/restart with pending work; local account creation; new-device marker/reconnect; both removals; addon edits; disconnect/account switch; and two isolated devices sharing one disposable Neon account. Use existing click/read/equality-wait/grab operations and explicit fixture readback. Separately perform official Stremio browser login on a designated test account, have the user authenticate in the browser, and observe the native validated return and subsequent sync. Lanista cannot operate the external browser or prove official-service compatibility by itself.

Completion signal: completed run and durable drain for each journey, exact expected provider and canonical records after restart, stable idle under repeated pulls, and successful real account validation without exposing credentials.

State / events / probes: compare both devices' media state, provider records, private addon roots and Stats snapshots; scan synthetic sentinel secrets across captured Neon payloads/QML/logs/artifacts. Verify the real callback preserves the nonce path, watched encoding and whole-addon collection behavior against the service, not only the fixture.

Visual evidence: compact final gallery of approved states and History label. During a bounded provider outage, play the existing disposable local video fixture and capture advancing playback with successful local progress persistence; fixture queue drains after recovery.

Regression paths: newer activity during retry, offline restart, profile switch during auth/import, replay after acknowledgement, remote deletion followed by refresh, same-manifest configured addon instances, failed disk/vault writes and mixed old/new media records.

Evidence artifacts: task-5 acceptance matrix linked to test outputs, session manifests, redacted service readbacks and gallery; record any external-service limitation explicitly.

Bridge status: available after prerequisite surfaces land for in-app scenarios. External-browser login is a separate witnessed integration check; lack of a designated test login leaves that check unverified, not the plan incomplete and not permission to use a live personal account.

Completion criterion: all twelve approved acceptance criteria have passing evidence at the appropriate layer, real Stremio compatibility is checked, no unresolved introduced regression remains, and branding has the required visual review. Report authored/compiled/tested/runtime-validated/deployed separately.

## Reference and feasibility notes

- Harbor source pinned to `0117755855d3f43960bad3f9f62b69ef851d5991`: [browser login](https://github.com/harborstremio/harbor/blob/0117755855d3f43960bad3f9f62b69ef851d5991/src/lib/stremio-auth.ts), [loopback callback](https://github.com/harborstremio/harbor/blob/0117755855d3f43960bad3f9f62b69ef851d5991/src-tauri/src/stremio_auth.rs), [library API](https://github.com/harborstremio/harbor/blob/0117755855d3f43960bad3f9f62b69ef851d5991/src/lib/stremio.ts), [watched encoding](https://github.com/harborstremio/harbor/blob/0117755855d3f43960bad3f9f62b69ef851d5991/src/lib/stremio-watched.ts), [addon account API](https://github.com/harborstremio/harbor/blob/0117755855d3f43960bad3f9f62b69ef851d5991/src/lib/addons.ts). These establish interaction/format references only. No architecture or threshold adoption is planned.
- Confirm the official browser accepts the nonce-bearing callback before declaring auth complete. If it does not, report that concrete protocol obstacle; do not weaken callback validation or substitute password entry.
- Stremio whole-document writes do not establish an atomic conditional-update guarantee. Fresh-read/rebase/write/readback and retained pending intent are required; the executor must report any demonstrable service limitation instead of claiming impossible cross-client atomicity.
- Existing History has no visible standalone History page and its records require valid dates. The plan adds the required imported rows to the existing activity list without affecting Stats; it does not introduce a page redesign or manufacture dates for invalid records.
- Windows secure storage is the approved implementation. Preserve non-Windows builds with an unavailable-storage state; adding a new platform credential backend is outside this plan.

## Plan self-review against the approved acceptance criteria

These verdicts describe **plan coverage**, not implemented behavior. The approved specification has not been edited.

1. MET — Tasks 1/4/5 cover browser auth for local and Neon profiles, with real-service qualification.
2. MET — Tasks 1/3 cover per-profile secrets, stores, generation fences and two-profile proof.
3. MET — Tasks 2/3/4 cover deletion-free library/addon union, activity-time resolution and committed result counts.
4. MET — Tasks 2/3 cover both directions, watched encoding, collection ordering, acknowledgements and echo-free two-device replay.
5. MET — Task 2 covers both timestamp orderings and the existing History/manual-override precedence conflict.
6. MET — Task 2 preserves intentional membership differences; Task 4 wires every Theatre removal path to the explicit choice.
7. MET — Tasks 2/4 retain History provenance across normalization/Neon and join presentation without Activity/Stats writes.
8. MET — Tasks 1/2/5 require bounded asynchronous work, isolated record failures, restart recovery and playback evidence.
9. MET — Tasks 1/3/4 synchronize only the linked marker and retrieve configured addons after reconnection.
10. MET — Tasks 1/3 cover binding invalidation, vault removal and preserved merged data on disconnect/switch.
11. MET — Task 4 covers the official asset, exact Theatre placement, other-world absence and keyboard/accessibility proof.
12. MET — Delivery boundary and Task 4 exclude chooser/category/multiple-provider UI and deferred work.

Review boundary: Astra performed this source-grounded self-review. No independent reviewer was used in this Astra-only planning session; independent review is not claimed.

APPROVE — The plan covers the approved contract through five bounded tasks, reuses the existing owners and names the necessary new seams and verification prerequisites.
