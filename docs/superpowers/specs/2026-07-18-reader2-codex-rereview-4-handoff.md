# Codex re-review #4 handoff — reader2 (2026-07-18)

Copy-paste prompt for Hemanth to hand Codex. Answers re-review #3's REQUEST-CHANGES
(items 2 partial, 3 not-resolved, + the addHighlight finding) in commit `87f1b18`.

```
Cross-model RE-review #4 for Colosseum reader2, requested by Agent 2. Your re-review
#3 returned REQUEST-CHANGES on three remaining items. Verify each in commit 87f1b18
(branch agent2/reader2-fresh-book-reader). You are a DIFFERENT model than the
author. Items already RESOLVED across your passes (paper sandbox, FBZ case,
intermediate stale 'ready', dictionary deadline, DNS cache race) need no
re-litigation unless this diff regressed them.

VERIFY EACH — state RESOLVED / NOT-RESOLVED / PARTIAL + one line of evidence:

1. selectionCleared gating (your item 2). qml/reader2/ReaderShell.qml now routes
   'selectionCleared' through L.acceptBookEvent before dismissing — a queued clear
   from the previous book cannot dismiss the current book's menu. (The current
   book's own clear passes: its gen matches and bookReady is true.)

2. Footnote same-href correlation (your item 3). resources/reader2/paper_glue.js
   footnoteTaps is now a per-href FIFO QUEUE (cap 4): 'link' pushes {gen, rect},
   'render' shifts the oldest, normal-link/reject remove one entry, paperOpen
   clears the map. Two rapid taps on anchors sharing one href no longer overwrite
   each other, and a second admitted render emits its own card. NOTE the author's
   scope claim, which you should evaluate rather than take on faith: within one
   href, FIFO pairing is claimed to be the best achievable without patching the
   vendored fork, because the vendor's link→before-render boundary carries no
   per-request identity (before-render detail = {view} only); and the residual
   ambiguity is claimed to be same-note-only and cosmetic (all queued entries for
   an href carry the SAME gen because the map is cleared on every open — so
   cross-book leakage is impossible; the only per-entry difference is the anchor
   rect of the SAME note). On your "vendor chain not cancelled" sub-point, the
   author's analysis: an admission-dropped view is never attached to the DOM, its
   section iframe never fires 'load', so the vendor chain never reaches 'render'
   — the residue is one queue entry, bounded by the cap and the per-open clear.
   If you disagree that this residual is acceptable, say concretely what failure a
   user would see and propose the minimal fix (vendor patch included, if that is
   your recommendation — the team can amend the vendored fork).

3. addHighlight error stamping (your additional finding). paperAddHighlight's
   catch now emits {gen: openGen, ...} so errorDisposition routes it as the
   current open's operational error instead of a later book's failed open. Sweep:
   every 'error' emit in paper_glue.js is now gen-stamped EXCEPT the boot-failure
   one (intentional: pre-book, must surface — confirm you agree).

ALSO: sweep diff 0e0c01b..87f1b18 for new defects (queue push/pop balance in the
link handler's reject/normal-link paths; the FIFO shift in the render handler;
the selectionCleared gate blocking a LEGITIMATE dismiss).

Build + run before verdict (kill any running reader2_harness.exe first; set
QT_FORCE_STDERR_LOGGING=1 for the QML runners and confirm the PRINTED verdicts):
  native\build-target.bat reader2_bridge_harness → native\build-msvc\reader2_bridge_harness.exe
  native\build-target.bat reader2_stores_harness → run it
  native\build-target.bat reader2_autoattach_harness → run it
  set QT_FORCE_STDERR_LOGGING=1
  qml.exe -platform offscreen tests\reader2_logic_harness.qml   (Qt 6.11.1 msvc2022_64)
  qml.exe -platform offscreen tests\reader2_chrome_smoke.qml
  node tests\reader2_paper_text_test.mjs
All must print VERDICT: PASS.

Output: per-item verdicts + evidence, any new findings, then an overall
APPROVE / REQUEST-CHANGES.
```
