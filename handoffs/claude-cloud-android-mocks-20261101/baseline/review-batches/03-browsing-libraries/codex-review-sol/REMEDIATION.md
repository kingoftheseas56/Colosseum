# Search candidate correction

**Corrected candidate ready for primary review.** SOL-B3-01 and SOL-B3-02 are fixed in the owned candidate only; the initial receiving report and Pro mocks remain unchanged.

Open [candidate/search.html](candidate/search.html?capture=1&preset=portrait&lane=Biblio&state=empty). Use `lane=Theatre` or `lane=Tankoban`; use `preset=landscape` for 900×412. The entry uses the original mock directory as its base for unchanged styles/assets/runtime and loads [the complete corrected app.js](candidate/app.js). Keep this folder within the existing batch. [Reviewable patch](search-correction.patch).

The input now always renders its query state; suggestions/results no longer erase one-character drafts. The focused rerender helper retains the field's caret/range, or preserves an intentionally selected existing result/control instead. Clear cancels pending work and returns focus as the source does. No CSS or visual design changes were made.

Frozen `BiblioSearch.qml:190,202` and `SearchSurface.qml:218,280` confirm Enter's search/commit/openTop intent. The candidate invokes the existing local `item-open` boundary for a matching top result; no match emits no action. **Field focus retention after Enter is fixture-only:** this demo reports an intent and stays on Search. Native navigation must follow its real owner and must not force focus back into a hidden Search field.

## Focused verification

**72/72 checks passed on the preceding candidate** (`2b956240…`): 12 cases × Biblio/Theatre/Tankoban × portrait/landscape. Actual browser keyboard/pointer events cover slow and fast typing, first-character pause, Enter then further typing, Backspace/clear, middle-of-query caret edits, selection-range retention, intentional focus moves during pending debounce, and no-match submission. Six screenshots of that preceding candidate retain original presentation and typography. The subsequent control-identity correction does not change rendered styling. [Raw results and focus/resource observations](remediation-test-results.json), [contact sheet](corrected-search-contact-sheet.jpg).

Run the bounded regression with `node test-search-correction.cjs` from this folder. It serves only the existing local artifact tree, blocks external requests, owns an isolated headless browser/context and closes both browser and server. It does not edit mocks. `node --check candidate/app.js` also passed. Page errors in both focused runs: zero; required-resource failures: zero (the implicit favicon 404 is separately recorded).

Visual review caught a UTF-8 copying error in the first local candidate. The copy step was corrected to explicit UTF-8; the final 72 checks and six screenshots were rerun. That UTF-8 correction preceded the later control-identity fix. The earlier interaction result is retained under `remediation-test-results-before-utf8-correction.json` and is not final acceptance evidence.

## Exact versions and preservation

- original: `175b0d07ba5d5b4bd3024f1591c37b2896139f0cb1ad9dbcc48d6c9ca37af0fb`
- candidate: `5f3adf984432ea9a0fde19c7a8b6050c93e5b4682ed8d805f39235cba32e27f4`
- entry: `25ef1819f1a2daad96c312fc47d2715fd88fab63bf1f1464faa3ca4b5b80704d`
- patch: `2a6010a0dbc2c6ae95ee75eb324731e577ca75fc5826c30fa5dde45a7bac0ca3`
- driver: `267fd38b5e3b728c339ae7344d66dec4fcdc1fda72f37728f5240360062504a5`
- focusDriver: `67aff541b8ef2bf89806368f777e771d0e97ef3fd4554c352d8ee11185b9bdfe`

All 47 Pro finished-artifact hashes and 78 original-worker input hashes still match their recorded manifests. This includes the unchanged original Search JS. Candidate, entry, patch and driver hashes plus exact paths are in [REMEDIATION.json](REMEDIATION.json).

Previous 72-case browser/context and loopback server closed at 2026-11-01T00:50:57.028Z. No source QML, application repository, other page, Git, device, account, provider or publication changes. Native metrics, keyboard/IME, actual navigation and service behavior remain unqualified; primary owns acceptance.

## Primary receiving correction: exact desktop control focus

**REM-F01 fixed in the latest candidate.** At 1280×700, both pointer and keyboard focus on Fullscreen or Close moved to Minimize after pending debounce: 4 failures reproduced in [before evidence](focus-identity-before.json). All three controls share `data-action=noop` and no value. The helper now matches `aria-label` as well as action/value, preserving the exact existing control.

**Latest candidate: 9/9 focused checks passed.** Minimize, Fullscreen and Close each retain exact identity with pointer and keyboard focus; result focus and Clear focus remain intentional; first-character pause, slow complete typing and Enter-followed-by-typing smoke pass. This bounded retest is [focus-identity-after.json](focus-identity-after.json), with [desktop screenshot](focus-identity-after.png). The earlier 72-case suite was not repeated after this two-expression identity refinement. Native navigation/focus boundaries above still apply.

Run `node test-focus-identity.cjs after` for this focused regression. Final candidate hashes above and in REMEDIATION.json supersede the prior candidate hash. Latest owned browser/context and loopback server closed at 2026-11-01T00:54:47.746Z. Pro's 47 finished artifacts and 78 original-worker inputs remain hash-identical.
