# Cross-model Codex review handoff — reader2 (the fresh book reader)

**Requested by:** Agent 2 (Claude, Opus 4.8) — producer. **Reviewer must be Codex** (a different model; a model endorses ~1-in-3 of its own drift bugs).

**How to run:** open a Codex session rooted at the worktree
`C:\Users\Suprabha\Desktop\Brotherhood\Colosseum\.worktrees\reader2-fresh-book-reader`
(branch `agent2/reader2-fresh-book-reader`), paste the block below, and let Codex read the listed files. The design spec is `docs/superpowers/specs/2026-07-16-colosseum-fresh-book-reader-design.md` (already amended to the ratified behaviors).

---

```
Cross-model review for Colosseum's reader2 (fresh ebook reader), requested by Agent 2. You are a DIFFERENT model than the author (Claude/Opus). Check the code against the written Definition of Done — intent, not just code-against-code. Read the files on branch agent2/reader2-fresh-book-reader in this worktree.

SCOPE — review ONLY these authored files (IGNORE resources/reader2/vendor/foliate-anx/** — that is a vendored MIT donor library, not our code; and ignore any comics/manga/Main.qml churn, which is unrelated master divergence):
  Native (C++):
    native/reader/BookStores.{h,cpp}         — shared SHA1[:20] key + JSON stores
    native/reader/BookBridge.{h,cpp}         — old bridge now delegating to BookStores
    native/reader2/Reader2Bridge.{h,cpp}     — the reader spine (base64 filesRead, paperEvent relay, stores, dictLookup w/ IPv4 pin, bookKey)
    native/reader2/reader2_harness_main.cpp  — standalone harness
    native/engine/AudiobookDownloader.{h,cpp} — audiobook auto-attach on finish
    native/main.cpp (the setPairing line), qml/BiblioBook.qml (the auto-attach call site)
  Glue (JS on the web "paper"):
    resources/reader2/paper_glue.js          — command/event protocol, format dispatch, in-page keyboard, selection/highlight/footnote, reveal double-click, gen guard
    resources/reader2/paper_text.js          — TXT->reflowable-XHTML shim
    resources/reader2/{paper.html,bridge_boot.js,mock_bridge.js}
  QML (native chrome):
    qml/reader2/*.qml  (Paper, ReaderShell, ReaderChrome, TopBar, BottomRail, LeftPanel, SelectionMenu, DictCard, FootnoteCard, AppearancePanel, SearchSheet, RulerOverlay, Theme, Harness, HarnessShelf)
    qml/reader2/Reader2Logic.js              — ALL pure logic (progress record, reveal reducer, rail ticks, chapterFor, appearance model, search/dict helpers, staleRelocate)
  Tests: tests/reader2_*.cpp, tests/reader2_*.qml, tests/reader2_paper_text_test.mjs

DEFINITION OF DONE (verify each — MET / NOT-MET / PARTIAL, one line of evidence):
  ARCHITECTURE
  1. Native QML chrome over a web "paper"; the web view renders ONLY book pages (no HTML chrome). C++ owns all persistent state + networking.
  2. Zero-migration: reader2 reads/writes the SAME native stores as the old reader, keyed by BookStores::keyFor (SHA1[:20] of the normalized book path) — progress, bookmarks, annotations, settings. Positions/marks/settings survive the swap.
  3. Input model: the web view owns focus + pointer + keyboard; page-turn keys (arrows/Space/PgUp/PgDn) + Esc are handled IN-PAGE by the glue and emitted up as semantic events (escape/toggleChrome/selectionCleared). No QML FocusScope key routing. (This is what makes text selection AND keys both work.)
  FEATURE PARITY (each must work)
  4. Formats: EPUB, MOBI, AZW3, FB2, PDF, TXT open + paginate (TXT via a synthesized reflowable XHTML shim; PDF/CBZ are fixed-layout and degrade gracefully — appearance reflow controls no-op, no crash).
  5. Resume: every position persists; reopening returns to the saved CFI (with a raw-path read fallback for legacy entries).
  6. Chrome: naked reading surface; reveal wakes ONLY on top/bottom edge-band OR double-click on empty page OR the book-open beat — body mouse-move / keys / scroll NEVER wake it; ~3s idle-hide. Icon-only top bar; gold progress rail with chapter ticks + scrub + return chip.
  7. Left panel: Contents (current chapter gold, empty-TOC graceful), Bookmarks (add/jump/delete), Highlights (render + jump), Audio tab.
  8. The pen: select text -> menu -> highlight in 3 colors / note / define (Wiktionary) / copy; tap a highlight -> delete/recolor; footnotes pop a card without navigating. Marks persist to the shared annotation store.
  9. Appearance: themes (Paper/Sepia/Slate/Night), typeface (Literata/Fraunces/Inter, Literata reaching the actual book text via @font-face), size/line-spacing/margins/justify — live-applied, persisted under settings.reader2 (old keys preserved).
  10. Search: find-in-book, capped payload, gold-marked hits, jump (sheet stays open).
  11. Reading ruler: focus band over TEXT pages only (never fixed-layout/cover), controls in Appearance, pointer-transparent (does not block selection).
  12. Read-along: audiobook auto-attaches on download keyed by the SAME book key the reader looks up; Audio tab shows it; "Follow my reading" syncs the audiobook chapter to the page (debounced, does not force-play a paused book).
  DELETED (confirm absent): TTS; any pairing/unpair UI (attachment is automatic); HTML chrome.

YOUR REVIEW — do all four:
1. For EACH DoD item above: MET / NOT-MET / PARTIAL + one line of evidence.
2. Flag anything the code DOES that the DoD never asked for (scope creep / unrequested behavior change).
3. Correctness + security pass: real bugs, regressions, races, leaked secrets, unsafe input/network handling (esp. the base64 bridge, the dict network path, the TXT/HTML synthesis, the QWebChannel seam, the cross-book gen guard).
4. Anything the DoD SHOULD have specified but didn't (gap in intent).

END with exactly one line: APPROVE or REQUEST-CHANGES, plus a one-sentence reason. Be terse; default to REQUEST-CHANGES if any DoD item is NOT-MET or you are unsure.
```
