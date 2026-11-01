# Batch 5 — Finished tracker and utility mocks

TASK: Complete the seven tracker and utility mocks inside the assigned batch.
STATUS: ready-for-review — seven runnable pages, verified fixes, no unfinished mock repair.

The usable deliverable is [finished-mocks/index.html](finished-mocks/index.html). It links every finished page, its unchanged original worker input, before/after comparisons, contact sheets and evidence. This pass follows the user’s scope update authorizing repairs inside this batch; the original brief’s review-only restriction was superseded. No other batch or application/repository file was changed.

## What is finished

| Page | Finished work and exercised behavior |
| --- | --- |
| [Connections](finished-mocks/tracker-connections.html) | Restored working responsive/capture CSS, latest approved hierarchy and wash; connected/disconnected/attention fixtures; provider catalogue, local connection changes, preferences and original actions. |
| [Tracker dossier](finished-mocks/tracker-dossier.html) | Dossier body no longer dismisses on ordinary clicks; provider capability gates; import matching and separate progress decisions; bulk/export/disconnect/removal dialogs; long-list and keyboard scrolling. |
| [Tracker preferences](finished-mocks/tracker-preferences.html) | Original switch geometry, readable labels and disabled states; all four preferences; short-height scrolling, Tab containment and Escape close. |
| [Wallpaper search](finished-mocks/wallpaper-search.html) | Six source-native scenes, all 13 source-named KDE picks, accurate search-fixture titles; selection, local Apply, sort/random, zero results, Load more, shelf scrolling and keyboard fitting. |
| [Extensions Store](finished-mocks/extensions-store.html) | Reachable House strip, correct dark colors and logo washes, original hover lift and installed gating; typed URL preview/error/install/Done; source setup panels; installed enable/remove/configure; original text menu and focus. |
| [Update](finished-mocks/update.html) | All 12 original update states; local action transitions; unknown-size progress; Next wrap versus arrow-key clamping; long copy and status controls fit both orientations. |
| [Collections](finished-mocks/collections.html) | Isolated edit draft; Cancel, Save, Undo, reorder, notes and validation; creation, memberships, delete/discard, empty states; source title context and icons; pagination, feedback, background wash and Taskbar clearance. |

The existing design remains the basis: actual artwork and fonts, original card/panel/menu forms, icon paths, source copy and capabilities. Fitting changes wrap, stack, clamp or scroll those elements. No new phone surface or behavior exception is proposed for approval.

## Run and review

Run the supplied dependency-free preview server with the existing Node runtime:

```powershell
node D:\Colosseum-Android-Phone-Mocks-20261101\review-batches\05-tracker-utilities\tools\serve-mocks.mjs
```

Open its printed loopback URL. The server was started independently and checked for all seven pages, the index, code, an added KDE image and a reference font: **12/12 HTTP checks passed**. That check server was stopped afterwards. The mock assets use relative paths to the supplied reference tree, so keep the project folder structure intact. See [finished-mocks/README.md](finished-mocks/README.md) for fixture addresses and controls.

The review controls sit outside the product frame. They expose viewport, insets, 130% text and a synthetic raised keyboard. Utility pages also expose long fixtures; Update exposes unknown size. Reload restores fixture data. The main index also links the [filterable capture gallery](finished-mocks/evidence.html) and [contact sheet index](contact-sheets/index.html).

## Verification completed

| Verification | Result | Evidence |
| --- | --- | --- |
| Required pages | 7/7 finished, each reviewed upright, sideways and against desktop source | [COVERAGE.csv](COVERAGE.csv) |
| Baseline evidence | 21 unchanged-worker captures: portrait, landscape and desktop for each page | Per-page [before/after comparisons](finished-mocks/comparisons.html) |
| Final viewport sweep | 63 captures: seven sizes plus two compact stress cases for each page | [final-matrix.json](evidence/final-matrix.json) |
| Final open-state and scrolling sweep | 108 captures, including dialogs, menus, all update states, keyboard cases and long scroll ends | [state-matrix.json](evidence/state-matrix.json) |
| Tracker action scenarios | 8/8 passed | [tracker-actions.json](evidence/tracker-actions.json) |
| Utility action scenarios | 21/21 passed | [utility-actions.json](evidence/utility-actions.json) |
| Additional keyboard/source scenarios | 15/15 passed | [final-keyboard-and-source-actions.json](evidence/final-keyboard-and-source-actions.json) |
| Final code-to-capture match | 171/171 indexed capture receipts match the final loaded HTML/CSS/JS hashes; none has an empty hash list | [final-capture-version-check.json](evidence/final-capture-version-check.json) |
| Assets, scripts and platform chrome | No missing images, script/HTTP errors or blocked external browser requests in those 171 captures; no Android desktop chrome; native wallpaper canvases render | Same version check and viewport/state receipts |
| Input preservation | 29/29 files snapshotted at intake remain byte-identical | [input-preservation.json](evidence/input-preservation.json) |
| Contact sheets | 10 HTML/PNG sheets, with image hashes and provenance | [contact-sheets/provenance.json](contact-sheets/provenance.json) |
| Standalone runnable server | 12/12 checks passed | [standalone-server-check.json](evidence/standalone-server-check.json) |

The **44 action and keyboard scenarios** exercise observable results, not only button presence. Examples include six collection titles remaining after Remove → Cancel; a 500-character note surviving reorder and Save; overview pagination from 36 to 44 collections; separate matching and progress decisions; disabled duplicate installation; and reaching the final Apply/Install/Create action above the simulated keyboard.

The sizes are **412×900, 900×412, 354×792, 792×354, 1280×700 desktop, 800×1280 tablet and 1280×800 tablet**. Compact stress uses **1.3 text scale and illustrative 24px top/bottom insets**. Keyboard tests reserve space without inventing OS chrome. Long fixtures include 44 collections, a 46-title collection, long tracker reviews and expanded descriptive copy. The initial and final scroll positions are both recorded where relevant.

The isolated Windows Edge session reports **Edg/156.0.4314.50**, deviceScaleFactor 1. It used its own profile and localhost server, without existing user tabs or global mouse/keyboard. [browser-session.json](evidence/browser-session.json) and [browser-closed.json](evidence/browser-closed.json) record ownership and closure. Browser requests were restricted to local artifacts/data URLs. Static KDE asset retrieval is separately recorded and was not an application/provider operation.

Visual review included the seven final before/after sheets, the full-page overview, compact landscape stress, keyboard action captures, long scroll ends, collection picker, original text menu and desktop hover. This supports layout and source-fidelity conclusions for the HTML mocks; it is not a claim that every native rendering path was tested.

## Important fixes and observed evidence

**B5-001 — tracker parsing and fitting.** Two missing closing parentheses in the original tracker CSS prevented later responsive/capture rules from parsing. Required original captures retained the harness and cut through the app frame. The finished stylesheet parses and fills each intended viewport; the approved Connections structure is preserved. The original captures remain available rather than being cosmetically repaired.

**B5-002 / B5-010 — wallpaper identity.** The worker labeled unrelated saga images as native scenes and KDE wallpapers. The finished page renders the original shader/gradient definitions and contains the full curated KDE set. Search fixtures now use their actual project titles instead of pretending to be live Wallhaven results. The final KDE tile and Apply action were exercised.

**B5-003 — collection cancellation.** The original Remove → Cancel sequence reduced a six-title collection to five. The corrected page keeps a separate draft, so cancellation restores the original data. Save, Undo, notes, order and membership updates were separately exercised. [before-cancel-regression.json](evidence/before-cancel-regression.json) preserves the reproduction, and [after-collections-cancel-preserves-six.png](evidence/after-collections-cancel-preserves-six.png) shows the corrected state.

**B5-004 / B5-005 — add-on interaction.** Typing did not enable URL Preview, later House tiles were inaccessible, and several card actions had no local result. The finished copy validates input, shows the source preview/completion/error states, updates installed fixtures and makes the original strip scroll. It uses the approved Android centered setup panel and the original desktop blocked-page fallback.

**B5-006 / B5-008 — platform and visual fidelity.** Android tablets no longer receive desktop window chrome. QML ARGB colors are translated correctly for CSS, restoring dark installed cards. House art remains opaque. The add-on wash geometry, hue calculation, source scrim, hover lift and installed-button gate now match the inspected component.

**B5-007 — update state coverage.** Checking, Installing and unknown-size handling are represented, source action transitions work locally, and keyboard arrows clamp while Next wraps. Long text scrolls without taking over the navigation or status controls.

**B5-011 / B5-012 — dialog behavior and capabilities.** Original panels retain reachable controls in compact/keyboard cases, modal focus is contained and restored, and MAL’s read-only capability gate prevents send/live/export interactions. No new phone sheet was needed.

**B5-013 / B5-014 — collection source details.** Restored the source wallpaper wash, desktop bottom reservation, 36-item pagination and 4200ms confirmation. “Create & add” retains the selected title/creator/type context, source icons and empty-search text. Nested Cancel returns to the picker and focuses New collection.

All **13 repair findings** are fixed and verified. The remaining finding is an inherited provenance gap, B5-009, described below. [FINDINGS.csv](FINDINGS.csv) keeps stable IDs, observed symptoms, source references, evidence and the resulting action. Source comparisons and inferred causes are distinguished from directly reproduced behavior.

## Source, artwork and provenance

The authority is frozen desktop **a40f66be06f22cdbe75e97e7dc4f1108ab2f5d41**, including the reviewed Connections refinement. Alpha 77844eef is a recovery reference; the rejected Android snapshot was not adopted as the target design. [reviewed-sources.json](evidence/reviewed-sources.json) identifies 36 inspected source files; Main/Taskbar inspection was limited to relevant integration. [reference-assets.json](evidence/reference-assets.json) hashes the 50 reference assets requested during the browser review.

The other 12 KDE stills were fetched unchanged from the exact KDE repository paths named in the frozen WallpaperApi.js and saved only under this batch’s finished-mocks/assets. Their SHA-256, ETag, content type, size, artist and source license string are in [kde-assets.json](evidence/kde-assets.json). Cold Ripple remains the supplied project asset. These upstream URLs use master, so the recorded content hashes identify the fetched bytes; they are not falsely described as pinned by the QML commit. No Wallhaven, account, add-on, tracker or updater API was called. The runnable pages need no external requests.

Before/after images are **original worker HTML versus corrected HTML**. The applicable historical capture inventory contains no directly matching current native Batch 5 reference. Desktop fidelity was checked against approved QML and assets, with desktop HTML comparisons, not against an invented approved app screenshot.

Earlier diagnostic screenshots and first-run receipts are preserved. They are not the final passing index. `utility-actions-first-run.json` includes earlier test-helper problems with empty input and an inert duplicate control. `utility-actions-kde-harness-failure.json` records a test-selector error corrected before the 21/21 rerun. `final-keyboard-first-run.json` includes the nested picker focus issue that was fixed, plus a pagination test-selector error. Final receipts and exact code hashes are separately identified so these records cannot be mistaken for current failures or approvals.

## Remaining items and limits

- **B5-009, unavailable:** the original tracker worker’s REPORT.json was absent at intake. It remains absent because original worker outputs are preserved. This batch’s independent versioned evidence and completion report cover the finished work; an earlier handoff can be supplied later if it exists.
- **Native reference unavailable:** no directly applicable current approved native Batch 5 capture was supplied. The browser/source review does not establish native pixel equivalence.
- **Device sizing remains provisional:** exact logical viewport, DPR, safe insets and keyboard geometry await device measurement by the authorized owner.
- **Integration is outside this assignment:** real authentication/provider content, persistence, shell destinations, clipboard operations, update installation and application window operations are not executed by these local fixtures. The corresponding source components and integration cautions are identified in QML-PORTING.md.
- **No unfinished mock repair or fixed-but-unverified page remains.** Final application acceptance and any native implementation/verification remain with Hemanth and Codex.

No application/repository edits, builds, installation, device interaction, live service operation or publication were performed. Original worker outputs, frozen references and other batches were preserved.

## Receiving handoff

Start with [QML-PORTING.md](QML-PORTING.md), which maps each chosen layout to the actual component and object names, explains fitting dimensions and keyboard rules, identifies state contracts to preserve, and separates HTML-only repairs from QML changes. Use [COVERAGE.csv](COVERAGE.csv) and the capture gallery to choose the exact stress/state case before implementing. No fresh investigation of asset identity, provider gates, collection draft behavior or the original menu/setup components is required to understand this proposal.

The complete machine-readable delivery index is [REPORT.json](REPORT.json), written last after evidence, coverage, preservation checks and session cleanup.
