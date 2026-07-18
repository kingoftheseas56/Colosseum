# Codex re-review #2 handoff — reader2 (2026-07-18)

Copy-paste prompt for Hemanth to hand Codex. Answers Codex's second REQUEST-CHANGES
(2 NOT-RESOLVED, 1 PARTIAL, 1 regression) in commit `008b408`.

```
Cross-model RE-review #2 for Colosseum reader2, requested by Agent 2. Your second
review returned REQUEST-CHANGES. Verify each item is now resolved in commit 008b408
(branch agent2/reader2-fresh-book-reader). You are a DIFFERENT model than the author.

VERIFY EACH — state RESOLVED / NOT-RESOLVED / PARTIAL + one line of evidence:

1. Cross-book async safety (was NOT-RESOLVED). Your finding: QML adopted currentGen
   only on 'ready', so book A's event arriving after openBook(B) but before B's
   'ready' passed the strictly-older gen check (gen == currentGen) and was accepted —
   saved under B. Check: qml/reader2/Reader2Logic.js acceptBookEvent (bookReady AND
   not-stale) now gates relocated/footnote/searchResults/selection/highlightTapped in
   qml/reader2/ReaderShell.qml; errorDisposition routes 'error' (gen > currentGen =
   open-fail + gen adopt; gen == currentGen + ready = operational; else drop). Race
   cases are in tests/reader2_logic_harness.qml sections 3c/3d. Also: footnote capture
   in resources/reader2/paper_glue.js is now two-slot staged (latestFootnoteTap →
   activeFootnoteTap at before-render admission) so a second tap's 'link' can no
   longer relabel the first tap's in-flight render.

2. Paper sandbox (was NOT-RESOLVED). Your finding: setAuthorizedBook was Q_INVOKABLE
   on the same QWebChannel object exposed to the untrusted paper — it could authorize
   an arbitrary path then call filesRead. Check: Paper.qml now registers ONLY
   Reader2Bridge.paperGate (class Reader2PaperGate, native/reader2/Reader2Bridge.h/.cpp)
   whose entire surface is filesRead + paperEvent; setAuthorizedBook, ALL store
   methods, and dictLookup are unreachable from the paper. The surface is enforced by
   a metaobject enumeration contract in tests/reader2_bridge_harness.cpp section (e)
   that fails if the gate ever widens.

3. Dictionary DNS bound (was PARTIAL). Your finding: the 8s HTTP timer started only
   after DNS completed; stalled DNS had no deadline. Check: Reader2Bridge.cpp
   dictLookup now arms a QTimer::singleShot(kDictTimeoutMs) deadline sharing a done
   flag with the QHostInfo callback — stalled DNS emits dictResult(word,"",false) at
   8s; a late DNS result still caches the IPv4 for the next lookup but never
   double-emits.

4. FBZ case regression. Your finding: inner .FB2 lookup inside FBZ was
   case-sensitive. Check: paper_glue.js makeBook now matches
   e.filename.toLowerCase().endsWith('.fb2').

ALSO: sweep the diff 6b47180..008b408 for any NEW defect these fixes introduced
(double-emit paths, a legitimate event now wrongly dropped by the bookReady gate —
e.g. first-open flows, failed-open error surfacing, same-book reopen).

Build + run before verdict (kill any running reader2_harness.exe first — the link
fails on a locked exe):
  native\build-target.bat reader2_bridge_harness   → native\build-msvc\reader2_bridge_harness.exe
  native\build-target.bat reader2_stores_harness   → run it
  native\build-target.bat reader2_autoattach_harness → run it
  qml.exe -platform offscreen tests/reader2_logic_harness.qml   (Qt 6.11.1 msvc2022_64)
  qml.exe -platform offscreen tests/reader2_chrome_smoke.qml
  node tests/reader2_paper_text_test.mjs
All must print VERDICT: PASS.

Output: per-item verdicts + evidence, any new findings, then an overall
APPROVE / REQUEST-CHANGES.
```
