# Collections Sharing 1.3.0 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. Follow current model/delegation authority when execution is authorized; this planning document dispatches no workers.

**Goal:** Complete public collection links, a focused browser preview, platform-aware app opening and explicit recipient saving for Colosseum 1.3.0.

**Architecture:** Recover the existing sharing candidate onto current master and extend its native collection store, account publication controller and Go service. Keep getcolosseum.world static: stage the existing browser viewer into Cloudflare Pages, route `/c/*` to it, and allow only that site's exact origins to call anonymous collection endpoints. Use a narrow native activation owner and existing platform packaging to open links in the correct running application.

**Tech Stack:** Existing C++17/Qt 6.11/QML, Go/PostgreSQL account service, plain HTML/CSS/JavaScript, static Cloudflare Pages, NSIS and existing Linux/macOS/Android packaging. No new frontend framework or service provider.

**Spec:** [Agreed flow and completion design](2026-11-04-collections-sharing-1.3-design.md). Read both documents before implementation. This repository reserves `docs/superpowers/` for ignored local scratch, so these durable documents use `docs/plans/`.

**Authorization:** Plan first. No product source, installer, production setting, database or release is changed by producing this document. Implementation and later production rollout require the corresponding user instruction. Dates/pins below describe the inspection on 2026-11-04.

## Global constraints

- Canonical link: `https://getcolosseum.world/c/<43-character-share-id>`; registered app URI: `colosseum://collection/<share-id>`.
- Browser title actions lead to `/downloads?collection=<share-id>` with visible installation wording. No web media-detail pages, readers, players or provider/media proxy.
- Publishing requires existing account sign-in. Receiving/saving into an unlocked local profile does not. Never auto-save, play, download, add Library membership or modify progress on link opening.
- Ordinary Save creates an independent editable collection. Kept gifts remain immutable; their independent copies retain existing reset semantics. Private edits never automatically update a publication.
- Preserve exact identities/editions, missing references, atomic persistence, profile isolation, passphrase privacy, quotas and owner-only mutation/revocation.
- Preserve unrelated dirty work, test profiles, existing website/demo/download/feed routes and active Android ownership. Never force-push, expose Lanista in shipping builds, or change provider plans automatically.
- Use the existing portable share schema version 1 and backend migration 0011. Local collection schema becomes 4, reading 2/3/4. No new server migration is needed merely to save an ordinary share locally.
- Automatic opening is platform/browser-dependent. Verified Android HTTPS association and desktop protocol handoff have different capabilities; a launch prompt or explicit Open fallback must be reported honestly.
- Keep task commits on the scoped branch and push them to origin under repository policy. Use `[skip render]` on commits that could reach service auto-deployment before the approved migration/rollout. Never weaken schema gates.

## Review focus

1. A second launch while Colosseum is starting or already running must deliver the exact collection to the primary instance; startup must never interpret its URL as a QML filename. Covered in Tasks 3/5/6.
2. Profile lock/switch or a stale asynchronous response must never save into another profile or leak received content. Covered in Tasks 4/5/7/10.
3. Repeated Save, a new published revision, name collisions or profile adoption must never overwrite recipient edits. Covered in Tasks 4/10.
4. A protected update, cross-origin request or failed app handoff must not remove protection, leak a passphrase or send account credentials to the website. Covered in Tasks 2/7/8/9/10.
5. Origin cold starts, revocation, unavailable exact references, uninstall/upgrade and browser launch restrictions need real recovery behavior rather than false success. Covered in Tasks 2/6/8/10/11.

## Work order and boundaries

| Task | Deliverable | Depends on |
| --- | --- | --- |
| 1 | Reconciled, reproducible feature baseline | — |
| 2 | Qualified backend/database and anonymous browser API | 1 |
| 3 | One public/app/legacy link contract | 1 |
| 4 | Ordinary recipient saving and schema compatibility | 1, 3 |
| 5 | Cold/warm native activation and single-instance delivery | 3 |
| 6 | Installed platform protocol/App Link integration | 5 |
| 7 | Compact sender and native receiver UI | 2, 3, 4, 5 |
| 8 | Browser preview, app handoff and download actions | 2, 3 |
| 9 | Website routes, staging and return after installation | 6, 8 |
| 10 | Combined-source and installed-package qualification | 1–9 |
| 11 | Approved migration, deployment and release | 10, separate rollout authority |

After the baseline, URL/persistence, service and browser work can progress independently only with explicit file ownership. The main agent retains state, security, integration and acceptance decisions. No parallel shared-desktop control or heavy build during timing-sensitive capture.

Commands below run from the feature checkout unless a directory is stated. `<native-build>` and `<checks-build>` mean newly identified task-owned build directories, not retained evidence binaries. Configure them using the repository's platform build guide and current qualified kit. A missing runtime/runner is an explicit blocked platform check, not a passing result.

## Task 1: Recover and reconcile the preserved implementation

**Files/source:** candidate branch `codex/collections-sharing-1.3.0-paused-20261030` at `058b2984457f99fa63f6c22f2f6d484878865700`; current master; especially `native/main.cpp`, `native/account/AccountRuntime.{h,cpp}`, `qml/Main.qml`, `qml/collections/CollectionsPanel.qml`, `YourCollectionsPage.qml`, and native/tests CMake wiring.

**Interfaces:** consumes the paused candidate and current committed master; produces one integrated feature branch with its base, merge resolution and initial check record pinned.

- [ ] Inventory current refs, attached worktrees and dirty ownership. Reuse a suitable isolated checkout, or create a scoped worktree through the supported worktree tool; do not switch/clean/stash the dirty primary checkout. Use branch `codex/collections-sharing-1.3-20261104` unless already owned by another chat.
- [ ] Fetch origin and record the actual current master/candidate pins. Merge/replay the candidate into that baseline, preserving later Back/window controls, account/profile durability, release/platform wiring and unrelated app behavior. Keep the archive itself unchanged.
- [ ] Inspect the scoped merge diff, build registration and old/new collection fixtures. Establish which archived evidence still matches; do not relabel old binaries/screenshots as current.
- [ ] Run the existing smallest baseline checks: `node --test tests/collections_routes_test.mjs tests/collections_shared_routes_test.mjs server/account-service/internal/httpserver/collectionviewer/viewer.unit.test.cjs`, existing named-store/identity/controller targets, and sharing QML checks. Record pre-existing failures separately.
- [ ] Commit/push the reconciled feature baseline only. Exit condition: a reproducible source pin, preserved master functionality, an explicit failure list and no changes to retained profiles or production.

## Task 2: Finish backend correctness, browser API access and migration readiness

**Modify:** `server/account-service/internal/account/collection_publications{,_test}.go`, `collection_projection.go`, `internal/httpserver/{server.go,request.go,request_test.go,collection_handlers.go,collection_handlers_test.go,collection_integration_test.go}`, `internal/config/{config.go,config_test.go}`, the service entry's config wiring as necessary, and `internal/database/{migrate_test.go,migrate_operational_test.go,schema_contract.go}` only where diagnosis requires it. Preserve `internal/database/migrations/0011_collection_publications.sql` unless a demonstrated correctness defect requires an explicitly reviewed change.

**Create:** `internal/httpserver/collection_cors.go`, `collection_cors_test.go`. Paths here are under `server/account-service/`.

**Interfaces:** keep existing authenticated publication routes and anonymous `GET /v1/collections/shared/{shareID}` / `POST .../{shareID}/open`. Add `Config.CollectionsAllowedWebOrigins []string`, populated from `COLLECTIONS_ALLOWED_WEB_ORIGINS` and validated into exact scheme/host/port origins, and `func collectionCORS(allowedOrigins []string, next http.Handler) http.Handler`. Production allowlist is apex and `www`; no credentialed CORS and no change to native authentication authority. A non-allowlisted Origin receives no CORS access headers; this must not break the existing service's own same-origin viewer or native requests.

- [ ] Reproduce the recorded **full** database-package failure in a new disposable database. Test serially because fixtures reset schema. Determine timeout/locking/resource cause before changing deadlines; keep exact migration/checksum/physical-schema validation and rollback behavior.
- [ ] Add CORS tests: both allowed HTTPS origins work, preflight allows only the needed methods/Content-Type, disallowed/null/lookalike origins receive no access, no `Access-Control-Allow-Credentials` or wildcard is emitted, `Vary: Origin` and `Cache-Control: no-store` survive success/errors. Native requests without Origin continue working. CORS applies only to anonymous collection routes.
- [ ] Inspect/reconcile the separate `codex/account-429-20261102` trusted-proxy repair against current server/config. Reuse its bounded client-address logic where appropriate rather than blindly merging unrelated native changes. Add collection-open tests showing independent verified client networks get independent buckets and spoofed/malformed forwarded chains cannot bypass limits.
- [ ] Establish the actual Render ingress chain and exact trusted CIDRs before production enablement. Missing reliable ingress information is a rollout blocker; do not fall back to arbitrary header trust or remove abuse limits. The static website calls the origin directly, so it introduces no additional API proxy hop.
- [ ] Verify owner isolation, immutable gifts, optimistic update conflicts, replay/rollback, passphrase attempts, quota behavior and terminal revocation. Rehearse migration 0011 from empty and from an isolated clone of the verified current production baseline; compare previous checksums/schema metadata. No production writes.
- [ ] Run in `server/account-service`: `go test -p 1 ./internal/config ./internal/account ./internal/httpserver ./internal/database ./internal/httpserver/collectionviewer -count=1` with the explicitly disposable `TEST_DATABASE_URL`. Expected: all affected packages pass with **no database-test skips**. Run the supported Linux race variant for affected packages after the ordinary run passes.
- [ ] Commit/push the qualified backend slice. Exit condition: full database failure resolved, safe public CORS, verified address-handling design and a rehearsable exact migration/service combination.

## Task 3: Separate public links from authenticated API transport

**Create:** `native/account/CollectionShareLink.{h,cpp}`, `tests/collections_links/{CMakeLists.txt,collection_share_link_test.cpp}`.

**Modify:** `native/account/CollectionPublicationController.{h,cpp}`, `AccountRuntime.cpp`, `native/CMakeLists.txt`, `tests/CMakeLists.txt`, `tests/collections_publication_controller/controller_test.cpp`.

**Interfaces:** namespace `Colosseum::Collections`: `std::optional<QString> shareIdFromUrl(const QUrl &url, const QUrl &webOrigin, const QUrl &legacyServiceOrigin)`; `QUrl publicShareLink(const QString &shareId, const QUrl &webOrigin)`; `QUrl applicationShareLink(const QString &shareId)`. Controller construction receives explicit service and web origins; production supplies the canonical website origin, tests supply controlled fixtures. `linkFor()` returns the public website URL; transport still uses AccountServiceEndpoint.

- [ ] Add table-driven `parsesCanonicalWwwLegacyAndAppLinks` / `rejectsUnsafeCollectionLinks`: exact 43-character IDs, public aliases and one optional trailing slash succeed; credentials, unexpected ports, suffix/lookalike hosts, encoded IDs/separators, additional segments, query/fragment, oversized input and unsupported schemes fail before any network request.
- [ ] Implement the shared parser/builders and update every controller constructor/caller/test. Do not derive API/web origin from incoming user text. Keep explicit localhost fixture injection out of production association metadata.
- [ ] Update controller tests so `linkFor(id)` uses the public domain while `openLink()` calls the existing account-service request for that exact ID without changing origin or forwarding website credentials. Preserve legacy pasted links.
- [ ] Configure/build the standalone `tests/collections_links` target and run `ctest --test-dir <checks-build>/collections-links --output-on-failure`; expected target `collections.links` passes. Run publication-controller tests.
- [ ] Commit/push. Exit condition: one tested identity/link contract used by native copy, receiving, activation and browser fixtures.

## Task 4: Add ordinary Save and compatible local persistence

**Modify:** `native/NamedCollectionsStore.{h,cpp}`, `native/account/CollectionPublicationController.{h,cpp}`, `tests/collections_sharing_store/collections_sharing_store_test.cpp`, `tests/named_collections/{named_collections_store_test.cpp,profile_collections_preservation_test.cpp}`, `tests/collections_publication_controller/controller_test.cpp`, `tests/collections_identity/collections_identity_test.cpp`, cross-language gift fixtures only if the contract truly changes.

**Interfaces:** `QVariantMap NamedCollectionsStore::saveSharedCollection(const QString &shareId, int revision, const QVariantMap &snapshot)` returns `{ok, collectionId, alreadySaved, error}`. Add `static bool sharedMemberReference(const QVariantMap &member, QVariantMap *reference)` for the existing safe member conversion used by keep/save/preview. Add controller `Q_INVOKABLE bool saveReceivedCollection()`, `Q_INVOKABLE QVariantMap receivedReference(int index) const`, `Q_PROPERTY(bool receivingReady READ receivingReady NOTIFY changed)`, and signal `saved(const QString &collectionId, bool alreadySaved)`.

- [ ] Add `ordinarySaveIsEditableAndIndependent`, `repeatedSavePreservesRecipientEdits`, `newRevisionMakesSeparateCopy`, `conflictingSameRevisionIsRejected`, `schema2And3UpgradeLosslessly`, `schema4RoundTrips`, `adoptionPreservesDivergentSavedCopies`, `invalidOrFailedWritePreservesOriginal`, `profileChangeRejectsStaleSave` and native-preview no-mutation checks. Assert no Library/download/progress owner calls.
- [ ] Write schema 4 and read schemas 2/3/4. Keep schema-3 gift fields and semantics; persist ordinary `savedShareId`/`savedRevision`/`savedSnapshotHash` atomically with members. Hash the validated canonical JSON snapshot with SHA-256. Validate the provenance triple together; unknown/malformed/future files fail without rewrite.
- [ ] Reuse the member conversion from Keep Gift; do not maintain a second identity resolver. Ordinary imported lists have no gift immutability flag. Resolve name collisions with `uniqueName()`. Repeated Save finds a same-provenance copy and opens it without rewriting its edits; same share/revision with a different hash errors. A new revision creates another local copy.
- [ ] Preserve adoption's conflict retention and gift/copy behavior. Saving while locked/changing/unhealthy must fail against the correct owner. Older files are upgraded only during a successful atomic mutation; record the safe downgrade limitation.
- [ ] Run focused native targets: `ctest --test-dir <checks-build> -R '(collections_sharing_store|publication_controller|named_collections_store_test|profile_collections_preservation_test|collections_identity_test)' --output-on-failure`. Expected: all targets pass, including actual reload/offline checks.
- [ ] Commit/push. Exit condition: ordinary saved collections survive editing, retries, restart, offline use and profile adoption without altering the sender or recipient's other state.

## Task 5: Deliver links to the correct native instance and profile

**Create:** `native/bootstrap/CollectionLinkActivation.{h,cpp}`, `tests/collections_links/collection_link_activation_test.cpp`.

**Modify:** `native/main.cpp`, `native/bootstrap/StartupLayout.{h,cpp}`, `native/account/AccountRuntime.cpp`, native/tests CMake files, `tests/{startup_layout_harness.cpp,instance_lock_harness.cpp}` and `qml/Main.qml` wiring only.

**Interfaces:** bootstrap helper `std::optional<QUrl> takeCollectionLaunchUrl(QStringList *arguments, QString *error)` consumes `--collection-url <url>`. `CollectionLinkActivation` exposes `bool startPrimary(const QString &appDataRoot)`, `bool forwardToPrimary(const QUrl &url, int timeoutMs = 1500)`, `bool submit(const QUrl &url)`, `void setDestinationReady(bool)`, `void clearPending()` and signal `void openRequested(const QUrl &canonicalUrl)`. It uses Task 3's parser and emits at most the latest pending link after readiness.

- [ ] Test cold start, primary already running, primary still booting, malformed CLI input, protocol argument injection, multiple rapid links, isolated tags, stale socket/lock recovery, sender exit after acknowledgment, profile switch during fetch, and shutdown with queued work. `rejectsProtocolArgumentInjection` must reject an otherwise valid link followed by a QML filename, updater flag or another activation argument. Assert one state-owning process and no arbitrary IPC command path.
- [ ] Consume the collection flag before StartupLayout's positional-QML detection. When present, require exactly one URL and reject additional positional/override/update arguments before normal startup; a protocol argument must never select a QML file. Ordinary invocations without the activation flag preserve their development/updater behavior. Keep InstanceLock authoritative. Start a per-user local channel derived from the app-data identity; a secondary launch forwards the validated ID with bounded wait/retry and exits without opening stores. Remove stale IPC state only after obtaining the authoritative lock.
- [ ] Register platform incoming URL handling early. On Android use the verified Qt 6.11 handler path; on Apple route URL-open events/handlers; desktop protocol launches use CLI. Marshal requests onto the GUI thread and unregister URL handlers before owner destruction. Unrelated outgoing HTTPS sign-in/help links must still open normally, without recursion or interception by the collection route.
- [ ] Wait until the normal QML destination is loaded, boot splash is finished and the actual active profile is unlocked/healthy. Wire `receivingReady` to authoritative runtime activation/deactivation; do not infer readiness from elapsed time. Hold only the latest URL, not a passphrase or received snapshot, through unlock. Invalidate stale network generations on profile change.
- [ ] Run the new activation test process pair and existing instance/startup harnesses. Expected: the exact collection reaches the visible primary once, including a warm process, and refused/malformed inputs cause no network/state writes.
- [ ] Commit/push. Exit condition: working cold/warm application-level delivery independent of OS installer registration.

## Task 6: Register installed application link handlers

**Modify:** `scripts/installer/colosseum.nsi`, `tools/linux-release/{package_deb.py,test_packaging.py}`, `tools/macos-build/{package-preview.py,test_package_preview.py}`, `native/platform/android/AndroidManifest.xml`, `tests/installer/{release_installer_smoke.ps1,release_upgrade_smoke.ps1}`, relevant Android packaging contract tests.

**Interfaces:** Windows and Linux launch `colosseum --collection-url <colosseum://collection/id>` through correctly quoted argument vectors. Android verified HTTPS `/c/` and the explicit custom-scheme filter feed Task 5. Export the actual public Android application ID/certificate metadata for Task 9; no signing key material enters source or site staging.

- [ ] Windows: register HKCU `Software\Classes\colosseum` with `URL Protocol` and a quoted executable/`%1` command. Refresh it for manual installation and update promotion; restore the right path on rollback. Uninstall removes the handler only if owned by that installation. Test paths containing spaces and hostile URL arguments without shell evaluation.
- [ ] Linux: generate `Exec=colosseum --collection-url %u` and `MimeType=x-scheme-handler/colosseum;` in the existing desktop entry, retaining launcher argument forwarding. Refresh desktop metadata through the packaging's supported mechanism without changing another user's default browser/handler. Test actual `.deb` install/upgrade/removal on the supported desktop.
- [ ] Android: add separate path-scoped HTTPS filters with DEFAULT/BROWSABLE and `autoVerify`, plus the custom protocol fallback. Determine the real built package ID and release certificate from the approved Android lane; the manifest template's `org.qtproject.example` is not release evidence. Do not claim the whole website or `/downloads` as app links. Verify the same signed identity on device, with app absent/present, cold/warm and user-disabled association.
- [ ] macOS: add `CFBundleURLTypes` to the existing packaging output and test incoming URLs on an actual available Mac before claiming runtime support. Associated-domain HTTPS support requires verified signing/entitlements/AASA work; custom-scheme packaging tests alone do not qualify it. No iOS target is created.
- [ ] Run packaging unit checks (`python tools/linux-release/test_packaging.py`, `python tools/macos-build/test_package_preview.py`) and relevant installer/Android checks, then perform installed activation smoke tests. Expected: native delivery matches Task 5; update/rollback/uninstall leave correct registrations and profiles.
- [ ] Commit/push. Exit condition: each release platform has a verified handler or a specifically recorded runtime/signing gap that blocks that platform's completion claim.

## Task 7: Finish the compact QML sender and receiver

**Modify:** `qml/collections/{CollectionsSharingPopup.qml,CollectionsPanel.qml,CollectionPage.qml,YourCollectionsPage.qml,CollectionsAdapter.qml,CollectionGiftPresentation.qml}`, `qml/Main.qml` scoped wiring, `tests/collections_qml/tst_CollectionsSharing.qml`, `tests/collections_shared_routes_test.mjs`. Reuse existing Theme, Glass, CollectionActionButton and focus/navigation owners.

**Interfaces:** `CollectionsPanel.openIncoming(url)` opens the receive view and delegates to controller `openLink()`. Popup has an explicit initial view, rather than an always-visible three-tab console. Its `titleRequested(var reference)` feeds `panel.openReferenceRequested(reference)` and the existing Main `openCollectionEntry()` route. Controller `saved(id, alreadySaved)` opens that collection in Your Collections.

- [ ] Add visible-navigation tests for compact default Share, Create→Copy, exact-source existing-link reuse, signed-out account routing, gift disclosure/draft/publish, links management, explicit protected-update choices, revoke/retry errors and receiver Save/Already saved/Keep Gift. Never preserve tests by clicking hidden obsolete controls.
- [ ] Implement the compact default popup using wallpaper-backed existing Glass, readable scrim and established typography. Target roughly 460–520 logical width and content-driven height that fits 1280×720; narrow layouts stay inside viewport margins. Gift/receive/manage can scroll. Preserve keyboard Tab/Escape, focus restoration, busy state and entered values.
- [ ] Make the default ordinary flow Create link→Copy link. Filter link reuse by current account/profile, exact local source ID, ordinary kind and active status. Do not substitute links by name. Keep Publish changes explicit; passphrase removal is a named choice and confirmations use actual action verbs.
- [ ] Receiving must not trigger the old unconditional publication refresh and block its own Open request. Present the exact collection, ordinary Save, gift opening/Keep and stable inline errors. Native preview title actions use `receivedReference(index)` and existing route validation without persisting a temporary list. Unavailable items remain visible and non-opening.
- [ ] Run `named_collections_qml_test -input tests/collections_qml/tst_CollectionsSharing.qml`, then the complete Collections QML target with a measured justified timeout; the archived full fixture previously exceeded 180 seconds. Run route Node tests and scoped QML lint/qmllint. Do not count an exceeded CTest timeout as a pass because a direct executable later finished.
- [ ] Inspect the actual desktop and narrow UI, including protected updates and receive/save. Commit/push. Exit condition: accepted flow is reachable through visible controls, retains Colosseum styling and preserves existing shell/Back/window behavior.

## Task 8: Complete the focused browser preview and app handoff

**Modify:** `server/account-service/internal/httpserver/collectionviewer/{index.html,viewer.css,viewer.js,embed.go,embed_test.go,viewer.unit.test.cjs,viewer.test.cjs,viewer.service.test.cjs}`.

**Interfaces:** viewer HTML has an explicitly staged `data-api-origin` attribute; empty means the serving origin for existing service/local fixtures. Public staging injects only the approved HTTPS service origin. JS builds only the existing anonymous collection API paths. Location supplies the ID, never the API origin. Title hrefs use `/downloads?collection=<validated-id>`; Open uses Task 3's custom URI/verified package intent.

- [ ] Add tests for canonical `/c/` and legacy `/collections/s/` paths, explicit API origin, CORS fetches with `credentials:'omit'`, title/download hrefs, Copy link, existing gifts and passphrase/error states. Unsupported local/foreign origins must not be selected by URL content.
- [ ] Match the collection's visual character using established first-party site assets/colors/layout. Retain ordered text/notes and bounded gift covers; no new provider-art fetches, catalogue requests, title detail routes or media code. Use native anchors for title actions and correct keyboard/screen-reader labels.
- [ ] Implement one bounded launch attempt only where platform/browser behavior supports it, plus a permanently visible **Open in Colosseum** action. Maintain an in-navigation attempt guard so app return/Back does not relaunch repeatedly. Never infer installation from timeouts or redirect to downloads automatically after a blocked attempt. Explicit Get/title clicks alone go to Downloads.
- [ ] Test real browsers with and without a registered handler. If an unattended attempt creates an unknown-handler error for an absent app, use the explicit Open fallback on that browser and record the limitation. Do not bypass browser permission prompts or claim a browser page can reliably detect installation. Android intent fallback returns safely to the preview.
- [ ] Retain gift edition-file export/manual import as a fallback, with accurate copy and no passphrase/media export. Do not add ordinary file export unless needed for the agreed app-link path; native ordinary Save is Task 4.
- [ ] Run `node --test server/account-service/internal/httpserver/collectionviewer/viewer.unit.test.cjs tests/collections_shared_routes_test.mjs`; run real-service/browser tests with a disposable local service/database, including desktop/narrow viewports, malicious text/image payloads, keyboard focus, cold-origin retry and no-handler recovery.
- [ ] Commit/push. Exit condition: usable read-only browser preview, intentional install navigation and honest app-handoff behavior without extra web media surfaces.

## Task 9: Add getcolosseum.world routes and reproducible website staging

**Create in product repository:** `web/collections-sharing/{README.md,stage_site.py,downloads-return.js}`, `web/collections-sharing/tests/test_stage_site.py`. The actual current website is maintained independently; identify its current authoritative source and production deployment through the website skill/context before editing it. Keep machine-specific staging paths in private records, not these repository files.

**Modify in the verified website source/stage:** `_redirects`, `_headers`, `index.html` Downloads initialization; create `collections/viewer.html`, `collections/assets/{viewer.css,viewer.js,downloads-return.js}` and `.well-known/assetlinks.json` from the qualified public release metadata.

**Interfaces:** `stage_site.py --site-base <verified-current-site> --output <new-task-stage> --api-origin <approved-service-origin> --assetlinks <verified-public-association-file>` copies the canonical viewer assets and merges only the required integration. Emit a JSON manifest with source revision, asset hashes, API/public origins and changed paths. Output must be new; never overwrite the input/public baseline.

- [ ] Write staging tests asserting byte/hash parity of copied viewer JS/CSS, one validated API-origin injection, no secrets/private review hub, preservation of unrelated assets/redirects/headers, idempotent generation into a fresh output, and rejection of unsafe output/origin/association input.
- [ ] Add same-origin 200 routes `/c/* /collections/viewer.html 200` and `/downloads /index.html 200` before broader fallbacks. Keep the shared URL in the address bar. The existing site selects its Downloads panel when pathname is `/downloads`, while retaining its normal hash-tab navigation.
- [ ] `downloads-return.js` reads only a valid 43-character `collection` value, renders a return/copy link to the canonical `/c/` URL inside the Downloads panel, and uses literal text/constructed trusted URLs. Ignore invalid/extra values. No arbitrary redirect parameter, cookie, local storage or automatic app launch from the downloads page.
- [ ] Apply collection-specific CSP with only the exact API origin in connect-src, no-referrer, no-store, nosniff and framing restrictions to public preview/rewritten HTML. Verify header behavior on both source and destination paths; do not assume `_headers` behavior from local file inspection alone. Keep generic HTML free of collection contents and protected metadata.
- [ ] Serve Android asset links as JSON at the exact well-known URL on apex and `www`, without redirect or login. Include only actual released package/certificate identities; never publish test/debug certificate assertions to production. Restrict application manifest matching to `/c/` so downloads/home remain normal website routes.
- [ ] Test with `python -m unittest discover -s web/collections-sharing/tests -p 'test_*.py'`. Preview the staged site with Pages-compatible static routing and a disposable service; verify `/c/id`, `/downloads?collection=id`, direct invalid paths, current downloads/demos/about, legacy versioned installers, stable feed and private-preview 404 guards.
- [ ] Prepare a Cloudflare preview deployment only after that action is authorized; check actual apex/`www` production routing before later promotion. Current production uses existing download/feed/demo/preview-guard Workers: preserve them. No new Worker or Functions service is required by this design.
- [ ] Commit/push repository staging changes and the website's scoped changes under its actual source-control policy. Exit condition: reproducible site artifact with verified routes/security/return behavior and a concrete rollback baseline; no production promotion yet.

## Task 10: Qualify the exact combined source and installed artifacts

**Modify/extend:** `tests/collections_sharing_runtime/{README.md,run_service_browser.sh,run_service_native.sh,file_client.py}` and its existing fixtures/driver where relevant; add `tests/collections_sharing_runtime/acceptance-matrix.md`. Keep raw logs/screenshots/profiles/binaries outside Git. Reuse the existing Harness guide for runtime work.

**Interfaces:** one candidate source pin and working-diff identity; corresponding runtime-ready QML manifest, executable/package hashes, service source/schema revision and website staging manifest. Fixture instrumentation is not shipping/default-off installer evidence.

- [ ] Run Task 2's complete affected Go/database checks and supported Linux race checks; run native store/controller/identity/activation/bootstrap checks, Collections QML tests, Node viewer/routes and packaging checks. Do not rerun already passing unrelated suites without a new dependency/failure reason.
- [ ] Build the real combined runtime-ready candidate and shipping/default-fixture-off packages. Verify the packaged QML manifest matches that source; preserve unrelated test profiles. Record actual build/runtime evidence separately.
- [ ] Execute every mandatory scenario below using two synthetic people/profiles and an actual service/browser/native app. Check native store contents and visible outcomes; screenshots alone do not prove persistence, routing or source identity.

| Scenario | Required result |
| --- | --- |
| Create/Copy/reopen/update ordinary link | Same connected active link reused; private edits absent until explicit publish; old revision conflict is recoverable. |
| Browser without app | Collection remains viewable; title/Get actions open Downloads; no launch loop or automatic download. |
| Browser/app already installed | Exact collection opens cold and warm through supported association/protocol; confirmation/fallback behavior recorded per browser. |
| Save ordinary collection | Correct order/identities/notes; editable independent copy; repeated Save after local edits preserves them. |
| New revision/name collision/adoption | Explicit new copy; unique name; both divergent local versions retained; no silent replacement. |
| Native preview title before Save | Existing native route opens without persisting a collection or affecting Library/download/progress merely from receiving. |
| Gifts | Closed→Open→explore→Keep→restart/offline→Make My Own Version preserves existing edition/copy semantics. |
| Protection/revocation | No protected data before unlock; wrong passphrase/rate errors recover; protected update cannot silently clear; revoked links stop future access while saved copies remain. |
| Profile and application lifecycle | Locked/unlock, switch/sign-out, startup delay, duplicate launch and shutdown cannot cross-save or expose stale data. |
| Invalid/unavailable input | Malicious text/URLs/images, excessive bodies, unknown fields, missing provider or conflicting canonical assertions stay safe and never pivot identity. |
| Install/update/rollback/remove | Registered handler always targets the correct installed path; uninstall preserves profiles and removes only owned registration. |
| Website regression | Apex/`www`, Downloads return link, existing download/feed/demo routes and preview guards keep their intended behavior. |

- [ ] Cover Windows current supported Edge/Chrome/Firefox and the supported Linux desktop/browser. Cover the signed Android lane on phone/tablet for verified association and cold/warm intents; macOS native results require an actual available Mac. Record each as pass/fail/unverified with version and cause. Unsupported/missing platform evidence blocks a claim for that platform.
- [ ] Inspect desktop and narrow captures for the native Share/Receive and browser page. Offer only a short human appearance check if it adds judgment beyond automated/agent checks; do not substitute unnecessary manual testing for adequate runtime proof.
- [ ] Write a concise acceptance receipt with exact source/artifact pins, checks, remaining gaps and rollout recommendation. Commit/push only portable tests/docs. Exit condition: the full agreed journey is evidenced on every release platform being claimed, with no unresolved database failure or concealed launch limitation.

## Task 11: Prepare and execute a separately approved rollout

**Files/artifacts:** update `server/account-service/{COLLECTIONS_PUBLICATIONS.md,README.md}`, create the sharing section of `docs/release-notes/v1.3.0.md` when verified; production migration/service/site/package/feed receipts and rollback identifiers stay in their proper release artifacts. Reconfirm exact live targets; do not copy old production IDs or machine paths into source.

**Interfaces:** accepted Task 10 candidate, unchanged/reviewed migration checksum, a rehearsal on the actual current schema baseline, existing Render/Pages service identities and approved 1.3.0 installers. Standing scoped Git push authorization does not authorize database writes or a release.

- [ ] Prepare the concrete review package: combined diff, candidate packages, native/browser captures, acceptance matrix, exact migration statements/checksum, schema/ingress preflight, site manifest, live target readback and recovery procedure. Recheck whether concurrent master or production changes require renewed qualification.
- [ ] Rehearse coordinated rollout without weakening exact schema gates. Current old schema-10 code rejects schema 11 and new code requires it; a brief readiness interruption may be necessary on the existing Free service. Recovery after migration uses schema-11-compatible code or a forward fix, never a schema-10 binary that rejects the database. Preserve a backup/recovery point and test the migration transaction's rollback behavior before production.
- [ ] Obtain the actual rollout/release instruction covering production migration, existing service/site promotion and publication of the qualified installers/feed. No plan upgrade, new paid service, credential change or rehearsal-branch deletion is implied.
- [ ] Reconfirm production migration history, schema contract, live source and trusted ingress immediately before the authorized operation. Apply the rehearsed guarded migration, deploy the pinned compatible service, and verify ready/health and anonymous/protected APIs before exposing the new flow.
- [ ] Coordinate public availability: prepare working `/c/` routes, publish the approved 1.3.0 installer artifacts, promote website Downloads to those capable installers, then promote the stable update feed/announcement under release authority. Do not leave the final collection page recommending a 1.2.0 installer that cannot handle/save these links. Preserve older versioned download URLs.
- [ ] Verify on both public domains: a tagged synthetic ordinary/gift publication, browser preview, passphrase, app activation, ordinary Save, Downloads return, owner rejection and revoke. Remove only task-owned synthetic data through supported endpoints. Do not alter real users' collections or existing profiles.
- [ ] Record actual deployed revisions, migration checksum, installed package identities, final redirects, health, acceptance and recovery identifiers. Mark authored, built, tested, integrated, deployed and released states independently.

## Plan self-review and handoff

- Accepted flow is covered by Tasks 3–9; prior implementation debt by Tasks 1/2/7; complete verification and production boundaries by Tasks 10/11.
- Shared identity, persistence, activation and UI interfaces are defined above; no executor needs to infer whether an ordinary save is live-followed, editable, account-required or destructive.
- The browser remains a collection preview. Static Pages staging plus narrow anonymous CORS is deliberately chosen over an API proxy/new backend. Native source/media authority is preserved.
- The unavoidable limitation is explicit: an installed application does not guarantee every browser launches it without a prompt. Platform association and explicit fallback are release-tested behavior, not assumed detection.
- This document is ready for Hemanth's plan review. Implementation has not started. At execution, retain primary ownership of coupled state/security/integration and use bounded workers only within current routing/permission instructions; no execution method is selected or dispatched by saving the plan.
