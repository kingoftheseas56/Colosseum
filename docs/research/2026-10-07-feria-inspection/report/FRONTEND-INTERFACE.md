# Feria frontend behavior interface recommendation

This is a proposed contract for later implementation. Names below are illustrative and must be reconciled with existing code; they are not claims that these methods already exist. The brief decides behavior. The attached HTML only illustrates presentation and simulated state.

## Ownership

1. **SessionStore** remains the authoritative open-activity registry. Extend its browser record/projection for placement and host identity; do not import a donor tab database.
2. **FeriaAccountStore** remains the account-scoped canonical owner of history, bookmarks, browser preferences, manual restore descriptors and download metadata/authorization.
3. A **session-bound browser controller** operates one retained view and its current host. It can be an evolution of FeriaSessionDeck and its entry objects; durable policy/state should move into the existing native owners where useful, while QML view creation and presentation glue stays close to Qt Quick. This is not a second registry.
4. **Engine adapter** owns Qt/WebView2 operations and reports actual capability. **Host binding** owns visual parent, window placement, geometry, focus and presentation. Neither owns provider credentials.
5. Request objects stay internal to their origin-bound request owner. QML presentation gets a safe projection and commands with IDs. A request view must never accidentally refer to deck.activeBrowser after another window activates.

## Minimal observable session projection

| Field group | Required values | Existing source / change |
|---|---|---|
| Identity | sessionId, providerId, providerName, account/profile epoch, privateBrowsing | SessionStore target and deck descriptor already provide much of this; expose stable read-only projection |
| Host | placement: embedded/separate; hostId; active/parked; moving; canPopOut/canReturn/canRaise | Missing complete placement/host owner; global activeId alone is insufficient for two windows |
| Page | displayUrl, title, favicon, document generation | Browser URL/title and SessionStore updateBrowser already exist; sanitize displayed credentials without rewriting actual destination |
| Navigation | canGoBack, canGoForward, load state, progress or unknown, canStop, load error | Adapter must derive from real engine state; replace heuristic history after target-kit differential test |
| Lifecycle | active/frozen, closing, closePending, recoveryState, recoveryReason, retryAvailable | Current freezing exists; close transaction and bounded recovery need explicit state |
| Media | observed playback, suspension cause, automaticallyPaused, controllability/limitation | Existing observation is not sufficient for the new suspension contract; no fabricated state |
| Page tools | findVisible/query/pending/activeMatch/matchCount; zoomPercent/limits | Adapt existing FeriaPageTools to exact host/session, keep result-generation fencing |
| Capability | supported/enabled plus reason for print, permissions, downloads, move, media suspend, etc. | Engine/package/platform capability; do not assume Qt and WebView2 parity |
| Requests | list/current request summary: requestId, kind, origin, page generation, allowed responses, state | Existing FeriaBrowserRequests queue is a starting implementation; separate transient host visibility from lifetime |

Expose ordinary Q_PROPERTY/NOTIFY or equivalent QML bindings from actual owners. Avoid repeatedly copying authoritative state into independent writable QML models. Views may have presentation-only state such as which menu section is open.

## Commands and exact targeting

| Intent | Proposed shape | Semantics / current migration |
|---|---|---|
| New | newSession(providerId, privateMode) | Resolve provider start page from catalogue. Do not clone active URL; clear old resume/title destination metadata. Replaces newTab(activeEntry, about:blank) callers |
| Site-requested destination | internal adoptNewWindow(sourceId, request) | Synchronous engine request adoption, matching profile; destination semantics retained. Never a frontend delayed command containing a stale request |
| Activate/toggle | activateSession(id, intent) | Single provider icon toggles embedded foreground/parked state; chooser selects exact ID; separate session raises exact window. Explicit distinction avoids blind toggle on chooser selection |
| Park | minimizeSession(id) | Retain view, pause eligible media, expose underlying world; no session destruction or neighbor promotion |
| Close | requestClose(id, reason) | Start exact-session transaction; returns initiated/rejected/unavailable, not final success. Final deletion follows engine acknowledgement. Moving is not closing |
| Move | moveSession(id, destinationHost) | Keep live view/profile/document/session. Cancel or finish host transition atomically, reparent visual item and rebind overlays/focus. Return outcome without URL recreation |
| Restore | reopenClosed(providerId); restorePreviousSessions(selection) | Private excluded; explicit action only; descriptor replay is a new page. Report partial failures and remaining restore count |
| Navigate | navigate(id, textOrUrl); back(id); forward(id); reload(id); stop(id) | Validate through policy and apply to exact engine view. UI cannot set active global browser as an implicit target |
| Find/zoom | openFind(id), search(id,query,direction), closeFind(id); zoom(id,step/reset) | Existing functions may remain on controller object; bind per window. Preserve webpage viewport dimensions when UI overlay opens |
| Requests | answerRequest(id, requestId, generation, response) | Validate origin/account/page and type before one-shot response; no default screen or certificate selection |
| Downloads | pause/resume/cancel/open(downloadId) | Profile/account-scoped transfer owner, independent of current page. Return unsupported/finished/stale results honestly |
| Browser data | bookmark(id/safeUrl), queryHistory(account/filter), clearHistory, resetSitePermissions | Explicit canonical owner operations; page URLs do not become unchecked store writes |
| Recovery | retrySession(id, failureGeneration) | Bounded per-view recovery; stale retry does nothing, no selected-view fallback |

Where an operation is unavailable on WebView2, expose the limitation and disable/explain the action. Do not silently open a replacement Qt session while claiming the same live page moved across engines.

## Close state machine and cancellation seam

Proposed states: open → closeRequested → awaitingBeforeUnload → closing/draining → closed, with cancellation returning to open. A page with no before-unload prompt can acknowledge immediately. Every completion carries the originating session ID and generation; duplicate close intents do not cause double destruction.

Qt RequestClose and windowCloseRequested are public and appropriate. A standard public QML windowCloseRejected signal is **not** declared in target source. Therefore an observable cancellation result needs a deliberately owned before-unload presenter or another tested supported seam. Do not call Qt's internal dynamic method a stable API. Never infer consent from elapsed time.

Native Window.onClosing initially prevents window destruction and delegates to this handshake. The final engine acknowledgement authorizes SessionStore.close and host cleanup. A return-to-main transfer removes/parks only the empty host window and must not enter the session close transaction. Existing callback draining remains destruction safety after consent, not consent itself.

## Move and request lifetime contract

- Allocate views/components in a session/application lifetime context; visual parent change alone does not move every QML binding's context or QObject ownership.
- Enter moving state; preserve profile/session/document identity; detach old-window connections; set new visual parent/geometry; attach destination input/overlay/fullscreen/prompt bindings; restore appropriate focus; leave moving state.
- If allocation/attachment fails, keep the old live host reachable and report failure. Do not leave a session without a presentation owner.
- New-window requests are handler-bound and synchronous. Permission/client-certificate/desktop-media/WebAuth requests have distinct retention rules; the same queue strategy cannot be generalized to all Qt requests.
- A transient hide while moving must not be interpreted as user close, sign-out or persistent permission denial. Genuine navigation/account retirement must cancel stale requests explicitly and exactly once.
- Desktop-media requests must be explicitly canceled if abandoned; target Qt's unhandled-destructor behavior can select the first screen.
- WebAuth QML references do not own the native controller's lifetime; handle completion/cancel/destruction.

## Media suspension contract

Maintain auto-pause provenance separate from observation/history and resource lifecycle. Record session/account/page/frame/media identity and the transition that paused it. Restore only if that same media remains eligible and no later manual action/navigation superseded the decision. Preserve manual pause. Handle repeated minimize/world-switch causes without repeated resume.

Frame scripting and provider connectors may cover many HTML media cases. They do not prove universal cross-origin/WebAudio/protected-player control. Chromium MediaSession offers stronger internal primitives, but accessing them would require a maintained Qt integration patch and matching engine rebuild. Decide that only after public API/provider probes show a required gap. UI needs a truthful controllability state so the product never calls a mute/freeze operation “paused.”

## Presentation use of the HTML

The illustrative mock shows integrated top app controls, provider identity, transient address/find, Browser tools, app sessions, a contextual separate-window action, recovery and permission cards. Those are useful composition examples. Its simulated matches, history, session count, loading and playback are not data contracts or test oracles.

For the later QML reshaping:
- Keep the shared app taskbar, with app-named New/private/reopen commands
- Keep window placement action in Browser tools and contextual to placement
- Bind all controls to the host's explicit controller
- Use existing Theme/Glass/SVG/focus components; no full-width expanded control strip merely because a donor has one
- Overlay address/find/tools/permissions without resizing or repositioning the webpage
- Render request origin and actual errors as plain text; retain accessible names and stable objectNames
- Distinguish native maximize, site fullscreen and app immersive mode
- Show indeterminate progress when the engine has no real percentage

## Backend scope needed before frontend integration

The next implementation assignment should deliver the session-bound controller/projection, same-live-view host transfer, destination routing, unified close transaction, provider start-page lookup, explicit restore behavior, automatic-pause provenance, bounded renderer recovery and transfer-safe request/download/account cleanup. Keep canonical stores and working native Qt actions unless a specific replacement is proven stronger. It should also deliver local HTML fixtures and real Qt tests for those flows.

No new product ambiguity blocks the initial same-view transfer/close prototype. Evidence may force a product decision about provider-specific suspension exceptions, unsolicited popup policy, or sign-out impact on shared SSO. Those must be explicit; do not downgrade V1 silently. Windows receiving verification and final visual acceptance are separate from this inspection.
