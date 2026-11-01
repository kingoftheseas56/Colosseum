# Batch 01: Sources and manga controls

Status: organized only; review has not started. 9 required HTML pages.

## Independent assignment

This batch can be reviewed and handed off on its own. No other review batch must finish first. Work on this batch only, using the local inputs in INPUTS.json. Use existing computer access for local artifact inspection and isolated rendering. Aim for a useful review pack; stop and save a handoff within three hours, even if checks remain. Checkpoint COVERAGE.csv and findings as work proceeds so an interrupted session still leaves useful evidence.

The scope is reviewing HTML mocks, not correcting them or implementing app changes. Existing worker output, frozen references and other batches are read-only. Write review artifacts only in this directory. No app/repository edits, builds, installation, ADB/device interaction, account/provider operations, uploads, publication or messages to other agents. The beta remains held. Do not start another batch automatically.

## Design authority and review rules

- Preserve Colosseum's approved visual design, original information and actions. Layout fitting may rearrange, wrap, size and scroll the existing elements. New phone surfaces, replacement controls, typography or visual vocabulary require an explicit finding and decision.
- Review portrait and landscape for every page; check long content and the existing open controls relevant to that page. Group repeated component findings, but record each page's coverage separately.
- Behaviour alternatives require the original attempt and proposed alternative, with the concrete obstacle explained. A proposed exception is not approval.
- Approved frozen desktop source: ../../reference/desktop at a40f66be06f22cdbe75e97e7dc4f1108ab2f5d41. Alpha recovery: ../../reference/alpha at 77844eef0bd3b7fee2c1f884be4a76db6d63321d. Rejected Android comparison: ../../reference/android-drift at 9e896bfd5dac40ef3ceb0e26e166e85ed8beecd9; it is not the desired design.
- Read the applicable page QML and necessary shared components listed in INPUTS.json, plus ../../context/ANDROID-PORT-CLASSIFICATION.md and ../../context/VIEWPORTS.json. Some helper components exist only in the rejected branch; inspect the approved parent's inline presentation rather than treating the helper as authority.
- Phone dimensions and insets are provisional. Historical captures are dated component references. An HTML desktop rendering is a mock, not independently verified approved app output. Label evidence accurately.

## Inputs

Worker reports and HTML existence do not prove fidelity. A missing or malformed handoff stays explicitly pending; it need not block checking the available pages. Reuse existing captures where they show the relevant version. Generate only missing or unusable evidence, using isolated browser contexts, local assets and owned profiles. Do not take shared mouse/keyboard focus or touch existing user tabs. If rendering fails, record the gap rather than repairing tools indefinitely.

| Worker input | HTML pages | Handoff evidence |
|---|---|---|
| 01-theatre-sources | [film-sources.html](../../outputs/01-theatre-sources/film-sources.html), [episode-sources.html](../../outputs/01-theatre-sources/episode-sources.html) | [REPORT.json](../../outputs/01-theatre-sources/REPORT.json), [FIDELITY.md](../../outputs/01-theatre-sources/FIDELITY.md) |
| 02-comic-sources | [comic-sources.html](../../outputs/02-comic-sources/comic-sources.html), [comic-source-confirmation.html](../../outputs/02-comic-sources/comic-source-confirmation.html) | [REPORT.json](../../outputs/02-comic-sources/REPORT.json), [FIDELITY.md](../../outputs/02-comic-sources/FIDELITY.md) |
| 03-manga-sources | [manga-volume-sources.html](../../outputs/03-manga-sources/manga-volume-sources.html) | [REPORT.json](../../outputs/03-manga-sources/REPORT.json), [FIDELITY.md](../../outputs/03-manga-sources/FIDELITY.md) |
| 04-manga-series | [manga-series.html](../../outputs/04-manga-series/manga-series.html), [manga-chapters.html](../../outputs/04-manga-series/manga-chapters.html) | [REPORT.json](../../outputs/04-manga-series/REPORT.json), [FIDELITY.md](../../outputs/04-manga-series/FIDELITY.md) |
| 05-volume-flow | [volume-flow-original-fit.html](../../outputs/05-volume-flow/volume-flow-original-fit.html), [volume-flow-minimal-alternative.html](../../outputs/05-volume-flow/volume-flow-minimal-alternative.html) | [REPORT.json](../../outputs/05-volume-flow/REPORT.json), [FIDELITY.md](../../outputs/05-volume-flow/FIDELITY.md) |

The complete source file lists, exact output paths and source pins are in INPUTS.json. Input captures live under each original output directory. Record the reviewed input revision/hashes; changed inputs require rechecking the affected evidence.

## Required saved handoff

1. REVIEW.md: concise findings, pages covered, evidence limitations, unresolved decisions and the next useful actions. Distinguish observed symptoms from inferred causes. Do not claim app acceptance or runtime qualification.
2. COVERAGE.csv: update every page's portrait, landscape, desktop-reference, fidelity, controls and long-content checks. Use explicit checked, issue, unavailable or not-reviewed states with evidence links. Never count an unchecked page as passing.
3. FINDINGS.csv: stable finding IDs, affected pages, severity/category, observed issue, source reference, evidence paths and recommended next action. Include shared findings once and name all affected pages.
4. contact-sheets/: portrait/landscape pairs, with source/reference comparisons where available. Clearly label original-versus-proposed behaviour and the provenance of every reference.
5. REPORT.json last: batch, status (ready-for-review or partial), reviewed input versions, coverage counts, artifact paths, unresolved items and remaining work.

Do not wait for all five batches to prepare this handoff. A completed portion should remain useful if the session or message allowance ends. Leave missing checks pending, with enough detail to resume without repeating finished work. Final product decisions and acceptance remain with Hemanth and Codex.
