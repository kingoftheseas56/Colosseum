# BR-EPUB-DEFAULTS — Phone-specific EPUB defaults were removed; evaluate narrow-viewport defaults without migrating saved choices

Class C; priority P2. Authorization/order comes from START.md, not classification alone.

## Why this matters
Different first-run/unspecified typography reduces available vertical room and changes paragraph spacing/justification on narrow effective viewports. This is a source-defined default difference, not proof of unreadable text or exact horizontal width loss.

## Alpha behavior
On Android, defaultMarginPx returns24 and defaultJustify false; persisted defaults/book patch still win.

## Beta behavior
Unspecified margin falls back72 and justification true on Android as on desktop; actual rendering uses top/bottom margins plus gapPct based on WebView innerWidth.

## Actual owner/caller
Main5445 ReaderShell->ReaderPaper49 AndroidPaper->AndroidEbookRenderer::setAppearance162->AndroidEbookHost.command200->android_boot.js34-43->paper_glue paperSetAppearance.

## Donor evidence
qml/reader2/Reader2Logic.js:496-516 defaultMarginPx/defaultJustify/appearanceDefaults; :645 appearanceToPaper

## Beta evidence
qml/reader2/Reader2Logic.js:498-504 appearanceDefaults; :633-634 appearanceToPaper; :669-697 appearanceStore/effectiveAppearance; resources/reader2/paper_glue.js:452-457

## Evidence layer
committed-alpha-beta

## Dependencies
Reader2Logic appearance normalization/store precedence; shared Foliate renderer; actual native WebView viewport at installed density1.15/default or1.20 launch.

## Reuse/adaptation
adaptation

## Files and integration points
Reader2Logic appearanceDefaults/appearanceToPaper; ReaderShell passes effective saved appearance unchanged. If viewport-dependent fallback introduced, derive from actual reader surface, not desktopPresentation.

## Preserve
Reader2Bridge profile stores, generation stamping, source origin/CSP/publication script guard, font load safeguards; saved defaults and per-book sparse patches; desktop defaults and layout.

## Bounded unit
One appearance fallback policy plus normalization/migration-precedence regression tests, previewed in actual renderer; no schema migration.

## Risks
Preservation constraints: Reader2Bridge profile stores, generation stamping, source origin/CSP/publication script guard, font load safeguards; saved defaults and per-book sparse patches; desktop defaults and layout. Dependencies: Reader2Logic appearance normalization/store precedence; shared Foliate renderer; actual native WebView viewport at installed density1.15/default or1.20 launch.

## Recommended change
Treat as a product-preference adaptation: consider narrower Android EPUB viewport defaults from Alpha after preview, keeping existing explicitly saved defaults/per-book settings. Do not force phone layout or rewrite user settings.

## Existing tests
tests/reader2_logic_harness.qml; tests/reader2_paper_text_test.mjs; tests/android_reader_fonts_test.mjs; tests/qml/tst_android_beta_reader_touch.qml covers overlay/IME but not this typography default.

## Required host checks
Fresh/no-reader2, legacy-flat, saved default and per-book override cases; Android narrow/wide viewport fallback; desktop unchanged; fixed-layout content ignores reflow settings. Assert top/bottom and computed2..18% gap against actual innerWidth, not2*margin approximation.

## Local device acceptance (pending)
OnePlus11R portrait+landscape at both relevant density launch settings; inspect actual EPUB WebView viewport and text, page turn/resume after settings changes; NokiaT20 separate. Compare fresh test profile and saved24/72 preferences.

## Recovery
Revert fallback code and upgrade same package without uninstall/clear-data; preserve settings.json and progress. Defaults-only change must not write migration, so saved preferences survive rollback.

## Evidence confidence
high source-delta confidence; device behavior unverified

## Unresolved
Whether Hemanth prefers Alpha typography defaults at Beta effective viewport needs a concrete preview. No measured reader viewport or rendered page image gathered here.

## Execution checklist
- [ ] Inspect the referenced production functions/callers and donor contract.
- [ ] Reproduce the specified gap with the smallest actual-owner fixture or deterministic production-function check; record command and result.
- [ ] Implement the bounded adaptation in the identified owners, preserving the stated invariants.
- [ ] Run the listed applicable regressions plus missing host cases; test desktop invariants and relevant stale/cancel/error paths.
- [ ] Review exact changed-file diff, update progress/evidence, commit explicit owned files and push.
- [ ] Leave physical acceptance pending and continue the next authorized unit.
