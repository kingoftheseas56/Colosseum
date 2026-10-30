# Non-Feria 1.1.9 acceptance implementation

## Goal and source

Implement repeatable Lanista acceptance journeys and platform-scoped evidence for the 1.1.9 release notes, starting at `a6104f7a8d9aab9766e71f031b7908beaeca85ad` in `deliveringsake-rgb/Colosseum`. Branch: `dots/119-lanista-acceptance-20261009`.

## Architecture

Retain the native Lanista JSON protocol, scenario evaluator and disposable `session run` process owner. A thin Python entry point selects explicit cases, binds fixture paths, checks executable/QML provenance, and adjudicates existing protocol/timing/session evidence. It does not introduce an alternative driver. Every ordinary command must assert a successful reply. Missing prerequisites remain BLOCKED/NOT RUN; abnormal shutdown is FAIL even if native scenario assertions passed.

Use deterministic generated CBZ/PDF/playable media and disposable extension data. Reuse focused native updater/account/tracker/Watch Party tests. Keep a separate PowerShell Windows receiving runner for installer identity, synthetic-state migration and relaunch. Authentic higher-version signed automatic-update evidence is a receiving prerequisite, not simulated success.

## Work and verification order

1. Verify exact commit/tree and local instructions; inspect native commands and live selectors. Reuse verified Qt 6.11.1 SDK/dependencies in a fresh Release build.
2. Test the thin runner's failure classification first: empty selection, missing replies, identity mismatch, failed assertions, crashes/forced kill, missing evidence and restart ownership must never pass.
3. Author bounded shell, Universes/franchises/atlas, Extensions, Vault, comic/PDF and account journeys. Add only stable object names/read-only observations where necessary, preserving behavior.
4. Generate and validate real fixtures. Seeded browsing, native file-dialog entry and actual playback remain distinct checks.
5. Run focused component/security tests and the assembled Linux app through unique profiles/pipes on the cloud desktop. Capture valid ping/get-state, visible rendering, controls, restart persistence and normal exit. Execute representative wrong-identity/page, missing-asset and tampered-update negative controls.
6. Package the fresh Linux build where supported; verify normal AppRun startup without QML override separately from Lanista's explicit-QML runs.
7. Review diff, record exact source/artifact hashes and PASS/FAIL/BLOCKED/NOT RUN matrix, publish tested source on the task branch and a concise Dots-Colosseum evidence index.

## Review focus and limits

- No production adoption, release/feed/website changes, main-account Colosseum access or user-machine access.
- No private accounts, signing secrets or personal libraries.
- No broad historic scenario rewrite; selected assertions remain meaningful.
- No mutable-property-only navigation claims, stale binary evidence or forced-kill-as-success.
- Linux assembled evidence, native/component tests and unexecuted Windows receiving work are separate.
- Research-only release-note entries are documentation scope, not shipped integration claims.
