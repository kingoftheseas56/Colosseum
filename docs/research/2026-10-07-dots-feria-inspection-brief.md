# Dots assignment: Feria granular donor audit and backend prototypes

Updated: 2026-10-07. Status: ready-to-send; prepared here, not dispatched by Codex.

## Objective and authority

Audit and write working donor-derived prototypes for **categories 1-5**, in catalog order. Hemanth wants both source comparison and prototype code per reviewed batch, not another broad report claiming all 52 are complete. Later batches follow 6-10, 11-15 and so on after receiving review; do not substitute 4/5/6/8/9 for this first batch.

The goal remains a complete functional Feria browser before extension work. Sites are apps; their tabs are watchspaces/readspaces/soundspaces in custom retractable sidebars over shared machinery. Dots owns comparison, bounded branch prototypes, needed nonvisual QML glue and the strongest build/test/Linux qualification it can execute. A minimal functional QML host is allowed to prove behavior. Codex owns final product styling. Production-main adoption, releases, WebView2 removal and a replacement-engine build are outside this assignment.

**Primary goal: a strong browser foundation with Chrome-like correctness and reliability in a much narrower product scope.** QtWebEngine/Chromium supplies the web engine; these batches build the navigation, page/request ownership, privacy, lifecycle, recovery and integration around it. Neither a styled mock nor copied donor snippets satisfy that goal. Narrower scope means fewer product features and supported surfaces, not weaker behavior for supported browsing. Prototypes must form reviewable working candidates toward that foundation, not disconnected demos. Full Chrome feature parity or a new rendering engine from scratch is not the objective.

## Source and input precedence

- Repository: `https://github.com/kingoftheseas56/Colosseum.git`.
- Start from **current `master`**, resolve and record its full SHA before edits. Record the exact baseline in commits/evidence. Do not retain old `97d5f18f7abad992f0686acba6219d97e4fe514d` as the new assignment baseline.
- Read applicable repository-local `AGENTS.md` and bounded live owners/callers, without unrelated documentation preloading.
- Requirements: this brief and `docs/research/2026-10-07-feria-dots-ui-contract-draft.md`. They supersede inspection-only phase wording, taskbar session groups and visible Pop out/Return presentation.
- Category inventory: `docs/research/2026-10-07-feria-browser-donor-quality-comparison.md`. Its original winners are candidates to independently re-evaluate.
- Earlier Dots report: `01cc5a48dfd9b69176380d8749d576ab1d35a661`, `docs/research/2026-10-07-feria-inspection/report/`. Fetch/read relevant evidence as needed; do not merge the whole branch to read it. FRONTEND-INTERFACE is proposed, not implemented, and must be revised for app/spaces/sidebar projection.
- Optional visual reference: `docs/research/2026-10-07-feria-spaces-reference.html`. All interactions are simulated; written requirements win over fixture behavior. Git inputs are sufficient without local Downloads paths or conversational archaeology.

Receiving Windows target was Qt 6.11.1. Check actual build metadata and qualify cloud-version differences. Previously inspected target Qt revision: `eb0793cc4b76e93cf669f586fd68c76019f40ec9`; bundled Chromium: `58c11ad487f8a237cf0ac71cc3e818b52db150df`. Verify compatibility; arbitrary modern Chrome/CEF internal classes are not automatically callable through QtWebEngine.

## Exact first batch

| Category | Audit and working prototype | Decisive cases |
|---|---|---|
| 1 | Back/Forward/Reload/Stop through supported engine actions/native state | Independent histories in two spaces; explicit-ID targeting; Stop while loading; unavailable-action state |
| 2 | Address parsing/search through Qt parsing and Colosseum URL/provider policy | Hostname, HTTPS URL, encoded search, invalid/disallowed schemes, local-file policy; app identity survives origin changes |
| 3 | Per-page history model/history-offset navigation | Right entry/offset in originating space; distinguish account History page; no chooser overlay over website |
| 4 | Independent live pages/stable canonical identity projected as app-local spaces | Independent live fixture state without reload, selection after another space changes, app-only taskbar projection; skin/label-independent actions |
| 5 | Foreground/background new-page requests retaining original engine request | Adoption within lifetime; correct selection; opener/form context; explicit window/dialog destination classification |

Compare relevant code in **all five supplied donors** for every category, including absent/default/unsuitable paths. Inspect actual functions, ownership/dependencies and meaningful tests; a feature list or search hit is insufficient.

Categories 6-10 are next-batch work. Trace adjacent dependencies when necessary and identify minimal integration changes. Category 5 must preserve existing window/dialog routes, but comprehensive desktop hosting, live-view transfer and close/reopen implementation belong to the later batch. Tab-only fixtures do not verify those cases.

## Compare, choose, then prototype

For each of the five categories, return:

1. Existing owner/callers and implemented/partial/missing behavior, with file/symbol evidence.
2. Each original donor's relevant implementation, exact revisions/paths/symbols, supported Qt surface and useful tests; absence/default recorded honestly.
3. Wider candidates only where relevant: Qt integration/examples, bundled/upstream Chromium, Falkon, Brave, CEF, Firefox or focused alternatives. State which exact source was inspected; do not imply exhaustive audits.
4. Comparison of correctness, originating-page targeting, lifetime/cleanup, privacy, resources, dependency footprint, portability, maintainability and test quality. Name defects/dependency cones; language/popularity/line count are not quality scores.
5. Selection/rejected alternatives, copied versus adapted/API-wired/behavioral-reference classification, provenance/notices and significant changes. No suitable portable donor is valid; identify necessary Colosseum integration honestly.
6. Working prototype attached to existing owners, concrete QML-facing signatures and focused fixtures/checks. Preserve canonical state, identify obsolete machinery and migration/compatibility impacts. Pseudo-code/placeholders do not count as implementation.
7. Actual verification, gaps and reproduction steps, separating source inference, models, real components and assembled-app runtime.

Current backend receives no sunk-effort preference. Take stronger usable Qt/Chromium/donor machinery when evidence supports it. A Chrome-only dependency is not a portable port: distinguish supported API access, separable code, behavioral adaptation and engine modification. Engine changes need a separate scope. Preserve selected source notices and identify actual file-specific licensing/dependency implications.

## Current frontend essentials

- App-only taskbar; spaces in per-app sidebar. Watchspaces/readspaces/soundspaces label shared tab machinery; add actions `watchspaces+`/`readspaces+`/`soundspaces+`. No top strip or taskbar session chooser.
- Preserve header minus settings. Address unfolds vertically below it. Sidebar can resize website viewport while embedded outer frame stays stable. Managed UI never overlays website.
- Settings/pending managed requests use sidebar space; History/Downloads are full app pages. Provider skins/navigation and future extensions bind to shared capabilities without owning browser persistence/identity.
- Max/Restore presents the same live space full-size, then restores prior embedded geometry; separate from site fullscreen. No visible Open in separate window control.
- Pause on app minimize/world switch; resume only automatically paused media. Fresh startup/explicit restore; private exclusions remain intact.
- Header-X semantics, media on space switching, request auto-reveal and revised site-window/platform-dialog hosting remain explicitly Open in the contract. They do not block this batch. State any prototype assumption; do not settle them through code.

## Original donor pins

- Qt Quick Nano Browser: `https://github.com/qt/qtwebengine.git`, `a7a7106bc839974d9172c19707da15c21a8a7126`, `examples/webenginequick/quicknanobrowser`.
- Qt WebEngine Lifecycle Browser: same repository/commit, `examples/webenginequick/lifecycle`.
- KDE Angelfish: `https://github.com/KDE/angelfish.git`, `40c6dc69b93f93197cc7b1216224e7e52c491635`.
- Nicol: `https://github.com/arnav-kr/nicol.git`, `abdc8db83989376a06642dd15ec647ba9ce45408`.
- Morph: `https://gitlab.com/ubports/development/core/morph-browser.git`, `39175d1346e6da48ac331b6a8f293bbe66a722eb`.
- Earlier Falkon candidate: `https://invent.kde.org/network/falkon.git`, `215414413f6338876a5e458927497f05872372f6`; verify object availability and record revisions for new candidates.

If source/runtime is inaccessible, name the exact block. Do not invent source findings or require Hemanth's computer. Packet fallback is for actual missing inputs, not mandatory duplicate ZIP delivery. Vulkan is a graphics API, not a browser donor.

## Verification and delivery

Dots may create/use Colosseum task branches, commit/push, run Actions and use the connected alternate Colosseum fork/Actions/cache. Keep source on the actual working branch. `kingoftheseas56/Dots-Colosseum` is delivery index/evidence home, not a compulsory duplicate code repository. Production adoption remains Hemanth's decision.

Use deterministic local fixtures for navigation, independent spaces, form/opener context and request lifetime. JavaScript models are not real WebEngine verification. Previous PySide lacked WebEngine modules; that limitation does not prove Dots cannot run Linux GUI. Investigate available matching native/packaged runtime or Actions routes where useful, without gratuitously rebuilding Qt.

Prefer real Lanista protocol interaction where relevant/available; a connected socket alone is insufficient. For runtime claims record OS/architecture, Qt/WebEngine/Chromium/runtime versions, executable/source identity, artifact checksum and launch context. Separate curl/proxy, native and actual WebEngine networking. Mark PASS / FAIL / BLOCKED / NOT RUN. Linux evidence remains Linux-qualified. Codecs/DRM/hardware auth/live providers/audible output/Windows require their own evidence.

Return one reviewable delivery entry with baseline, working repository/fork/branch and exact commits, five-category comparison, prototype code and revised concrete QML interface, actual tests/Actions/artifacts, known gaps and reproduction steps. No duplicate ZIP/patch/second report is required. Complete this batch's audit plus working prototype and evidence, then stop for receiving review. Do not claim production integration or all 52 categories finished.
