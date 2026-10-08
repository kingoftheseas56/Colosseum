# Feria browser engine contract v1

Status: proposed design for review, 2026-10-08. This document defines required behavior and delivery boundaries; it does not claim the interface exists or that either engine has passed qualification.

## Goal and scope

One Feria browser product, one set of Colosseum owners, two engine adapters: QtWebEngine and Windows WebView2. Give WebView2 the same app-space ownership and acceptance criteria as Qt without duplicating stores, building another browser frontend, or treating codec compilation as DRM qualification.

V1 includes retained spaces, ordinary/private profile isolation, native navigation, site-created browsing contexts, safe close, manual restore, presentation changes, playback ownership, downloads/permissions and bounded renderer recovery. A general-purpose browser entry, new extension infrastructure, cached thumbnails, hover previews and visual polish are separate work.

Evidence anchors: native/SessionStore.h; native/feria/FeriaAccountStore.* and FeriaBrowserPolicy.*; qml/feria/FeriaSessionDeck.qml, FeriaCloseController.qml, FeriaQtWebEngine.qml and FeriaWebView2.qml; native/feria/provider-host/FeriaProviderHost.* and FeriaHostItem.*. The current deck/profile/new-window/close paths contain Qt-specific assumptions. WebView2 already owns a native host and popup path. Neither is to be rebuilt blindly.

## Canonical owners

| Concern | Owner |
| --- | --- |
| Open spaces, selected space per app, app-only taskbar projection, browser identity, close/reopen transactions | Existing SessionStore |
| Account scope, preferences, saved descriptors, manual restore offers and admitted download metadata | Existing FeriaAccountStore |
| Provider start destinations and watch/read/listen category | Existing provider definitions |
| Navigation admission, storage scope and supported engine policy | Existing FeriaBrowserPolicy |
| Retained view lifetime and presentation routing | Existing FeriaSessionDeck, extended to select an adapter |
| Browser mechanisms, raw native requests, renderer handles and callbacks | Respective engine adapter |
| Sidebar, app skin, address expansion, request controls and app pages | Existing QML presentation |

Do not introduce parallel session, account, history, restore, download or preference stores. Audit existing ownership before adding a new record. Extend C++ owners for durable rules. Preserve working QML presentation and use a small engine-specific QML shim where Qt requires one; this contract does not require a wholesale QML-to-C++ rewrite.

## Identity and operation fencing

Preserve the existing identity fields: accountEpoch, profileId, spaceId, pageGeneration, documentGeneration, hostId and hostGeneration. Do not silently rename or change their meaning.

The adapter binding additionally identifies engineId and its opaque profile handle. Ordinary/private and engine storage partitions must remain distinguishable even when they share a logical Colosseum account. Native profile handles are not canonical identities.

Commands and callbacks carry captured identity plus a unique operationId. Navigation adds a native navigation identifier; page requests add their source frame/origin. SessionStore coordinates identity changes; adapters cannot invent canonical generations. Distinguish provisional navigation from main-document replacement and settle the correct native navigation attempt.

Reject late callbacks from an old account, profile, replaced page, transferred host or retired operation. A stale callback cannot consume a restore offer, select another space, answer another page's permission request, resume unrelated media or destroy a replacement view.

Native objects remain inside the adapter. Shared code sees a typed request token and safe metadata, never a COM pointer or Qt request object. Engine-specific synchronous lifetimes and supported deferrals remain engine-specific; do not impose Qt's synchronous adoption technique on WebView2.

## Adapter surface

These are semantic operation names, not declarations of existing APIs. GLM must map each to current code and supported native APIs before Dots implements it.

| Operation group | Required behavior |
| --- | --- |
| create / dispose | Bind one canonical space to its explicit profile and presentation host; asynchronous initialization; idempotent retirement |
| navigate / back / forward / stop / reload | Native history and load state; typed-input admission through shared policy; no JavaScript history reconstruction |
| requestClose / respondToRequest | Exact close attempt and request token; native consent/cancellation observation; never equate forced controller destruction with an acknowledged close |
| adoptWindowRequest | Preserve the original native request, source profile, target name, opener and POST behavior; appropriate native lifetime/deferral |
| transferHost / setPresentation | Move the existing live context when supported; acknowledge actual transfer and preserve focus/geometry; otherwise report unsupported or blocked |
| setActivity / inspectMedia | Shared app pause/resume intent and owned-pause tracking; separate engine suspension from media pause |
| execute / invalidateCallbacks | Bounded internal scripts, fenced results and callback retirement; no arbitrary public bridge to account secrets |
| recover | Explicit user recovery for the affected space, with a new page binding; no automatic crash retry loop |
| download / permission / cleanup | Route native mechanisms through existing policy/owners and report actual completion or incomplete scope |

Every asynchronous operation has exactly one observable terminal outcome: succeeded, cancelled, failed, blocked, unsupported or invalidated. Requests can remain pending only while their native lifetime is valid. A timeout reports failure/blocking; it does not manufacture cancellation, successful transfer, restored playback or successful cleanup. Platform callback drain may outlive a UI-facing invalidation; retain safe native ownership until draining is complete.

Expose engine-independent state: initialized, current URL/title/icon, load state, progress (unknown allowed), native canGoBack/canGoForward, renderer incident, presentation state and supported capabilities. Distinguish adapter initialization, DOM readiness, main-document commit and terminal load success. Qt compatibility wrappers may remain while callers migrate.

## Required behavior

### App spaces and profiles

An app can own multiple retained spaces. Public names are watchspaces, readspaces and soundspaces according to its category; internal IDs stay stable. Selection belongs to SessionStore. A background-created space must not steal selection or focus. An explicit new space opens the provider's start destination, not the active media/article URL.

Ordinary profiles remain account-scoped. Private state must not enter ordinary history, restore offers or download history; private spaces in an active private scope may share its private profile. Retire the private scope after its last dependent space/download/native request retires, using the engine's supported storage guarantees. Do not claim physical zero persistence without verifying the actual runtime.

Engine selection is explicit and validated before allocation. Do not silently move cookies, logins, form state or opener relationships between engines. Changing a live engine is a new browsing context, not live-view transfer; prefer an explicit new space and retain the original until its safe close settles. No silent fallback on a DRM failure. Engine-specific limitations must be visible to the shell without exposing implementation jargon to users.

### Navigation, windows and close

Use actual native back/forward state. A native new-window request must not become a reconstructed URL navigation. Cover POST, opener scripting, named-target reuse, ordinary/private inheritance, background creation, nested requests and opener-first retirement.

Shared routing chooses app space versus permitted native auxiliary window. Engine adapters cannot create an unregistered competing window/session owner. Existing native host capability does not require a generic visible 'Open in separate window' menu action.

A per-space close acts only on that space. A close-app operation, when invoked, enumerates that app's spaces through the same transactions and stops further close dispatch after Stay or a blocking failure. Report any spaces already closed; do not claim the multi-space operation is atomic or recreate them silently. Leaving cancels no unrelated navigation. Stay preserves the space; retry is allowed only after an observed native cancellation/terminal barrier. Preserve opener dependencies until qualified native retirement is possible. Account sign-out/switch cannot complete while old profile teardown is unresolved.

Header X scope is an unresolved product decision: close current space versus close app. Implement both semantic operations, preserve current behavior until Hemanth chooses the binding, and do not infer it from Windows appearance.

### Restore and persistence

Persist safe descriptors, not live engine handles. Startup begins on the world with offers parked for manual restoration, without eager page allocation or autoplay. Existing Continue Watching/Reading/Listening remains authoritative for media progress.

Only a successful load of the current intended main document consumes the exact restore offer. Allocation, adoption, DOM readiness, aborted or failed navigation do not. Preserve retry identity for duplicate URLs, durable dismissal, bounded/sanitized descriptors and capture/write-failure protection. Private/auth/credential-bearing descriptors remain excluded. Persist enough admitted engine preference to avoid accidentally reopening a saved engine-specific space in a different partition.

### Presentation and playback

Preserve the existing header layer except settings. Settings, extensions and space rows belong in each app's retractable sidebar. The address control expands vertically in browser chrome. The sidebar/address expansion may resize the website viewport within the stable embedded frame; controls and request UI must never overlay the website.

Tabs have icon/title rows, app-specific skins and minimal glass response. The shared taskbar projects app icons only. History and Downloads are app-style pages backed by existing owners. No top tab strip or grouped taskbar session chooser.

Max enters a separate full-size presentation of the same live space and restores its prior embedded placement; it must not stretch the embedded Feria frame. Website fullscreen is a separate state. Retain the existing live context across supported presentation changes. If native transfer cannot preserve it, block/report the capability instead of silently reloading.

App/global minimize and world-away pause inspected media; return resumes only playback paused by Colosseum. User pauses remain paused. Ordinary inactive spaces inside the displayed app retain the current agreed playback behavior. Resume state is tied to the exact media/document identity, never just the URL. Native suspension/freeze is optional and cannot break a pending close, licence operation or consent lifetime; adapters expose blockers and fail honestly when safe suspension is unavailable.

### Downloads, requests and recovery

Permission/close/file/download decisions carry source space, actual frame/origin, identity and a one-use request token. Selection changes cannot retarget a pending decision. Present controls outside the website viewport; reserve a chrome request region when the sidebar is retracted instead of forcing it open or drawing over content.

Ordinary download metadata uses the existing admission/grant path. Failed writes retain explicit retry authorization; successful writes consume it once. Private downloads are excluded from ordinary history. Define profile/download teardown dependencies; no orphaned request may survive an account switch.

Renderer loss invalidates obsolete callbacks and records a generic incident. Manual recovery targets only the affected space and commits only after actual successful loading. Close during renderer loss settles once without waiting forever for a dead renderer. Scoped cleanup distinguishes complete, incomplete, failed and unsupported; it never promises a whole-account credential wipe from partial engine cleanup.

## Capability and qualification records

Each adapter publishes capabilities with declared availability and separately recorded qualification, including runtime/SDK version, platform, build options, fixture, observed outcome and evidence. Use not-run, pass, fail, blocked, unsupported and not-applicable accurately. unsupported is a visible parity gap for a required behavior, not a passing test. Optional optimizations may legitimately differ.

Keep these claims separate:

1. Unprotected decoding: actual H.264/AAC and other selected samples, seeking and playback.
2. EME/key-system availability: requested configuration and returned result.
3. Protected playback: actual licence exchange and playback of an authorized protected test stream.
4. Provider acceptance: signed-in playback of the intended Netflix title/service flow, with quality/audio/subtitle and presentation evidence.

A built codec flag, EME availability, page load or successful generic DRM sample does not prove Netflix support. Record Qt CDM availability separately; the current codec workflow does not bundle it. Test WebView2 capabilities in the actual native controller path, without assuming full Edge browser parity. Engine choice must follow these observed results.

## Acceptance suite and delivery boundaries

Run identical behavioral scenarios through both real adapters in the same Colosseum-focused host. Match outcomes, not native API shapes. A mock validates shared policy but cannot qualify an engine.

Required cases: account switch and stale callbacks; ordinary/private isolation; multiple retained spaces; native navigation and redirects; explicit start destination; POST/opener/named/background/nested windows; opener-first retirement; Leave/Stay and immediate retry; close-app cancellation; duplicate restore URLs and failed loads; minimize/world-away/manual pause; full-size/return without reload and focus loss; native request source correctness; authorized metadata retry; renderer death during ordinary use and close; partial cleanup; memory/process release after retirement. Test rejected identities and a negative control that exposes a deliberately broken fence.

Windows adapter parity and media/provider qualification are separate reports. Linux Qt regression qualification remains required for touched shared behavior; WebView2 is not claimed as a Linux implementation. No merged-code result implies a release.

Delivery responsibilities:

- Codex: approve shared contract interpretation, own shared-policy/deck integration, receiving review and accepted main integration.
- GLM: produce the current-code/native-API mapping, identify gaps and independence boundaries, and review each candidate against this contract.
- Dots: implement bounded WebView2 native adapter batches on its assigned branch. Batch 1: retained spaces/profile identity; batch 2: requests/windows/close; batch 3: lifecycle/downloads/recovery. Include evidence, limits and exact commits.
- Preflight: independently qualify the actual Windows host and report reproducible evidence. Coordination artifacts do not become Codex operating authority.
- Hemanth with Codex: signed-in Netflix acceptance; Codex prepares diagnostics and interprets failures. Basic functional verification does not depend on Hemanth manually piloting every test.

Before implementation, assign exact file ownership per batch. Do not have parallel writers modify SessionStore, the deck or the same host files. Each batch must pass its relevant shared regression checks and real-engine cases before integration. Preserve the existing Qt path while introducing and qualifying WebView2; no engine removal is authorized by this contract.

## Review gates

This is the proposed interface/behavior contract, not the implementation plan. GLM's mapping must identify any native API limitation before a capability is promised. Hemanth reviews the contract, especially header X and any unsupported live-host transfer; the reviewed mapping then informs the written implementation plan and bounded batches. Do not infer DRM success, live provider sign-in success, or full-app qualification from existing Qt foundation test totals.

Native mapping references: [WebView2 core API](https://learn.microsoft.com/en-us/microsoft-edge/webview2/reference/win32/icorewebview2), [WebView2 new-window request/deferral](https://learn.microsoft.com/en-us/microsoft-edge/webview2/reference/win32/icorewebview2newwindowrequestedeventargs), [Qt codecs and HTML5 DRM](https://doc.qt.io/qt-6.10/qtwebengine-features.html#html5-drm). Verify mappings against the installed SDK/runtime; these references do not qualify Colosseum's behavior.
