# Scoped Search History Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Persist independent Tankoban, Biblio, and Theatre recent searches across process restarts without coupling history to provider callbacks.

**Architecture:** A focused QSettings-backed `SearchHistoryStore` is the sole source of truth and is exposed to QML as `SearchHistory`. Search surfaces reload the relevant stable scope on creation/change and commit only explicit dispatched user searches.

**Tech Stack:** Qt 6 Core/QML, C++, QSettings, QML, PowerShell test harnesses.

## Global Constraints

- Use `QSettings("Brotherhood", "Colosseum")` and explicitly synchronize writes.
- Scopes normalize to `tankoban`, `biblio`, and `theatre`; each contains at most six entries.
- Preserve existing live provider dispatch and stale-result guards; callbacks must not commit history.
- Do not modify unrelated user worktree changes.

---

### Task 1: Durable native store and behavioral harness

**Files:**
- Create: `native/SearchHistoryStore.h`
- Create: `tests/search_history_store_harness.cpp`
- Create: `tests/test_search_history_p0.ps1`
- Modify: `native/main.cpp`
- Modify: `native/CMakeLists.txt`

- [ ] **Step 1: Write a failing store harness** covering normalization, trim/minimum length, case-insensitive move-to-front dedupe, six-entry cap, remove, clear, scope isolation, malformed data, reconstruction, and a second process invocation using a temporary INI path.
- [ ] **Step 2: Compile and run the harness before the store exists.** Expected: compile failure because `SearchHistoryStore.h` is absent.
- [ ] **Step 3: Implement `SearchHistoryStore`** with `list`, `record`, `remove`, `clear`, `revision`, and `changed(scope)`. Store one `QStringList` per normalized scope under `searchHistory/<scope>` and call `sync()` after mutations.
- [ ] **Step 4: Register the store in `main.cpp` as context property `SearchHistory` and include it in the native build.**
- [ ] **Step 5: Rebuild and run the store harness.** Expected: all behavioral and reconstruction assertions pass.

### Task 2: Search surface integration and intentional commits

**Files:**
- Modify: `qml/SearchSurface.qml`
- Modify: `qml/BiblioSearch.qml`
- Delete: `qml/SearchHistory.js`
- Modify: `tests/test_search_history_p0.ps1`

- [ ] **Step 1: Write failing contract assertions** for native store registration, stable scopes, explicit `commitCurrentQuery`, absence of callback-only history commits, scope reloads, and independent Biblio remove action.
- [ ] **Step 2: Run the contract test.** Expected: fail against the existing JavaScript store and callback-based calls.
- [ ] **Step 3: Replace QML JavaScript history calls with `SearchHistory`.** Use `tankoban`/`theatre` in the shared surface and `biblio` in Biblio. Subscribe to `SearchHistory.changed` and reload when the matching scope changes.
- [ ] **Step 4: Commit only intentional searches.** Keep debounced dispatch responsive; commit on Enter, shortcuts/recent selection, result or Top Match opening, and exit after an actual dispatch. Remove all provider-completion commits.
- [ ] **Step 5: Split Biblio’s chip body and remove control into separate mouse targets** so remove neither starts a search nor falls through.
- [ ] **Step 6: Run contract and store tests.** Expected: both pass.

### Task 3: Native build and restart proof

**Files:**
- Modify: `tests/test_search_history_p0.ps1`

- [ ] **Step 1: Add a PowerShell restart-level proof** that invokes the compiled store harness in `--write` and then a separate `--verify` process against the same temporary INI file.
- [ ] **Step 2: Build Colosseum with the repository’s native build command.** Expected: target `colosseum` succeeds.
- [ ] **Step 3: Run the complete search-history test.** Expected: store behavior, QML contract, and separate-process persistence all pass.
- [ ] **Step 4: Inspect scoped diff and commit only feature files** with `fix(search): persist scoped history across app restarts`.
