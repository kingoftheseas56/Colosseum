# Feria capabilities 16–20: bounded source/API audit

**Audit complete; no prototype implementation, product build, product test or platform qualification.**

Application baseline: [62057bfbdaa2305a05d2d3fb02b870bc9e826638](https://github.com/kingoftheseas56/Colosseum/tree/62057bfbdaa2305a05d2d3fb02b870bc9e826638). Audit branch: `dots/feria-capabilities-16-20-audit`. This folder contains audit/evidence artifacts only. The proposed engine contract was treated as requirements, not implementation proof.

## Bottom line

Keep the current app-owned pause policy, one download receiver per profile and FeriaAccountStore history/grants. Use supported Qt public mechanisms and extend narrow owner boundaries. No Chromium/CEF/Qt component is selected for transplantation, no new store is proposed, and adding CEF as an engine is not recommended or authorized.

The important gaps are:
- Accurate observable pause/resume coverage and document/frame retirement, rather than universal media-control claims.
- Captured download provenance and explicit destination/admission policy, distinct from history-writing authorization.
- Safe completed-file Open/reveal and truthful native/wrapper action states.
- Private transfers retained beyond their last source space when valid; account retirement remains an explicit separate policy.
- Typed metadata failures and a settled completion snapshot: Qt can emit Completed before final byte properties, while current once-only history writing reads them immediately.
- Native Save integration with context retirement and explicit-path autoaccept handled correctly. Public Qt lacks exact original-frame Save fidelity and arbitrary QML download(url).

These are source-derived findings, not reproduced runtime failures. Existing auto-save to Qt's default destination is an implemented policy, not automatically a defect because no chooser appears.

## Coverage and reading order

Exactly five original categories and three families, **15 comparison cells**:

16. Pause on app minimize/world-away; resume only media paused by Colosseum.
17. Download request ownership and admission.
18. Download progress/pause/resume/cancel/open.
19. Download persistence and private cleanup.
20. Save page/link/image/media through the native download pipeline.

1. [Current implementation](colosseum/EXISTING-IMPLEMENTATION.md): actual owners, implemented/partial/missing boundaries, tests inspected and canonical file-owner check.
2. [Pins and compatibility](donors/PINS.md): exact revisions, verified relationships and discrepancies.
3. [Fifteen-cell comparison](COMPARISON.md) and [full machine-readable cells](COMPARISON-CELLS.json).
4. [Selected/rejected choices](DECISIONS.md): correctness, targeting, lifetime, privacy, concurrency, retry, dependencies, cost and portability.
5. [Proposed interface](PROPOSED-INTERFACE.md): clearly separated from existing methods and unsupported capabilities.
6. [Acceptance/negative-control fixtures](ACCEPTANCE-FIXTURES.md): plans for a separately authorized prototype phase; none executed here.
7. [Independent evidence review](evidence/INDEPENDENT-REVIEW.md) and [artifact checks](CHECKS.json).

Detailed columns: [Chromium](donors/chromium/REPORT.md), [CEF](donors/cef/REPORT.md), [QtWebEngine](donors/qtwebengine/REPORT.md). Each has exactly five structured cells, actual source/test symbols, exact revisions, limitations and source provenance.

## Exact donor pins

| Role | Revision |
|---|---|
| QtWebEngine6.11.1 | `eb0793cc4b76e93cf669f586fd68c76019f40ec9` |
| Actual Qt Chromium gitlink | `58c11ad487f8a237cf0ac71cc3e818b52db150df` |
| Upstream Chromium140.0.7339.264 comparison | `56ca847e9ea70a5c56fa3d634361da1002fb284b` |
| CEF7339 | `49f5d095ac0c84b2da64d06fec866f22531b331d` |
| CEF Chromium140.0.7339.214 dependency | `754bf56217050dfc97f626926b8dbc6f22b12634` |

Qt declares .264 but its import/VERSION remains .225 with Qt patches; CEF targets .214. Security backports through Chromium148 do not confer148 APIs. This is a compatible-family source comparison, not identical source trees or ABI compatibility. Exact Qt vendor files govern receiving-engine findings. Chrome-only files absent at inspected vendor paths remain upstream-reference-only. Shared Chromium implementation is not independent three-way corroboration.

## Evidence versus execution

Performed: immutable source/vendor-document retrieval, scoped implementation/caller/test reading, revision/line/hash verification, JSON/cell coverage checks and independent source review. Source manifests retain successful reads and honest blocked/nonexistent candidate paths. A file downloaded to cache is not a claim every unrelated line was audited.

Not performed: product builds, product tests, runtime/GUI/provider checks, Windows qualification, WebView2 parity, engine rebuilding, production implementation, master merge or release. Cloud Qt was not used; narrow discovery found a qtchooser wrapper without its target qmake, not a usable verified Qt runtime. Source-test limitations (including Windows skips, permissive autoplay and misleading asynchronous test names) are retained rather than reported as passes.

Manifest paths under `source/` identify exact remote source URLs and cached-file hashes. Full third-party/application source caches are not duplicated here. The selected-file vendor/upstream diff and license notices are intentional comparison evidence, not an application port. Saved release/dependency metadata under donors/metadata preserves pin-resolution evidence.

## Remaining gates

Resolve product auto-save/ask/overwrite policy; qualify delayed QML request lifetime, originless/stale-view requests, settled final metadata, interrupted-cancel reconciliation, private transfer leases and stale Save context. Preserve unsupported frame/media capabilities visibly. Reconcile the separate WebView2 source mappings before any implementation; their local handbacks were not presumed accessible here. Existing UI contract remains app-specific and outside website content.

This assignment ends with the audit publication. Prototype implementation requires its separate prompt.
