# Colosseum Downloads Essential Fixes Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans. This replacement plan is deliberately small and tightly coupled; execute it inline in order.

**Goal:** Stop Downloads from advertising dead actions or deleting files silently, and make its existing five lanes report the minimum truthful state.

**Architecture:** Keep the existing page and its separate audiobook adapter. Add only the missing truth fields to `LocalDownloads`, one inline confirmation surface in `DownloadsPage.qml`, and the narrow existing-reader route needed to open a completed audiobook. Do not create a component framework or redesign the page.

**Tech Stack:** Qt 6.11, C++17, Qt Quick/QML, focused PowerShell/QML contracts.

## Global Constraints

- No keyboard or remote functions.
- No reusable Downloads components or context-menu system.
- No state restoration, virtualization, Theme, backdrop, ScrollGlide, hit-target, icon, animation, or ultrawide work.
- No generalized operation-request IDs, timeout machinery, or multi-operation state machine.
- Keep Theatre Retry/Pause/Resume/Play only where the backend already supports them.
- Non-Theatre failures show their real reason and a route to their owning world, never Retry.
- Replace destructive `Remove` wording with `Delete local copy`.
- Confirm local deletion and group/season cancellation before invoking the existing backend.
- Use the truthful deletion results implemented by commits `33b7cd0` and `76274e3`; show failure and keep the row when deletion fails.
- Failed jobs are attention, not active arrivals.
- Include audiobook jobs and completed audiobook bytes/items in the visible manager/summary.
- Preserve all unrelated behavior and touch no PlayerPage or MangaReader file.

---

### Task 1: Truthful backend deletion — COMPLETE

**Commits:** `33b7cd0`, `76274e3`

The reviewed slice provides checked file/tree deletion, preserves indexes on
failure, and stops cancellation cleanup failure from being reported as
successful removal. Do not expand it further.

---

### Task 2: Add only the missing truth data

**Files:**

- Modify: `native/engine/LocalDownloads.h`
- Modify: `native/engine/LocalDownloads.cpp`
- Modify: `native/engine/AudiobookDownloader.h`
- Modify: `native/engine/AudiobookDownloader.cpp`
- Modify: `qml/BiblioBook.qml`
- Test: `tests/test_downloads_essentials.ps1`

**Produces:**

- `LocalDownloads::activeJobs()` rows with `error`, `canRetry`, `canPause`,
  `canResume`, and `canPlay`.
- Retained non-Theatre failure rows until dismissal or a new attempt.
- Audiobook active rows retain title, author, and error.
- Completed audiobook rows carry `bookId` and `bookPath` when known.

- [ ] Write `tests/test_downloads_essentials.ps1` first. It must fail until:
  Theatre is the only owner assigned retry/pause/resume capabilities;
  non-Theatre failure reasons are retained; audiobook failure maps preserve
  title/author/error; and `downloadAudiobook` receives `detail.localPath`.

- [ ] Run:

  ```powershell
  powershell -ExecutionPolicy Bypass -File tests/test_downloads_essentials.ps1
  ```

  Expected RED: the first missing truth seam is named.

- [ ] In `LocalDownloads`, retain a small map of terminal failures keyed by
  `world + ":" + id`. Capture the last normalized active row, add the bounded
  backend reason, set every transport capability false, and set
  `canDismiss:true`. Clear it on a new progress/finished/removed signal.
  Add `dismissFailure(world,id)`; do not add request IDs or timers.

- [ ] Populate capability flags directly:

  ```cpp
  canRetry  = world == "theatre" && state == "failed";
  canPause  = world == "theatre" && state == "downloading";
  canResume = world == "theatre" && state == "paused";
  canPlay   = world == "theatre" && !url.isEmpty() && state != "failed";
  ```

- [ ] In `AudiobookDownloader`, retain the failed job's existing title,
  author, received, total, and reason in `activeDownloads()` until
  `dismissFailure(pairKey)`. Persist `bookId` and `bookPath` in completed
  entries. Pass `detail.localPath` as the existing sixth argument from
  `BiblioBook.qml`.

- [ ] Run the essentials contract and build `colosseum`; both must pass.

- [ ] Commit:

  ```powershell
  git commit -m "fix: retain truthful download failure data"
  ```

---

### Task 3: Apply the minimal page fixes in place

**Files:**

- Modify: `qml/DownloadsPage.qml`
- Modify: `tests/downloads_page_load_harness.qml`
- Modify: `tests/test_downloads_essentials.ps1`
- Test: `tests/downloads_essentials_harness.qml`
- Test: `tests/test_downloads_essentials_qml.ps1`

**Produces:**

- One inline confirmation popup.
- One status/error line.
- Capability-gated actions.
- Audiobooks included in the existing manager/summary.
- Byte-weighted known-size group progress.

- [ ] Create the QML harness and runner first. Inject `downloadsApi` and
  `audiobooksApi` properties into the real page. The fake data must include:
  a failed Theatre job, failed manga job, active audiobook with title/author,
  completed audiobook, unequal known byte totals, and a deletion failure.

- [ ] Run the QML runner. Expected RED: the page lacks injectable APIs or the
  required helper behavior.

- [ ] Replace direct global reads with these two defaulted properties:

  ```qml
  property var downloadsApi:
      (typeof LocalDownloads !== "undefined") ? LocalDownloads : null
  property var audiobooksApi:
      (typeof Audiobooks !== "undefined") ? Audiobooks : null
  ```

- [ ] Render Retry/Pause/Resume/Play only from the row's capability flags.
  For a non-Theatre failed row, show its reason plus the existing
  `openWorldRequested(world)` route and `Dismiss`. Do not add a context menu.

- [ ] Preserve audiobook title/author when progress or failure updates its
  existing active map. Count audiobook `resolving`, `queued`, and
  `downloading` rows as arriving; count failed audiobook rows as attention.
  Include `abDone.length` and their bytes in the visible totals.

- [ ] Replace average group ratio with:

  ```qml
  ratio = sumKnownReceived / sumKnownTotal
  ```

  When no child has a known total, show no percentage.

- [ ] Add one inline modal confirmation object with
  `confirm(title, body, destructiveLabel, callback)`. Use it for:
  completed-row deletion, completed-audiobook deletion, and group/season
  cancellation. Backdrop clicks never confirm.

- [ ] Replace `Remove` with `Delete local copy`. Deletion is a synchronous
  invokable, so do not add pending-operation state with no observable waiting
  interval. Inspect `{success,message}`:
  success refreshes; failure shows the bounded message and leaves the row.

- [ ] Make the audiobook empty nudge call
  `openWorldRequested("audiobook")`. Emit
  `openAudiobookRequested(item)` from completed audiobook rows whose
  `bookPath` exists; otherwise show `The paired book is not available locally.`

- [ ] Run the load harness, essentials contract, and QML harness; all pass.

- [ ] Commit:

  ```powershell
  git commit -m "fix: make Downloads actions and totals honest"
  ```

---

### Task 4: Add the narrow audiobook route

**Files:**

- Modify: `qml/Main.qml`
- Modify: `qml/reader2/ReaderShell.qml`
- Modify: `tests/test_downloads_essentials.ps1`

**Produces:**

- `routeDownloadedAudiobook(item)`.
- `ReaderShell.openAudioPanel()`.

- [ ] Extend the failing essentials contract to require the route, signal
  connection, and reader seam.

- [ ] Add to `ReaderShell.qml`:

  ```qml
  function openAudioPanel() { chrome.openPanelTo("audio") }
  ```

- [ ] Route a completed audiobook only when `bookPath` is non-empty:

  ```qml
  function routeDownloadedAudiobook(item) {
      if (!item || !String(item.bookPath || "").length) return
      win.closeDownloadsPage()
      win.openBookSession(item.bookPath, {
          id: item.bookId || item.bookPath,
          title: item.title || "Book",
          openAudio: true
      })
  }
  ```

- [ ] Connect `openAudiobookRequested`. After the existing reader
  `openBook(...)` call, use `Qt.callLater(item.openAudioPanel)` only when
  `bookMeta.openAudio === true`.

- [ ] Run the essentials contract, Downloads load test, existing Reader2
  chrome smoke, and `colosseum` build.

- [ ] Commit only the Downloads-related `Main.qml` hunks and the additive
  reader seam:

  ```powershell
  git commit -m "fix: open downloaded audiobooks in the reader"
  ```

---

### Task 5: Verify the reduced scope

- [ ] Run:

  ```powershell
  powershell -ExecutionPolicy Bypass -File tests/test_downloads_essentials.ps1
  powershell -ExecutionPolicy Bypass -File tests/test_downloads_essentials_qml.ps1
  powershell -ExecutionPolicy Bypass -File tests/test_downloads_page_loads.ps1
  powershell -ExecutionPolicy Bypass -File tests/test_downloads_manager_p0.ps1
  powershell -ExecutionPolicy Bypass -File tests/test_downloads_play_arriving_p0.ps1
  cmake --build native/build-msvc --target download_file_ops_harness colosseum --config Release
  ```

- [ ] Confirm the branch diff contains none of:
  keyboard handlers, focus topology, context menus, reusable Downloads
  controls, virtualization, state snapshots, Theme/backdrop/ScrollGlide
  changes, PlayerPage, or MangaReader.

- [ ] Write a short eyes-on checklist covering only:
  dead actions absent, failure reason visible, delete/cancel confirmation,
  forced deletion failure, audiobook-only manager/totals, weighted progress,
  and completed audiobook open.

- [ ] Stop for Hemanth's real-app eyes-on result.
