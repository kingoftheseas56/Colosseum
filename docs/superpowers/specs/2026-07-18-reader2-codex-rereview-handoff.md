# Codex RE-review handoff — reader2 hardening batch

**Requested by:** Agent 2 (Claude, Opus 4.8). **Reviewer = Codex** (you flagged the originals). This is a focused re-check that the **five REQUEST-CHANGES blockers** from your prior review are resolved. Fix commits: `d861f9d` (batch) + `c1b2a25` (dict-cache follow-up), on branch `agent2/reader2-fresh-book-reader`.

**How to run:** Codex session in the worktree, paste the block, review the fix commits + the named files.

---

```
Cross-model RE-review for Colosseum reader2, requested by Agent 2. You previously returned REQUEST-CHANGES with five merge-blockers. Verify each is now resolved in commits d861f9d + c1b2a25 (branch agent2/reader2-fresh-book-reader). You are a DIFFERENT model than the author.

VERIFY EACH PRIOR BLOCKER — state RESOLVED / NOT-RESOLVED / PARTIAL + one line of evidence:

1. STALE ASYNC OPENS not cancelled. Fix claim: paperOpen (resources/reader2/paper_glue.js) captures myGen per open and re-checks `superseded()` after EVERY await (filesRead/makeBook/view.open/view.init), aborting + removing any view it appended (removeSupersededView only nulls currentView if it still points at the aborted view). ready/error/searchResults/footnote are now gen-tagged; ReaderShell drops stale ones. staleRelocate hardened from `!==` to strictly-older `<` so a NEWER 'ready' (book switch) is adopted, older events dropped. VERIFY the cancellation actually fires after each await and a normal single open is unaffected.

2. PRODUCTION AUTO-ATTACH unwired. Resolution: this is DEFERRED to swap-day (Task 16) BY DESIGN, not fixed here — Reader2Bridge is registered in production main.cpp only at swap (it is inert until the reader is live; the whole reader swaps in as one step). The auto-attach CODE + key-consistency is proven in reader2_autoattach_harness. Confirm this is a reasonable sequencing decision (the feature is correct, just not wired into a production build that doesn't use reader2 yet), OR argue it must be wired now.

3. PAPER not sandboxed (unrestricted filesRead + remote content). Fix claim: Paper.qml localContentCanAccessRemoteUrls:false (file:// stays on); Reader2Bridge::filesRead restricted to a setAuthorizedBook-set, normalized path (returns "" otherwise); ReaderShell calls setAuthorizedBook before paper.open. VERIFY a rigged book can't read arbitrary files or beacon, and a normal book still loads (path normalization symmetric).

4. DICTIONARY: synchronous DNS on GUI thread, unbounded. Fix claim: QHostInfo::lookupHost async + cache-on-success-only (c1b2a25: a transient first failure retries, doesn't permanently disable the IPv4 pin); input <=64 chars, 8s timeout, 512KB response cap; dictResult emitted on every terminal path (no double-emit via a shared done flag). VERIFY no GUI-thread DNS block remains and the bounds hold.

5. AUDIO-FIRST download undefined. Resolution: DEFINED as out-of-v1 in the spec's new "Ratified constraints (v1)" block (docs/superpowers/specs/2026-07-16-colosseum-fresh-book-reader-design.md) — an audiobook downloaded before its ebook does not auto-attach (needs the ebook path's key); backfill is a future enhancement. Confirm the intent is now explicit (not silently broken).

Also re-scan for any NEW correctness/security regression introduced by the fix commits (esp. the staleRelocate `<` change over-dropping legit events, the async-cancel tearing down a normal open, or the filesRead authorization rejecting a legitimate book).

END with exactly one line: APPROVE or REQUEST-CHANGES + one-sentence reason.
```
