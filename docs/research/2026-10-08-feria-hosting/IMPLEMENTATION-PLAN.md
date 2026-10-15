# Feria categories 6–10 implementation plan

Goal: deliver source-backed functional hosting, same-page transfer, exact close and app-scoped reopen prototypes on the reviewed categories 1–5 baseline.

Specification: supplied PROMPT-DOTS.md and ACCEPTANCE.md, packet SHA256 80eff7056ee78702cf35b523bfa37bb312068fc021f6c9421379fc0d677916da. Product baseline cd4b7f6ce8b895239a1e1e02ecb4f0d377a4d413. This packet explicitly authorizes audit and implementation checkpoints; no production adoption.

## Architecture and ownership

SessionStore remains the sole canonical space identity and open/closed descriptor owner. Add exact-identity close transitions and app-scoped reopen inside that existing C++ owner, replacing the deck's transient closedTabs writer. The deck owns live QQuickWebEngineView integration and host projection. QML request objects stay in the originating view context and never become persisted data. All presentation hosts are disposable; they never own canonical identity.

Identity contract: accountEpoch, profileId, spaceId, pageGeneration, documentGeneration, hostId, hostGeneration. Every close attempt additionally has a unique attempt token. Native comparisons reject missing or stale fields. A document change or host move invalidates pending consent; adapters reject any held request before completing that invalidation. Ordinary navigation commands still target explicit space IDs and take fresh identity at dispatch.

## Checkpoints

1. Audit exact Qt 6.11.1 adoption/delegate/close and target Chromium behavior; compare CEF lifecycle patterns and relevant Brave overrides. Record source pins, implementation paths, test assertions, reuse class and incompatible dependencies. Do not adopt the optional draft wholesale.
2. Add failing native tests for exact close, retry, stale identities and two-phase app-scoped reopen. Add build target to the existing focused runtime. Run red on unchanged owners before implementing the native methods.
3. Implement native SessionStore state and geometry policy. Preserve non-browser SessionStore behavior and category 1–5 ownership tests. Run native green and targeted regressions.
4. Add real QtWebEngine failing hosting/close/transfer fixtures. Implement canonical desktop/dialog hosts and docked consent chrome, integrating native transactions. Preserve synchronous original-request adoption and roll back placeholders on failure.
5. Implement same-view Max/Restore with exact geometry/focus restoration and explicit host tokens. Test actual input, DOM/form/history/media identity, repeated transfer, native close and stale callbacks. Adopted-child opener retirement is gated on an evidence-backed lifetime route; do not conceal an unresolved Qt creation-view dependency.
6. Add resource counters and negative control for ownership or exact close. Run complete focused native, accepted foundation, retained and hosting suites against final code. Compare the known retained-download failure to unchanged baseline. Fix the source-manifest pipefail and regression-run gates in the reused CI workflow.
7. Publish exact source commits, audit/contract, builds/artifacts/logs, category result matrix and receiving Windows checklist in Dots-Colosseum. Distinguish source, model/native, real graphical component and full-app evidence. No full-app or Windows claim without execution.

## Native interfaces (frozen for first checkpoint)

- QVariantMap browserIdentity(QString id) const
- QString beginBrowserClose(QVariantMap identity)
- bool settleBrowserClose(QVariantMap identity, QString attempt, bool accepted, QVariantMap descriptor)
- QVariantMap advanceBrowserDocument(QString id)
- QVariantMap setBrowserHost(QVariantMap identity, QString hostId)
- QVariantList closedBrowserRecords(QString provider) const
- QVariantMap prepareBrowserReopen(QString provider) const
- bool commitBrowserReopen(QString recordId, QString newSpaceId)

Identity rejects exact stale account/profile/page/document/host tuples. Rejection settlement occurs only after the renderer's beforeunload rejection acknowledgement barrier. Accepted close snapshots a sanitized ordinary descriptor and retires that exact ID once. Generic teardown close does not manufacture a reopen entry. Reopen peeks without consuming; the deck creates a fresh canonical entry and view, then commits removal of only that matching closed record. Private, invalid and old-account records never reopen.

## Review focus

- A neighbor becomes selected between command and close acknowledgement: only captured identity may retire.
- Navigation or account retirement races a beforeunload response: reject stale consent and clear pending state.
- Native host is destroyed after moving a live view: keep canonical view and original DOM/input intact.
- Failed synchronous adoption or reopen creates an empty placeholder: remove it without consuming reopen history.
- Nested adopted children outlive their original opener: prove delegate/input safety and release; do not treat visual reparenting as ownership proof.

## Explicit pending architecture decision

Qt 6.11.1 keeps adopted delegates' creation-view pointers. A discarded, capped lightweight view shell is a candidate workaround, but its lifetime may equal a dependent child's lifetime. User ruling requested before implementing that exception to the no-indefinite-lease constraint. Independent close/reopen and ordinary host transfer proceed meanwhile.
