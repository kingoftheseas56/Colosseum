# Ready-to-send Dots prompt: Feria categories 1-5

Brother, the next assignment is **both a granular source audit and working donor-derived prototype code**, starting with categories **1-5 in catalog order**. Your earlier 52-category report is useful evidence, but it is not exhaustive verification and its frontend contract now needs updating.

The main goal is **a strong browser foundation, akin to Chrome in correctness and reliability but far narrower in scope**. Build dependable browser behavior on the QtWebEngine/Chromium basis: navigation, independent pages, original-request ownership, privacy and later lifecycle/recovery. The mock only guides the interface. Deliver working candidates toward that foundation, not disconnected demos or copied snippets. Narrower scope must not weaken supported browsing; full Chrome feature parity and a new renderer from scratch are not the objective.

Start from current `master` in `https://github.com/kingoftheseas56/Colosseum.git`. Resolve and record the exact starting SHA before editing. Read the current files below and applicable repository-local AGENTS.md:

- `docs/research/2026-10-07-dots-feria-inspection-brief.md` — scope, donor pins, cases and delivery requirements.
- `docs/research/2026-10-07-feria-dots-ui-contract-draft.md` — revised shared behavior/presentation contract.
- `docs/research/2026-10-07-feria-browser-donor-quality-comparison.md` — stable category numbers and prior findings to independently re-evaluate.
- `docs/research/2026-10-07-feria-spaces-reference.html` — optional simulated visual reference; written requirements take precedence.

The first batch is:

1. Back/Forward/Reload/Stop.
2. Address parsing and search.
3. Per-page navigation-history model and history-offset navigation.
4. Independent live pages and stable canonical identity.
5. Foreground/background new-page requests preserving the original request/context.

For each category, compare actual relevant code from all five donors: Qt Quick Nano Browser, Qt WebEngine Lifecycle Browser, KDE Angelfish, Nicol and Morph. Compare existing Colosseum code and supported Qt APIs on equal terms. Inspect Qt/Chromium, Falkon, Brave, CEF or other focused candidates where relevant, naming exactly what you inspected. Choose by correctness, ownership/cleanup, privacy, resource behavior, dependencies, portability and test quality. Existing backend gets no sunk-effort preference. Do not equate a Chrome feature or internal class with a portable Qt implementation.

Then port/adapt the strongest usable implementation into a working prototype on your Colosseum task branch, using existing canonical owners. Record exact donor revision/path, dependency cone, retained notices, copied versus adapted code and significant corrections. Return concrete QML-facing signatures and focused fixtures/checks, not only recommendations or pseudo-code. Necessary Colosseum integration code is allowed and should be identified honestly.

The frontend now has **app-only taskbar icons** and app-local **watchspaces, readspaces and soundspaces** in retractable custom sidebars. Add actions use `watchspaces+`, `readspaces+`, `soundspaces+`. No top tab strip or taskbar session chooser. Preserve header minus settings; URL unfolds vertically below it. Sidebar may resize the website viewport; managed UI never covers the website. Max/Restore preserves the same live space and prior embedded geometry. Settings/requests use sidebar space; History/Downloads are full app pages. Provider skins/navigation can change over the shared interface.

Revise your previous FRONTEND-INTERFACE accordingly. The mock is simulated; app-X semantics, media when switching spaces, request auto-reveal and revised site-window/platform-dialog hosting remain explicitly Open. They do not block this batch and must not be silently settled by prototype code. Trace minimal adjacent dependencies, preserve existing routes and report gaps without claiming categories 6-10 are finished.

Build/test and exercise real WebEngine/Linux GUI where available, preferably through Lanista when relevant. Report PASS/FAIL/BLOCKED/NOT RUN, exact environment/artifact identity and reproduction steps. Model checks are not engine/runtime proof; Linux does not prove Windows. Return your repository/fork, branch, exact commits, five-category comparison, prototype/interface and evidence through one `kingoftheseas56/Dots-Colosseum` delivery entry. Task branches, Actions and the connected alternate fork/cache are available; no duplicate source ZIP is needed. Production-main adoption is separate. Finish this batch, then stop for receiving review before categories 6-10.
