# Batch 2 — assets, versions and evidence provenance

## Authority

The original INPUTS.json declares desktop commit a40f66be06f22cdbe75e97e7dc4f1108ab2f5d41, alpha recovery 77844eef0bd3b7fee2c1f884be4a76db6d63321d and rejected Android drift 9e896bfd5dac40ef3ceb0e26e166e85ed8beecd9. The approved desktop archive SHA-256 is 3e6becc75ac3c9a8045ae52763d66ece8dbd9181617fa9c6351ee6e5cecd2ff0. These are declared frozen-source pins, not a claim that this directory is a live checkout at those commits.

input-versions-start.json and input-versions-end.json independently bind the inspected original worker/QML/context files by SHA-256 and modification time. preservation-check.json compares them. source-versions.json includes additional inspected source components and loaded approved resources. finished-versions.json hashes the finished runtime files. finished-lineage.json records the initial copy and relative-asset-path adjustment.

Original output and reference files were read only. New and modified files are confined to review-batches/02-detail-pages. The original README and INPUTS.json retain their original status and review-only wording as historical inputs; the user's later scope update authorized corrected files under finished-mocks.

## Actual local artwork and typography

| Group | Retained actual project artwork | Limitation |
|---|---|---|
| Biblio | extensions/house/cosmere.jpg | This is a project image, not The Way of Kings retail jacket. |
| Theatre film | wallpaper/sagas/harry-potter.jpg | Saga art substitutes for the precise film poster/backdrop. |
| Theatre series | universes/one-piece/new-world/banners/wano-country.png and East Blue arc banners | These are project arc images, not an episode-by-episode remote still catalogue. Cast portraits are local initial placeholders. |
| Comics | DCAU Gotham/future-Gotham/Metropolis/Watchtower portal and environment images; DC/Marvel marks | Existing project art substitutes for individual issue/trade jackets. |
| Saga | Harry Potter wallpaper and bundled sibling saga artwork | The same source art stands in for separate book/film jackets and gallery frames. |
| Shelves | James Bond and Shōnen Jump marks, cold-ripple wallpaper, bundled sibling marks | Individual Bond/manga covers are unavailable in the frozen input. |

Fonts load locally from reference/local-fonts (Segoe regular/semibold/bold) and reference/desktop/assets/fonts (Fraunces and Switzer variants). Existing project icons are used where the original component uses assets; source inline SVG/text controls remain inline. No generated image, fetched replacement poster, font CDN or provider artwork was added.

See loaded-resources.json and source-versions.json for exact paths and hashes. Normal-page font faces that are not used until a modal opens may correctly show “unloaded”; the shared picker font faces were checked after opening and are loaded.

## Capture method

One isolated, owned headless Chrome session rendered local HTML through an owned loopback-only server. Browser version: Chrome/156.0.8078.74. Profile: browser-profile-20261101. The harness blocks requests outside its own origin/data/blob. It does not use shared browser tabs, mouse/keyboard focus, device tools or app services.

Every screenshot has adjacent JSON with URL, input, timestamp, viewport, browser version and image SHA-256. browser-events.jsonl preserves commands and observations; loaded-resources.json records resource versions and error history. Screenshot dimensions are actual image dimensions. Biblio is cropped to #app-screen from a 1450×1500 workbench; the adjacent viewport metadata describes the outer browser, while verified-layouts.json records its exact inner frame. Other app captures fill their browser viewport.

Contact sheets are deterministic Pillow composites of those PNGs. They introduce no generated or retouched app content. Their labels identify the original worker, finished HTML, source-geometry attempt or pending proposal, plus dimensions and source capture. Full PNG and metadata links are available in the comparison pages.

## Evidence authority and superseded attempts

verified-layouts.json is the consolidated final normal-layout matrix: 56 distinct page/size combinations. It takes Theatre from handoff-theatre-layouts.json and Comics from handoff-comics-layouts.json after the final ChromeScrim, source-scroll and unused-asset-reference corrections; Biblio from biblio-final-layout-retest.json after the tablet-host correction; Saga/Shelves from universes-final-layout-retest.json after the restored Continue controls. The earlier complete matrix is retained as a checkpoint, including the Biblio tablet defect that it exposed.

The earlier series jump failed and was corrected; theatre-final-jump-retest.json supersedes that failed state. The final archive selector test first looked for literal “All releases”, but the real label includes the release count; final-regression-archive.json retests the real data-action and verifies DC Comics identity. The first Biblio tablet popup metric selected a hidden legacy dialog; biblio-tablet-popup-retest.json targets the visible .cp-dialog and passes. The harness does not accept a combined “Shift+F10” key string; the real ContextMenu key was used for all ten Continue-menu checks, and the Shift+F10 handler was separately dispatched as a DOM keyboard event.

A temporary Universe JavaScript syntax error during a patch was fixed before final verification. The harness keeps a cumulative error array, so later capture metadata still contains that historical error. It is not a repeated error on every screenshot. Final page loads, interaction ledgers, runtime syntax checks and the final resource check distinguish it from new failures. Failed attempts remain in the evidence rather than being silently deleted.

The first final source-queue assertion searched for the literal word “Queued”. The source edition row uses an ellipsis indicator and suppresses alternate-source actions while busy. comic-source-queue-retest.json verifies that actual presentation, focus return and visibility through the real edition-row entry point in both narrow phone orientations and desktop; the earlier text assertion is not a remaining product failure.

Some early Biblio stress capture parameters were ineffective because that worker uses width/height/scale/insetTop/insetBottom. Correctly parameterized captures and ledgers supersede them. Use the final verified matrix and named verified stress ledgers rather than inferring pass status from any historical PNG alone.

## Reference gaps and limits

Original outputs/19-generic-universes/REPORT.json is absent. Its missing handoff remains B2-019; the available HTML, FIDELITY.md and approved QML were still independently reviewed.

An HTML desktop rendering is a mock comparison to source, not independently rendered approved app output. No native QML baseline, Android emulator/device measurement or runtime qualification was performed. Phone dimensions and insets are provisional. The fixture catalogues, progress, provider rows and dates are frozen illustrations, not claims about current services.

The corrected pages are runnable local artifacts. Routes, playback, reading, library changes, collections and source outcomes exercise page-local fixtures or emit an external action log. They do not prove actual app navigation, persistence, provider correctness, downloads or linked-account operations. The source's unconnected Universe Continue Remove signal is retained and documented in QML-PORTING-NOTES.md.

## Final comparison clarification

The decision contact sheets now use actual first-tab actions, with 24 verified landing cases across Saga, Bond shelves and magazine shelves. Earlier identical-scroll-position captures remain available; they can show a heading passing behind sticky chrome and do not represent a tab landing. The two Theatre synopsis comparisons now show the paragraph at matched 354x792, 1.3 text and 24px insets, proving the original hidden three-line clamp versus complete final copy. Theatre-synopsis-visible-comparisons.json records both states. Contact-sheet panels label their capture dimensions; some key-fix transitions intentionally show different states, as named.
