# Finished Batch 4 mocks

Open **index.html** in a desktop browser. It links all 13 finished pages, their original worker pages, live before/after comparisons, fixture states, source QML and evidence.

No build, installation, account or network connection is needed. Keep this folder in the supplied project tree:
`review-batches/04-accounts-configuration/finished-mocks/`.
The relative artwork/font paths resolve to the existing `reference/` folder at the project root. If moving the deliverable, move the project folder with its reference assets and original outputs; this folder by itself is not an asset bundle.

Each page has a workbench outside the app frame for viewport and fixture controls. Capture URLs hide that workbench. In-frame actions use local fixtures and record request intentions; no live account, provider, clipboard, browser-launch or install action runs.

The five task subfolders retain the original page filenames. Account Centre intentionally composes the corrected shared `12-account-forms/app.js` and stylesheet. Keep those sibling folders together.

The original worker outputs under `../../../../outputs/` and frozen reference sources remain unchanged. The original worker 11 REPORT.json is malformed; see the batch review instead of relying on that historical handoff.

See [keyboard-decision.html](keyboard-decision.html) for the unapproved short-landscape IME alternative, [../QML-PORTING-NOTES.md](../QML-PORTING-NOTES.md) for implementation guidance, and [../REVIEW.md](../REVIEW.md) / [../REPORT.json](../REPORT.json) for final coverage and limits.
