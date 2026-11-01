# B2-018 — Universe tabs on narrow phones

**Owner of decision: Hemanth. Status: pending.** Both the original attempt and the proposed alternative are runnable. The rest of Batch 2 is completed independently.

## Original source attempt and concrete obstacle

SagaUniversePage.qml's inline tabBar around lines 1087–1133 and ShelvesUniversePage.qml's inline tabBar place the natural-width docked bar beside Back. The finished workbench's Original option reproduces that source geometry in HTML: x = content gutter + 118, docked y = 22 + top inset. This is a fresh HTML source-geometry attempt, not a native QML capture.

At 1.3 text and illustrative 24px top/bottom insets:

| Page | Natural bar x | Natural bar width | Right edge | 354px / 412px portrait |
|---|---:|---:|---:|---|
| Saga | 142px | 485.25px | 627.25px | Later tabs extend beyond both frames |
| Shelves | 142px | 306.69px | 448.69px | Bar extends beyond both frames |

At 792×354 and 900×412 the same beside-Back arrangement fits. The source attempt therefore works in the reviewed landscapes; the concrete obstacle is narrow portrait width.

## Proposed alternative

Keep the original position whenever the natural bar fits the usable width. Otherwise put the same bar below Back at y = 74 + top inset. Keep the original 36px pills inside a 46px track; use horizontal scrolling for the natural-width labels. Selection and keyboard focus reveal the active tab. Account for the resulting chrome floor when jumping to a section.

There are no replacement tabs, new icons, truncated label vocabulary or reduced font sizes. Desktop presentation retains the original beside-Back position.

## How to compare

Open [the comparison page](comparisons/decision-B2-018.html) for eight matched pairs: both pages at four phone sizes, with the same scale and insets, after activating the same first tab. Each mode uses its own landing offset so its heading remains below its chrome. Contact sheets: [Saga](contact-sheets/saga-tab-decision.png) and [Shelves](contact-sheets/shelves-tab-decision.png).

In either finished workbench use **Phone tab docking — decision pending**. Query parameters are `tabs=original` and `tabs=proposed`. The proposed mode is the clearly labeled default so the finished narrow-phone pages remain usable while the original attempt stays one selection away.

Final action-based evidence: universe-tab-landing-verified.json (Saga and magazine shelves, 16 cases) and shelves-bond-tab-landing-verified.json (Bond shelves, 8 cases). All 24 cases land headings below the tab bar; all proposed bars remain contained. Earlier fixed-scroll evidence remains in saga-finished-stress.json and shelves-finished-stress.json. A fixed scroll position may naturally place content behind sticky chrome; it is not the tab-action landing. Captures are named `saga-tabs-{original|proposed}-*-stress.png` and `shelves-tabs-{original|proposed}-*-stress.png`; each has timestamp, viewport, URL and image SHA-256 metadata.

## Exact remaining decision

Choose whether to adopt this conditional docking in native QML, retain the overflowing original geometry, or specify another behavior. No app change or approval is implied by the proposed HTML. All other verified fitting fixes and their source mapping can be used independently.
