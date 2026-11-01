# Finished Batch 5 mocks

Seven runnable HTML pages are linked from [index.html](index.html). They use the original project artwork, source icon paths and supplied fonts. Original worker outputs and frozen references remain in their original folders and are not modified.

## Run

Use the existing Node runtime; no package installation is needed:

```powershell
node D:\Colosseum-Android-Phone-Mocks-20261101\review-batches\05-tracker-utilities\tools\serve-mocks.mjs
```

Open the loopback URL printed by the server. An optional first argument selects another port. Ctrl+C stops that preview server. Serve from the project root through this script so the relative reference paths work; moving just finished-mocks elsewhere will omit its reference assets and fonts.

## Review controls

The controls outside the app frame change viewport, state, insets, font scale and simulated keyboard. Utility pages also expose long fixtures; Update exposes unknown download size. Tracker pages offer connected/disconnected/attention scenarios, provider selection, review dialogs and long review data. Reload restores each page’s representative data.

The supplied presets are 412×900, 900×412, 354×792, 792×354, 1280×700 desktop, 800×1280 tablet and 1280×800 tablet. Insets are adjustable; 24px top and bottom are illustrative. Screenshots use deviceScaleFactor 1 and logical pixels. No physical device dimensions have been inferred.

## Fixture actions and boundaries

- Tracker decisions, connection states and switches change in-memory fixtures. No account, authentication, import, sync or delivery operation occurs.
- Wallpaper selection and Apply update only the local preview. All six native scenes and all 13 source-named KDE picks are available locally. Search uses eight accurately titled project saga images, not a live Wallhaven result set.
- For add-on URL flows, use `https://fixture.colosseum.invalid/subdl/manifest.json` or `https://fixture.colosseum.invalid/comet/manifest.json`. The former demonstrates nonconfigurable preview/install; the latter setup. `https://torrentio.strem.fun/manifest.json` demonstrates the already-installed fixture. None is contacted.
- The original text menu uses an isolated in-memory clipboard. It does not read or change the computer clipboard.
- Update buttons demonstrate state transitions only; no check, download, restart or installation occurs.
- Collection Save, Cancel, Undo, reorder, notes, creation, membership and deletion are local and reset on reload.
- Parent shell routes, Universe Hall, Tankoyomi configuration and title destinations remain outside Batch 5. Requests are described in the review status outside the app frame. Desktop window chrome is visual context; it does not control a real application window.

## Evidence and implementation handoff

Use [before/after comparisons](comparisons.html), [all capture evidence](evidence.html), [contact sheets](../contact-sheets/index.html), [coverage](../COVERAGE.csv), [findings](../FINDINGS.csv), [final review](../REVIEW.md) and [QML porting notes](../QML-PORTING.md). The machine-readable completion index is [REPORT.json](../REPORT.json).

These artifacts qualify the browser mocks and the inspected source relationships. They do not qualify a native Colosseum build or approve product adoption. No new phone UI exception is proposed. Local Segoe font copies remain in the reference tree for local review only. KDE credits and retrieval provenance are recorded in [assets/ATTRIBUTION.md](assets/ATTRIBUTION.md) and [kde-assets.json](../evidence/kde-assets.json).

The tools folder contains the bounded review scripts. `patch-tracker.mjs` and `write-utilities.mjs` are historical intermediate authoring snapshots, not regeneration commands for this finished revision. The finished HTML/CSS/JS and final hashes are the deliverable.
