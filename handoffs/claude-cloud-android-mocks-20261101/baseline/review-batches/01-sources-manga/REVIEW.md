# Batch 1 — finished mocks and review handoff

## Result

**The nine required HTML artifacts are delivered under finished-mocks/.** Seven primary pages are corrected and verified. The volume-flow pair supplies a corrected constrained original attempt and a verified readable proposal. The proposal awaits Hemanth's decision; the original attempt deliberately retains its documented sizing failure as comparison evidence.

Start with [the finished index](finished-mocks/index.html) or [START-HERE.html](START-HERE.html). Every page links to its untouched original and an interactive before/after comparison. [Contact sheets](contact-sheets/index.html), [coverage](COVERAGE.csv), [findings](FINDINGS.csv), [decisions](DECISIONS.md), and [QML implementation notes](QML-PORTING.md) are part of this handoff.

The later user instruction authorized mock repairs and superseded the review-only wording in the earlier brief/README. Work continued from the review evidence already collected. All task writes stayed inside this assigned batch. Original worker output, frozen references, other batches and app/repository code were preserved. The recorded input comparison contains **221 tracked entries: 217 existing files and four expected absences**, all matching their initial state. See [input preservation check](evidence/input-versions-end.json).

## Run and inspect

Keep the finished folder inside this project tree so its relative paths resolve to the supplied artwork, fonts and icons. Open finished-mocks/index.html directly for navigation, or use the following local, read-only static preview from Windows for the verified HTTP path:

~~~powershell
py -m http.server 8765 --bind 127.0.0.1 --directory "D:\Colosseum-Android-Phone-Mocks-20261101"
~~~

Then open:

~~~text
http://127.0.0.1:8765/review-batches/01-sources-manga/finished-mocks/index.html
~~~

The one headless review browser and its temporary loopback server are closed at handoff; this command starts a fresh user-owned preview. There is no installation, build or live-service requirement. The browser-session receipt records the actual review environment and cleanup.

The comparison viewer renders the two pages at identical logical dimensions, then scales the surrounding frames only to fit the review window. It offers the required portrait, landscape, compact, desktop and tablet sizes; 1.0/1.3 text; 0/24 px top and bottom insets; relevant fixture states; and initial/gallery/bottom positions. Its separate flow-pair mode compares the corrected original allocation against the proposed allocation. Controls remain interactive.

Actions that would open an outside reader, collection manager, settings screen or acquisition service produce local fixture feedback. No service operation is implied by a gold button or a completed fixture state. Within-batch series/chapter navigation and local control/state interactions are runnable.

## Page coverage and final status

All pages were checked in portrait and landscape, against frozen source and desktop HTML, with the relevant controls and long-content states. Each final per-page check file contains its actual observations, not an unchecked “pass.”

| Finished page | Final status | Main verified result | Evidence |
|---|---|---|---|
| film-sources.html | Finished, verified | Provider/menu dismissal, keyboard scope, filters, source actions and desktop download alignment | [25 checks](evidence/finished-film-checks.json) |
| episode-sources.html | Finished, verified | Shared picker fixes plus full-season pack behavior | [26 checks](evidence/finished-episode-checks.json) |
| comic-sources.html | Finished, verified | Filter-preserving weak cancel, modal focus, archive/recovery states and query alignment | [26 checks](evidence/finished-comic-checks.json) |
| comic-source-confirmation.html | Finished, verified | Same state model, confirmation and long-stress reachability | [26 checks](evidence/finished-comic-confirm-checks.json) |
| manga-volume-sources.html | Finished, verified | Native status disc/timers, batch/series context, long-content scrolling and missing metadata | [26 checks](evidence/finished-manga-sources-checks.json); [metadata](evidence/manga-metadata-checks.json) |
| manga-series.html | Finished, verified | Explicit default source composition, 107 reachable volumes, scale falloff, focus and action fitting | [27 checks](evidence/finished-series-checks.json) |
| manga-chapters.html | Finished, verified | Default ledger, anchored menus, focus, local download lifecycle, empty/error/extension states | [32 checks](evidence/finished-chapters-checks.json); [desktop menu](evidence/chapter-desktop-menu-check.json) |
| volume-flow-original-fit.html | Comparison complete; known constraint | Shared controls/fidelity fixes verified; cover is below source minimum at six sizes | [42 checks and six constraints](evidence/finished-flow-original-checks.json) |
| volume-flow-minimal-alternative.html | Finished proposal, verified; decision open | Full readable focused cover and reachable actions through measured scroll allocation | [42 checks](evidence/finished-flow-alternative-checks.json) |

The final per-page suites contain **272 passed checks**, with the original flow's **six known sizing constraints recorded separately**. Additional metadata and default desktop-menu checks pass, as do **13 comparison-viewer checks**. These counts include resource, geometry and interaction checks; they are not 272 independently verified native app behaviors.

There are **no final HTML changes left fixed-but-unverified**. The optional rebuilt route is explicitly a component comparison, not a claimed finished native route. Hemanth's product decision and native runtime work remain outside the browser-verification result.

### Viewports and stress conditions

- Phone portrait / landscape: **412 × 900 / 900 × 412**.
- Compact portrait / landscape: **354 × 792 / 792 × 354**.
- Desktop reference presentation: **1280 × 700**.
- Tablet orientations: **800 × 1280 / 1280 × 800**.
- Text/inset stress: compact portrait and landscape at **1.3 text scale**, **24 px top and bottom insets**, with relevant long content, open menus and scroll-to-last-action checks.

The ordinary size matrix uses text 1.0 and zero simulated insets. Stress settings are deliberately provisional. Tablet controls were checked separately from the phone rule. Browser-emulated touch was exercised for flow swipe selection; no device or ADB interaction occurred.

## Findings resolved

Stable IDs and source/evidence references are in FINDINGS.csv. Shared component findings name every affected page.

| IDs | Observed issue and implemented result |
|---|---|
| B1-001, B1-002 | Theatre provider dismissal activated rows or closed the picker; a transparent event catcher, scoped Escape and focus return correct it. Download/season mode now removes the absent action's grid column and restores the source right gutter. |
| B1-004, B1-005 | Comic weak-cancel reset filtered results and keyboard focus escaped behind the modal. Query/selection/scroll now survive, focus is contained and returned, archive labels use basenames, and the desktop query anchor is restored. |
| B1-006, B1-007 | Manga failure/completion could freeze, live messages used the wrong volume context, and tight long-content landscape left too little list space. Source timer beats, eligible batch selection, contextual messages, the native status disc and conditional scrolling now work. |
| B1-008 | The worker mixed incompatible default and optional rebuilt series compositions. The finished target follows Main.qml's default with the brief's explicit restorations; optional rebuilt components are separately labeled. |
| B1-009 | A 107-volume label exposed only ten items, neighbor scales were flattened and the desktop action bar fell below the viewport. The model, scale falloff, centering, caption and action budget are corrected. |
| B1-010, B1-014 | Chapter menus lacked scoped focus/collection keys; download updates lost focus and skipped states. These are corrected, recovery states are present, and the final desktop page menu uses the source's 36 px rows, 300 px cap and glass option states. |
| B1-011 | The flow bookmark was inverted to black, required header elements were missing, keyboard focus was lost on virtual rerender and pending Read used the wrong fill. The source-facing icon, header, focus and pending/retry/fallback behavior are restored. |
| B1-013 | A new missing-metadata stress fixture exposed NaN, -1 and fabricated zero in the copied source renderer. Unknown values are omitted while a real reported zero remains. This is not a claim that the original default populated fixture displayed NaN. |

Twelve findings are resolved for the delivered target. B1-003 remains an original-input provenance gap, mitigated by this fresh handoff. B1-012 remains the explicit flow decision.

## Decision that remains

The constrained original volume fit yields **139 px** covers at 412 × 900, **66 px** at 354 × 792, and **16 px** in the two ordinary short landscape cases and the two text/inset stress cases. The source minimum is **190 px**. Shared control fixes do not make that allocation acceptable.

The proposal keeps a 190 px cover in those cases by reserving the whole scaled cover/caption and allowing the existing content to scroll. At 792 × 354 with text 1.3 and 24 px insets, chrome joins that scroll; the gallery and action bar require different positions. Desktop/tablet geometry remains corresponding and needs no gallery-position scroll. [DECISIONS.md](DECISIONS.md) contains all nine measured rows, runnable alternatives and the concrete cost.

**Recommendation:** review the completed proposal for adoption. Hemanth's acceptance is pending. A flattened flow, replacement grid or new phone surface was not substituted.

## Evidence and provenance

The frozen desktop/alpha/rejected-drift pins are recorded in INPUTS.json. Source files and worker artifacts were read directly from the assigned local project. [QML-PORTING.md](QML-PORTING.md) names the original components, symbols, dimensions, fitting changes, native signals and mock-only behavior so implementation can proceed without repeating source discovery.

Fresh capture receipts contain the viewport, parameters, time, loaded resource paths/byte lengths/SHA-256 hashes, errors and blocked-request observations. [The contact-sheet manifest](evidence/contact-sheets-manifest.json) binds each final sheet to its constituent screenshots and receipts. Earlier baseline, failed-state and working captures are preserved as evidence; they are not all final acceptance captures. The final manifest distinguishes current-resource captures from earlier working states.

**16 contact sheets** cover per-page portrait/landscape before/after, three desktop groups, two flow decisions, open controls/states and a source reference comparison. Sheet sources were checked against current resource hashes. Individual full-resolution screenshots remain linked from the sheet index.

Historical T5 screenshots are labeled archival component references. Their exact native runtime/input revision is not established, and their fixtures/backgrounds differ. They are not treated as current app validation. HTML desktop rendering likewise does not establish native app acceptance.

All imagery, icons and fonts loaded for the finished pages are existing project assets. The local inventory did not locate a corresponding manga volume-jacket/chapter-image set; supplied One Piece illustrations and film posters remain disclosed stand-ins. The corrected source no-cover fallback is included. No new artwork, network asset, backend data or cover identity was fabricated.

## Remaining work and boundaries

1. **Hemanth:** choose the flow allocation using DECISIONS.md and the interactive original/proposal comparison.
2. **Codex:** implement the chosen fit in the original QML components, using QML-PORTING.md. That app/repository work was outside this pass.
3. **Later native validation:** check real font metrics, Android safe areas/input methods, accessibility/focus, neighbor-tap event order and service-driven acquisition/consume/cancel lifecycles after separate authorization. The possible onPressed/onClicked focus-order concern is a source-code inference, not an observed native runtime bug.
4. **Original provenance:** outputs/05-volume-flow/REPORT.json was missing and remains missing. This batch's final REPORT.json describes the corrected deliverable; it does not masquerade as the worker's original report.
5. **Content assets:** supply exact matching manga artwork if content acceptance requires it; the runnable layout fixtures already use actual project assets.

The useful finished portion is complete and independently reviewable. Other batches remain outside ownership. No build, installation, app/repository mutation, device interaction, provider/account operation, upload, publication or message to another agent was performed.
