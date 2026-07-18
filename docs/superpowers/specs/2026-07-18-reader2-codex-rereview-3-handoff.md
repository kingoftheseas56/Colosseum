# Codex re-review #3 handoff — reader2 (2026-07-18)

Copy-paste prompt for Hemanth to hand Codex. Answers re-review #2's REQUEST-CHANGES
(items 1 + 3 + the DNS cache race) in commit `0e0c01b`.

```
Cross-model RE-review #3 for Colosseum reader2, requested by Agent 2. Your re-review
#2 returned REQUEST-CHANGES on cross-book async safety and the dictionary deadline.
Verify each in commit 0e0c01b (branch agent2/reader2-fresh-book-reader). You are a
DIFFERENT model than the author. Items 2 (paper sandbox) and 4 (FBZ case) were
RESOLVED in your last pass — do not re-litigate unless this diff regressed them.

VERIFY EACH — state RESOLVED / NOT-RESOLVED / PARTIAL + one line of evidence:

1. Intermediate stale 'ready' (your finding: a queued A 'ready' with gen >
   currentGen was adopted after openBook(B), re-arming bookReady). Generation
   ownership MOVED: qml/reader2/ReaderShell.qml openAtResume now ISSUES currentGen
   (bump + bookReady=false) and passes it to paper.open(path, cfi, gen);
   resources/reader2/paper_glue.js paperOpen uses the passed gen (bench fallback
   +1) and echoes it on every book-scoped emit. 'ready' is accepted ONLY on exact
   gen match — L.acceptReady in qml/reader2/Reader2Logic.js (non-finite defensive);
   the adoption branch is deleted. errorDisposition v2: gen !== issued → drop,
   issued → open-fail pre-ready / operational post-ready. Tests: logic harness
   sections 3c/3d/3e including the exact queued-intermediate-ready case
   (acceptReady(1,2) === false, acceptReady(3,2) === false).

2. Untagged selection events (your finding: 'selection'/'highlightTapped'
   unstamped). paper_glue.js now stamps gen on 'selection', 'selectionCleared',
   and 'highlightTapped' (attachSelection takes the view's captured gen;
   show-annotation closes over it); ReaderShell gates selection/highlightTapped
   through acceptBookEvent.

3. Footnote capture overwrite before async admission (your finding: before-render
   fires after the handler's async view.open, so the two-slot staging still
   raced). Captures are now KEYED BY NOTE HREF — present on BOTH ends of the async
   chain ('link' detail and 'render' detail in vendor footnotes.js): 'link'
   records footnoteTaps.set(href, {gen, rect}); 'render' consumes its own href's
   entry; normal links / rejects delete theirs; paperOpen clears the map. A render
   with NO recorded entry emits NOTHING (inventing a gen would relabel a
   superseded book's note as current).

4. Dictionary overall deadline (your finding: DNS 8s + HTTP 8s ≈ 16s total).
   Reader2Bridge.cpp dictLookup starts ONE QDeadlineTimer(8s); the DNS callback
   passes only deadline.remainingTime() to sendDictRequest(word, query,
   timeoutMs), so DNS + HTTP ≤ 8s total. The resolved-cache path passes the full
   8s (no DNS phase).

5. DNS cache race (your new finding: a concurrent failed callback cleared
   m_wiktIpv4 while m_wiktResolved stayed true). The callback now writes BOTH
   fields only on success (`if (!ipv4.isEmpty()) { m_wiktIpv4 = ipv4;
   m_wiktResolved = true; }`) and never clears — a transient failure leaves the
   pin and the retry flag intact.

ALSO: sweep diff 008b408..0e0c01b for new defects these fixes introduced —
especially: first-open and same-book-reopen flows under issued gens; the bench
(browser) fallback path in paperOpen; a legitimate 'ready' wrongly dropped; the
footnote map lifecycle (leak or premature delete when two anchors share one href).

Build + run before verdict (kill any running reader2_harness.exe first — link
fails on a locked exe). IMPORTANT for the two QML runners: qml.exe -platform
offscreen swallows console output unless you set QT_FORCE_STDERR_LOGGING=1 —
set it and confirm the printed "VERDICT: PASS", don't rely on exit code alone:
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
