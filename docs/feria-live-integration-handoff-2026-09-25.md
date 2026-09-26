# Feria live integration and UX handoff — 2026-09-25

## Read this first

Hemanth has approved **Feria as Colosseum's fourth world, replacing Vinyl**. The live Feria integration shown on 2026-09-25 was undercooked. His seven corrections below are the product contract for the next chat, not optional polish. He asked for this handoff before continuing; **this handoff makes no QML, C++, build, or runtime change**.

The current Colosseum checkout is `C:\Users\Suprabha\Desktop\Brotherhood\Colosseum` on `master`. At handoff, `git rev-parse --short HEAD` was `a78fb9c2`. The checkout is massively shared-dirty across unrelated account, tracker, player, and shell work. `qml/Main.qml` and `qml/TopBar.qml` are already modified; `qml/feria/` and `native/feria/` are untracked. **Inspect status and diffs afresh. Preserve everybody's work. Do not reset, clean, or broadly stage the tree.**

## Hemanth's exact UX direction

1. Restore **Tankoban / Biblio / Theatre / Feria** as the centered top selector. The pills are the visual thread that makes all worlds feel like one app. They remain visible on ordinary pages, with the established reader/player exception.
2. The visible search control says **“Search”**. Remove “or just start typing” from the UI. Keep useful type-to-search behavior unless it conflicts with focus handling.
3. Remove visible keyboard-key instruction strips/overlays. **Do not remove keyboard navigation.** Its feel was a major reason Hemanth approved the native QML parity work.
4. Remove visible **“Prototype”** labels from the live Feria experience.
5. Move **All / Watch / Listen / Read** below the app showcase, positioned like Theatre's Discover / Movies / Shows / Anime / Library row. The app showcase is Feria's counterpart to another world's carousel. Preserve easy mouse scrolling and keyboard flow into and out of the lenses and shelves.
6. Remove Feria branding and the Colosseum logo from Feria's own mast. Use the normal **“‹ Home”** world back affordance.
7. The account button must lead to Colosseum's **real account centre**. The mock Account Center is unfinished and must not be exposed as if it were the product. Hemanth wants a future **combined Colosseum + streaming-service watch-time view** in the same account/stats experience. Do not silently forget this requirement or manufacture service minutes from sample/session-dwell data.

Hemanth also requires **real local provider marks/logos**, not letter placeholders. Feria's live tile assets already have locally stored marks; preserve them. He does not want a throwaway web UI. The direction is native QML, with the accepted Portico/Feria mock and parity QML as visual/interaction references. Portico was renamed Feria; do not reintroduce Portico as the user-facing name.

## What is in the tree now

### Live shell and account

- `qml/Main.qml` resolves `"Feria"` to `feria/FeriaWorld.qml` in `worldSourceFor()` (~1066–1070). Its world loader wires `homeRequested`, `mediumSelected`, `searchClicked`, and, **if present**, `accountClicked` into the shell (~3550–3633).
- `qml/TopBar.qml` already has a shared centered four-world pill row, including Feria (~171–212), a world-only `BackAction` labelled Home, search, and an account signal. `qml/WorldPage.qml` uses this header for the established worlds. Do not invent a separate Feria mast when this shared component exists.
- `qml/Main.qml` contains the live `AccountCenter` and `AccountFlyout` (~5014, ~5069). `qml/account/AccountFlyout.qml` opens the real centre through `openCentre()` (~109–116).
- `qml/account/AccountCenter.qml` already projects Colosseum activity through native `ProfileActivity`. Its `Your Colosseum` page includes real `watchSeconds`, pages read, completions, active days and recent activity. `qml/account/AccountYourColosseumPage.qml` renders the Watch time card. It does **not** currently have a verified streaming-service playback metric.

### Live Feria

- `qml/feria/FeriaWorld.qml` is the current live fourth-world root. It has a separate `viewState` machine (`home/title/search/apps/account/host`) and live discovery/content/destination context properties. It does **not** declare `accountClicked` yet; its mock account path sets `viewState = "account"`.
- `qml/feria/PorticoDiscoveryHome.qml` draws the feature, 7-column/2-row app showcase at wide sizes, discovery shelves, and `PorticoCombinedMast` (~436). It currently has no lens rail under the app showcase. Its `topHalf` height is fixed from the viewport, so adding a rail needs a responsive height/content check at 1920×1080 and 1024×720.
- `qml/feria/PorticoCombinedMast.qml` draws the **wrong live header**: Colosseum logo, Feria wordmark, All/Watch/Listen/Read at the top, “Search or just start typing”, and a button that opens the mock account state. Remove this component from the live home path or replace its role with the shared header and a separate below-showcase lens row.
- `qml/feria/PorticoCombinedSearchBottomChrome.qml` and its `PorticoCombinedParityBottomChrome.qml` child create the exposed keyboard instruction strip, bottom icon row, and Prototype tag. The root instantiates this on every non-host state (~715–719). The search wrapper adds another keyboard-hint overlay. Remove these visible hints from the live path while retaining their underlying actions.
- `qml/feria/PorticoDiscoveryHostView.qml` also has a Prototype tag (~129). Its provider page explicitly says that provider browsing is not connected; preserve that truthful boundary until the real host works.
- `qml/feria/PorticoCombinedParityAccountView.qml` is the unfinished mock account view currently shown by Feria. Replace that user path with the real Colosseum flyout/centre. Any surviving mock-only state/keyboard branches should be removed or made unreachable, not presented as an alternative account system.
- `qml/feria/FeriaWorld.qml` keyboard logic (~540–665) currently moves `feature → app → shelf`. Add a `lens` focus stop between app and shelf, with coherent Up/Down, Left/Right, Enter, hover, focus visibility, and scroll reveal. Bracket lens shortcuts and type-to-search exist; keep working actions even though visible key legends go away.

### Discovery and provider reality

- `native/feria/INTEGRATION-STATUS.md` is the current runtime status receipt. Discovery, destination resolution, local logos, and native composition are integrated. The 2026-09-25 MSVC `colosseum` build passed, and offscreen QML checks instantiated Feria and decoded 21 brand marks plus five utility glyphs. **No visible in-app journey or provider playback was verified.**
- The production **WebView2 host is not connected to this QML world**. `FeriaWorld.providerWebViewReady` is `false`; the host view is an honest placeholder. The root's `recordedSessions` and `Settings.sessionsJson` have prototype machinery, including sample sessions. Even after host attachment, elapsed host-window time is not automatically verified video watch time. Never add sample sessions or host dwell minutes to the account's watch total.
- The foundation handoff at `C:\Users\Suprabha\Desktop\Preflight-Architect\arcs\13-streaming-local-agents\PORTICO-DISCOVERY-FOUNDATION-HANDOFF-2026-09-24.md` documents source adapters, canonicalization, cache, destination resolver, feed registry, credential state, and the separate integration lanes. It predates the present live Feria attachment; use `native/feria/INTEGRATION-STATUS.md` and current source for latest runtime truth.
- The accepted source mock is `C:\Users\Suprabha\Desktop\Preflight-Architect\arcs\13-streaming-local-agents\mockups\mockups\colosseum-portico-tv-os-prototype.html`; parity files are `colosseum-portico-tv-os-parity.qml` and `colosseum-portico-tv-os-combined.qml` in that directory. The old mock is a reference for craft and interaction, **not authority over Hemanth's seven newer corrections**.

## Recommended implementation sequence

1. Re-read `Colosseum/AGENTS.md`, inspect live diffs and the Colosseum Harness map. Keep changes tightly scoped to Feria and only the shared files needed. The latest user requirements outrank the older mock and any plan.
2. Use the real shared `TopBar` in Feria. Remove the live use of `PorticoCombinedMast`; avoid duplicate local back/header controls on title/search/apps pages. Wire Home, world selection, search, and account to existing `Main.qml` signals. The Feria search affordance must visibly read “Search”. Check top-bar spacing and focus at both window sizes.
3. Move the four lenses below the showcase. Give them a proper focus stop and scroll positioning. Preserve the feature/app/shelf interactions and responsive 7/5/3-column app grid.
4. Remove only the live bottom hint chrome and Prototype tags. Retest keyboard behavior without a hint overlay; no button should become discoverable only through undocumented shortcuts.
5. Route the Feria account affordance through `accountClicked` to the existing shell flyout/real Account Center. Remove or disable the mock account route. Record the **combined-watch-time requirement** in the actual account model work: define a verified service playback evidence source, a separate service subtotal, and the combined presentation. Until provider playback and evidence are real, show only defensible Colosseum activity; do not imply service minutes were measured.
6. Build and run relevant QML/mechanical checks. Hemanth operates visible windows himself. **Do not use screen control** or ask him to run terminal commands. Give him an app he can inspect, then treat his visual judgement as the visual gate. Be explicit about authored/build-tested/runtime-verified states.

## Acceptance checks for the next chat

At wide and 1024×720 sizes, the four world pills are centered, Feria is selected, “‹ Home” works, Search visibly says only Search, and the account icon opens the same real Colosseum account flyout/centre as the other worlds. No Feria/Colosseum duplicate mast, instruction strip, mock account page, or Prototype badge is visible. All/Watch/Listen/Read sits below the app showcase and mouse/keyboard navigation can move into it and into shelves without losing focus. Existing discovery, logos, search, app launch boundary, and other three worlds still load. Account totals are not inflated by sample data or window dwell time.

Do not call the WebView2/provider or streaming-watch-time work complete until real provider playback and verified activity evidence are connected and tested. Do not defer the seven UI corrections behind the separate world-feel overhaul; Hemanth explicitly authorized Feria integration now.

## Operating constraints and communication

Hemanth is the product creator and visual judge. He does not want terminal chores or screen automation controlling his visible windows. Speak in short plain paragraphs. Report the user-visible change first and accurately distinguish what was inspected, edited, built, and visually verified. The current shared checkout contains extensive unrelated work; no commit, push, reset, clean, branch, or worktree was made for this handoff.

This file was written by [Agent 0 (Codex), Feria handoff].
