# Dots assignment: Feria browser source and integration inspection

Status: ready-to-send inspection brief. Preparation of this file does not mean it has been sent to Dots.

## Objective and phase boundary

Hemanth wants a complete functional Feria browser with Colosseum's minimalist presentation. Inspect the existing implementation, QtWebEngine APIs/integration, upstream Chromium and relevant browser donors. Report where each required capability already exists, which implementation is strongest, and how it fits Colosseum's current owners and QML presentation.

Hemanth's explicit follow-up: disregard our implemented backend as a preferred solution. If Chromium/Qt provides a better implementation, choose the better implementation. Existing code is a comparison baseline, not a preservation requirement or a reason to reject a stronger candidate. Recommend replacing it when the evidence supports replacement; identify how the better implementation can actually be used through the target Qt stack.

This assignment is **inspection first**. Do not implement the backend, reshape product UI, patch the engine, or adopt code into production in this phase. After Hemanth and the receiving agent review the findings, a separate assignment will authorize backend implementation on a task branch. This phase does not grant that later implementation authority.

Source reads, dependency tracing, evidence/report commits and bounded disposable feasibility probes are in scope. A probe is not production implementation; keep it separate, reproducible and clearly labelled. Do not require a full browser/engine build merely to investigate an application-level feature.

## Exact source basis

- Repository: `https://github.com/kingoftheseas56/Colosseum.git`.
- Fixed application source pin: `97d5f18f7abad992f0686acba6219d97e4fe514d`.
- Relevant foundation commit for inspection: `b53a22d3e47bb286219973104905333e9ff6f544`.
- Read the applicable repository-local `AGENTS.md` instructions.
- Record this exact starting SHA before inspecting. Do not silently replace it with a newer master tip. Describe relevant drift separately if a newer revision becomes useful.

At the source pin, read these bounded inputs:

1. `docs/research/2026-10-07-feria-browser-donor-quality-comparison.md`: 52 capability categories and source findings for the five supplied donors. These are prior inspection findings, not an instruction to retain the same winners.
2. `docs/research/2026-10-07-feria-dots-ui-contract-draft.md`: product behavior and division between browser machinery and presentation. It is a draft contract, not an implementation instruction. The new inspection-first instruction in this brief supersedes its prospective backend assignment and its preference for the original donor selections.
3. Live owners and callers needed for the feature under inspection. Do not preload unrelated recaps, governance or history.

Primary source seams include `native/SessionStore.h`, `native/feria/FeriaAccountStore.*`, `native/feria/FeriaBrowserPolicy.*`, `qml/feria/FeriaSessionDeck.qml`, `FeriaQtWebEngine.qml`, `FeriaBrowserRequests.qml`, `FeriaBrowserDownloads.qml`, `FeriaPageTools.qml`, `FeriaBrowserMenu.qml`, `PorticoDiscoveryHostView.qml`, `qml/Taskbar.qml` and the relevant session/window glue in `qml/Main.qml`.

The receiving Windows target has Qt 6.11.1. Inspect the actual target/build metadata and distinguish that from any different Qt version available in your cloud environment. Resolve and record the Qt integration and bundled Chromium revision relevant to the target; an arbitrary current Chromium or Chrome tag is not automatically ABI/API-compatible with it.

The local disposable UI mockup is illustrative and not production source or an approved specification. It is not available at this Git pin and is not a required input for this inspection. If supplied separately, use its layout/states as a visual reference; do not infer product requirements from its simulated JavaScript, fixture content or placeholder operations. The written behavior contract and real source are sufficient to begin.

## Product contract to preserve

- Each provider/site is its own Feria app. Sessions use the existing shared Colosseum taskbar and canonical SessionStore identity. No extra Chrome-like tab strip.
- A single-session app icon toggles minimize/restore; multiple sessions use an exact-session chooser. Selecting a separate-window session raises that exact window.
- Minimize parks the session and reveals the world underneath. Close ends only that session; active embedded close does not promote a parked neighbor.
- Real Feria desktop browser windows are V1. Pop out/Return should preserve the same live page and session/profile identity. The placement action is contextual inside Browser tools, not an additional permanent header icon. Closing its desktop window closes its session.
- Explicit new sessions start at the provider's start page. Site-requested destinations retain the original request semantics, including foreground/background/window/dialog handling, opener and form state.
- Pause on minimize and world switch; resume only automatically paused media. A manually paused page stays paused. Report cross-frame/provider limitations rather than silently weakening this contract. Vinyl is the later deliberate play-across-world feature.
- Normal startup is fresh. Continue Watching/Reading/Listening remain primary return paths; browser restoration is explicit.
- Presentation uses app-named actions, e.g. "New YouTube session". Address/find interfaces appear on invocation and permissions on request. Keep site-specific controls and the existing integrated QML appearance, with the browser's default wallpaper behind browser chrome.
- Existing account/profile/privacy boundaries and Colosseum's canonical data authority remain in force. Backend internals and interfaces may be replaced if a better implementation warrants it; avoid competing simultaneous registries. Identify state migration and frontend compatibility consequences for any replacement rather than requiring the current implementation to survive unchanged.
- Cached thumbnails, hover peek, live previews, preview playback controls, generic reader/player desktop pop-outs, general-browser entry and extensions are deferred. Index their eventual dependencies, but do not turn them into V1 requirements.
- Reader mode, adblock, rich printing and other extra donor features remain separately scoped candidates. Identify their fit without assuming they are approved for implementation.

## Inspection questions

### Existing Colosseum code

Establish what is already implemented, what is partial, and what is missing for each capability. Trace state ownership and callers rather than treating a UI control as proof that a function works.

Inspect the foundation commit's native changes and donor-derived QML. Classify useful existing components, duplicated ownership if actually found, regressions, unverified assumptions and missing integration. Identify concrete source evidence; do not assume that the presence of C++ means another browser's backend was transplanted. Compare current implementations on equal terms with the candidates, without rewarding sunk effort. Retain, adapt, replace or remove components according to demonstrated quality and integration fit; no deletion or replacement is executed in this inspection phase.

### Qt and Chromium

For each capability, check the supported Qt QML/C++ API first. Distinguish engine-provided functionality from application-level ownership, persistence, consent, error handling and presentation that Colosseum must supply.

When an API appears missing or insufficient, trace the relevant Qt integration and upstream Chromium source. Explain what can be reached through supported Qt interfaces, what needs a small application helper, and what would require changing Qt/Chromium itself. Label engine changes explicitly, with their dependency/build/maintenance consequences; do not propose them merely because Chrome has a feature.

### Wider donor comparison

Do not restrict candidates to QML, Linux browsers or the original five. Chromium, Qt's other examples, CEF, Falkon, Brave, Firefox and other focused donors may be useful where they have a relevant separable implementation or stronger behavioral evidence. This is a candidate pool, not a requirement to audit all those codebases in full or select Chromium for every row.

Compare implementations by correctness, lifetime/cleanup, account/privacy boundaries, resource behavior, maintainability, test quality, dependency footprint, portability and provenance. Language alone is not a quality score. Preserve source notices and identify copying versus behavioral adaptation without claiming blanket license clearance.

For a proposed source port, identify the concrete dependency cone: imports/libraries, framework types, services, IPC, platform assumptions and build flags it actually needs. A class using Chromium `WebContents`, `base`, Mojo, Chrome Views or CEF types is not automatically callable from Feria's existing QtWebEngine object.

Review relevant donor defects already documented, and verify or correct those findings independently. Neither donor quotas nor incumbent preference decide the result. If Chromium has the stronger solution, recommend taking it, documenting whether that means using an already exposed Qt capability, porting separable code, adopting its behavior with compatible machinery, or changing the engine integration.

## Required feature map and integration findings

Use all 52 categories from the existing comparison as the common inventory. Split broad rows when doing so materially changes the conclusion. Label each capability V1, deferred or separately scoped; prioritize detailed integration findings for the agreed V1.

For each row, supply:

- Current Colosseum owner and implemented/partial/missing status, with file/symbol evidence.
- Qt API availability and target-version requirements, including whether it is QML-facing, C++-facing or engine-internal.
- Compared sources and the recommended implementation/reference; exact repository, revision, file and symbol. "No suitable portable donor" is a valid result.
- Classification: keep existing, replace existing, wire supported Qt API, port separable code, adapt behavior, engine change required, unresolved or deferred.
- Where it fits: owner to extend or replace, required dependencies, significant corrections, frontend/API compatibility, data migration and observable integration risks. A replacement recommendation must name which current machinery becomes obsolete and how canonical state remains intact.
- What presentation needs to read/request and how state updates are exposed. Keep browser logic ownership separate from QML styling; neither lane is limited to one language.
- Verification already supported by evidence and decisive checks still needed. Distinguish inspected source, inference, probes and actual runtime results.

Include focused conclusions on these high-risk seams:

1. Same-live-page transfer across QQuickWindows: QML context/QObject/visual ownership, graphics, focus, dialogs and original-host destruction. Evaluate Morph's helper as a candidate, not a guaranteed solution.
2. Original new-window request lifetime and destination routing, including opener/form context.
3. One close handshake across taskbar, menu, page and native-window closure, with before-unload cancellation and exact originating-session targeting.
4. Shared profile/account/private ownership, pending requests, download authorization and account-switch cleanup across window moves/closure.
5. Freeze/wake versus explicit media pause/resume; bounded per-view renderer recovery and cancellation of delayed work.
6. Existing WebView2 boundary and current QtWebEngine route: state which findings apply to which engine and whether any assumption constrains future engine convergence. This inspection does not authorize engine removal or a second embedding stack.
7. The smallest shared behavior interface needed by the minimalist frontend, plus any genuine product choice that source evidence cannot resolve.

Do not turn the report into implementation or declare the eventual integration already complete. Explain feasible approaches and their tradeoffs sufficiently for the later assignment to be concrete.

## Evidence and delivery

Dots works in its own cloud environment. You may create/use a Colosseum task branch for report/evidence work, push it, use Actions or the connected alternate fork/cache where useful. This assignment changes the phase's objective, not your ordinary Git route authority. Production-main adoption remains Hemanth's decision.

Return one concise delivery entry in `kingoftheseas56/Dots-Colosseum` pointing to the exact source pin, working repository/fork, report/evidence branch and commits, source comparison, actual probes/checks, known gaps and reproduction instructions. A report file is useful for the feature map; the delivery entry can point to it without duplicating its contents. No source ZIP, patch bundle or duplicate report is required when Git carries the evidence.

Original donor inputs, for reproducibility:

- `https://github.com/qt/qtwebengine.git`, `a7a7106bc839974d9172c19707da15c21a8a7126`, `examples/webenginequick/quicknanobrowser` and `examples/webenginequick/lifecycle`.
- `https://github.com/KDE/angelfish.git`, `40c6dc69b93f93197cc7b1216224e7e52c491635`.
- `https://github.com/arnav-kr/nicol.git`, `abdc8db83989376a06642dd15ec647ba9ce45408`.
- `https://gitlab.com/ubports/development/core/morph-browser.git`, `39175d1346e6da48ac331b6a8f293bbe66a722eb`.

Record revisions for newly inspected sources too. Remote availability of these original objects has not been verified by the assigning agent. If required source is inaccessible, report the specific block; do not invent source findings or require access to Hemanth's machine. Packet fallback is available only if it solves a real missing-input problem.

For actual runtime probes, record OS/architecture, Qt/WebEngine/Chromium versions, exact source/artifact identity, checksum and launch context. Use real Lanista protocol replies where relevant and available. Separate component/model tests from assembled-app GUI results; mark PASS/FAIL/BLOCKED/NOT RUN. Linux evidence remains Linux-qualified. Codecs, hardware authentication, protected providers, audible output and Windows behavior require their own evidence.

Completion means a source-backed capability/integration map and a usable recommendation for the next assignment. It does not mean the backend is implemented, merged or released. End with the necessary backend scope, frontend interface, unresolved decisions and verification needs for receiving review.
