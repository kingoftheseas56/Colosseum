# Feria browser machinery and presentation: draft ownership contract

Status: reviewable design draft, not an implementation plan or dispatched Dots assignment.

Phase update: Hemanth subsequently assigned Dots an independent source/integration inspection first. Backend implementation is a separate later assignment after review of that inspection. See `2026-10-07-dots-feria-inspection-brief.md` for the current phase contract.

## Source and purpose

Colosseum source basis: `https://github.com/kingoftheseas56/Colosseum.git`, commit `022864eb52de908606e074827d3cd1a3a9fca8fd`.

Donor decisions and source evidence: `docs/research/2026-10-07-feria-browser-donor-quality-comparison.md` at that commit. The comparison selects individual implementations or principles; it does not authorize copying entire donor applications or adding every catalogued capability to V1.

Hemanth's division of work: Dots develops browser machinery on a Colosseum task branch; Codex reshapes the QML presentation to fit Feria's existing appearance and independently reviews returned commits, diffs and evidence. Ownership follows behavior versus presentation, rather than C++ versus QML.

## Ground truth

Present in the inspected source: retained browser entries in `FeriaSessionDeck.qml`; normal/private profiles; native account-owned history, bookmarks, preferences and download authorization; browser tools, download and permission presentation; navigation/find/zoom controls; a text session chooser in the shared taskbar.

Partial or missing: real taskbar-managed Feria desktop windows; same-live-page Pop out/Return; consistent browser close handshake through taskbar and desktop-window paths; renderer recovery; new-session provider start pages; the agreed app-icon minimize/restore behavior. Current site dialog windows are not equivalent to complete taskbar-managed desktop browser windows. Presence in source does not establish runtime correctness.

Primary current seams: `native/SessionStore.h`, native Feria account/policy owners, `qml/feria/FeriaSessionDeck.qml`, `FeriaQtWebEngine.qml`, `FeriaBrowserRequests.qml`, `FeriaBrowserDownloads.qml`, `PorticoDiscoveryHostView.qml`, `FeriaBrowserMenu.qml`, `qml/Taskbar.qml` and `qml/Main.qml`. Preserve unrelated account/activity work.

## Locked

- Each Feria provider is its own app. Its sessions use the existing shared Colosseum taskbar, with no new Chrome-style tab strip.
- Single-session app-icon activation toggles foreground/minimized state. Multiple sessions use an exact-session chooser; a detached session selection raises its own desktop window.
- Minimize parks a session and reveals the world underneath; close ends only that session. Closing the active embedded browser does not promote a parked neighbor.
- Real Feria browser windows are V1. Pop out/Return keeps the same live page, session identity and privacy/profile relationship. Closing that desktop window closes its session; returning changes placement without closing it.
- Explicit new sessions open the provider's start page. Site requests and ordinary links preserve their actual requested destination and request context.
- Pause on minimize and world switch. Resume only playback Colosseum automatically paused; manually paused playback stays paused. Vinyl is the future deliberate play-across-world feature.
- Normal startup is fresh. Continue surfaces remain the main return path; browser restore is an explicit action.
- Use the provider's display name in actions: "New YouTube session", "New private YouTube session", "Reopen closed YouTube session". A multi-provider restore action is "Restore previous app sessions".
- Keep existing navigation, app identity, options/browser tools and window controls. Put placement in the menu, not a new permanent header button: "Open in separate window" inside, "Return to Colosseum" outside. Only the relevant placement action appears.
- Address/find interfaces appear when invoked; permissions appear when requested. Site-specific controls use the existing provider policy, with remaining capabilities reachable through tools/shortcuts.
- Keep the surrounding controls visually integrated into the app. Use the browser's default wallpaper behind browser chrome rather than the Universes carousel.

## Constraints

- Preserve Colosseum's canonical session/account/policy authority and privacy behavior. Current backend implementation is not privileged: replace it when the independent inspection establishes a stronger Qt/Chromium/donor solution, identifying migration and frontend compatibility. Introduce no competing simultaneous tab, account, history, download or preference store.
- The original five-donor selections are provisional inspection evidence. Check existing Qt APIs, Qt's integration and upstream Chromium, and compare wider relevant donors before choosing backend implementations. The original selections may be retained, corrected or superseded from source evidence.
- Every adapted component records donor repository, commit, source path, retained notices and significant changes. Correct identified donor defects. If none of the donors provides a suitable component, identify the gap and the existing Colosseum owner to extend.
- Donor behaviors do not override Colosseum product choices. Do not bring in whole donor shells, implicit session restore, automatic device/certificate selection or donor-global account state.
- Private sessions never enter normal browsing history or persisted restore state. Delayed work and outstanding requests remain fenced to the originating session/account/page generation.
- Pending permission, authentication, download and close requests survive or cancel deliberately through moves and closures; they must not become orphaned or target the selected neighbor.
- Shared C++/QML source is not proof of matching Windows/Linux behavior. Installed Qt's missing proprietary codecs and unverified DRM remain separate qualifications.
- Codex's presentation changes and Dots' behavior changes must be reviewable independently. Shared files may need coordinated changes; do not prohibit required QML behavior work merely because it shares a presentation file.

## Deferred

- Cached thumbnails, hover peek, live previews, preview playback controls and visual polish.
- Generic Tankoban/Biblio/Theatre desktop pop-outs.
- General-browser entry and extensions.
- Additional donor catalog items such as article extraction, adblock, rich system printing, mobile sharing and web-app installation remain separately scoped; this contract does not silently add them to V1.

## Open

No additional product choice is introduced by this ownership split. This draft still needs Hemanth's review and approval before implementation dispatch. API signatures and the concrete cross-window lifetime solution are engineering decisions to establish against the live source and runtime, not unanswered product questions.

## Dots-owned browser machinery

Dots owns functional behavior through implementation and the strongest available build/test/Linux runtime qualification. That includes native services and nonvisual QML request/lifecycle/window glue needed for the agreed browser sessions.

The functional surface comprises provider-scoped new/private/reopen actions; exact-session selection and window activation; placement/move ownership; originating-page close handshake; site-requested destinations; per-view recovery; automatic-pause tracking; and preservation of profile/account/request cleanup. Keep existing working browser/account features, filling the identified V1 gaps through their owners.

Dots may construct the functional desktop-window host necessary to prove moves, requests and closing. It should use existing Colosseum presentation where practical and expose a usable host/content seam for the receiving UI. Final wallpaper, spacing, header/menu arrangement, labels and visual treatment belong to Codex's presentation work.

## Codex-owned presentation and receiving review

Codex owns Feria's QML visual composition: wallpaper/backdrop integration, browser header, app-named menu actions, contextual placement action, session chooser presentation, compact status/recovery feedback, dialog styling and responsive keyboard/controller-accessible interaction. Preserve the existing house components and provider identity.

Browser "Tabs" becomes "Sessions" in presentation; provider names qualify session actions. Existing bookmarks/history/downloads/settings remain reachable through the compact tools surface. The taskbar is the main switching surface; no parallel donor tab bar is introduced.

Codex inspects Dots' exact commits/diff and evidence, reproduces decisive Windows checks, and connects the visual surfaces to the agreed behavior interface. Source integration, production adoption and release states remain distinct.

## Shared behavior interface

The interface must let presentation read session identity, provider display name, title, privacy, location (inside/separate window), minimized/foreground state, request-close progress, error/recovery status and which actions are available.

Presentation must be able to request new/private/reopen, explicit restore, activate, minimize, close, move out and return for a named session. State updates must be observable and come from the existing owners. Final method/property names should follow current code rather than create a second model solely to satisfy this draft's vocabulary.

A desktop host must let the same visual browser controls bind to its own session, without selecting whichever embedded session happens to be active. Engine fullscreen and desktop maximize/restore must keep their distinct behavior even if presentation uses existing compact controls.

## Observable acceptance

- Two sessions of one provider remain independently selectable and closable; a background page's close request cannot close the foreground page.
- Pop out/Return preserves a local fixture's page state and live script state without reloading or creating a duplicate SessionStore record. Former-host lifetime, focus, dialogs and window closure are exercised on the real WebEngine view.
- Foreground/background/tab/window/dialog requests reach the right destination while retaining the original request; an opener/form fixture supplies evidence beyond a URL-only navigation test.
- Taskbar/menu/native-window close routes respect before-unload acceptance/cancellation and close only the requested session. A canceled close leaves it available.
- New provider sessions use its start page; reopening a closed normal session preserves its intended descriptor. Private sessions are excluded from closed/restore persistence.
- Minimize/world switch pause playing fixture media; restoration resumes automatically paused media and leaves manually paused media paused. Cross-frame/provider restrictions are reported explicitly rather than replaced with an unconditional play call.
- Renderer recovery targets the affected view, is bounded, exposes a failure/retry state and cannot fire after session/account replacement.
- Existing download/auth/permission/account-switch cleanup remains correct when the requesting session closes or changes window placement.
- Normal restart opens no browser sessions automatically; manual restoration is deliberate.
- Visible controls retain Feria's compact composition, provider-specific labels and context-dependent actions. No permanent Pop out/Return button or V1 preview cache is added.

## Eventual Dots dispatch and evidence contract

Once the design is approved, use the exact source pin above or explicitly replace it with a newer agreed pin and record that change. Dots may create its own Colosseum task branch, push, run Actions, or use the connected alternate fork/cache when practical. Production-main adoption is not part of the assignment.

Donor source inputs are obtainable from these recorded upstreams, not from a user-machine path:

- `https://github.com/qt/qtwebengine.git`, `a7a7106bc839974d9172c19707da15c21a8a7126`, `examples/webenginequick/quicknanobrowser` and `examples/webenginequick/lifecycle`.
- `https://github.com/KDE/angelfish.git`, `40c6dc69b93f93197cc7b1216224e7e52c491635`.
- `https://github.com/arnav-kr/nicol.git`, `abdc8db83989376a06642dd15ec647ba9ce45408`.
- `https://gitlab.com/ubports/development/core/morph-browser.git`, `39175d1346e6da48ac331b6a8f293bbe66a722eb`.

Remote availability of those exact objects was not verified here. If a required input is inaccessible, report the specific block; use a bounded source packet only when Git cannot supply it.

Return the working repository/fork, branch, exact commits/tip, actual checks and CI runs, donor provenance, and one concise `kingoftheseas56/Dots-Colosseum` delivery entry pointing to the implementation and evidence. No duplicate source ZIP or second report is required.

Record actual OS/architecture, Qt/WebEngine versions, matching executable/source identity and artifact checksum for runtime claims. Use real Lanista protocol replies and runtime interactions where available. Separate modeled tests, component tests and assembled-app evidence with PASS/FAIL/BLOCKED/NOT RUN. Distinguish native app, curl/provider and WebEngine networking. Hardware auth, live-provider/media/DRM and Windows qualification stay unverified until exercised.

## Draft self-review

The split preserves the agreed product behavior, existing owners and deferred scope. It assigns QML behavior to the machinery lane and QML presentation to the UI lane, exposes the shared-file seam, and gives observable cases rather than adding test-only product expectations. Cross-window feasibility and platform/runtime claims remain explicitly unverified. No implementation has been started or dispatched under this draft.
