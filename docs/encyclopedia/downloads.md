# Downloads — subsystem guide

> **Hand-written. Keep it true.** If you change how Colosseum presents, groups, deletes, cancels, or opens local
> downloads, update this file in the same commit. The per-file index beside it
> ([`downloads-index.md`](downloads-index.md)) is generated — never edit that one.
>
> Drafted via Preflight-Architect, ground-truthed and adopted by the Hemanth-seat session, 2026-08-09.
> Source-read against `master@e47e556763afbbb4e2eaeb4ab62bac1e45acd090`; ground-truth pass verified the
> mechanism claims below (400ms coalescing timer, exact-row Theatre cancellation, the "owns no files/network"
> self-description, `index.json`'s plain-QFile write, and the fileIdx-stays-original-index contract) against
> the live source, and corrected one inaccuracy in the draft (§6, filesystem-deletion gate). **Ownership note:**
> the Downloads page belongs to no single lane — Hemanth personally gates concurrent edits across agents; check
> with him before substantial work here.

## 1. What this subsystem is for

Give the house **one honest answer to "what is downloading, and what do I already hold locally?"** without
teaching QML how MangaDownloader, BookDownloader, ComicDownloader, the Theatre DownloadStore, Tankoban volume
acquisition, and audiobook delivery each happen to represent the same ideas differently.

There are two deliberately different read paths:

- `LocalDownloads` normalizes the five ordinary download backbones into one `world → series → item` model;
- `AudiobookDownloader` stays its own engine and its completed/live rows are folded into `DownloadsPage.qml`
  separately.

That split is intentional. `LocalDownloads` is a **read-model and action router**, not another download engine. It
owns no downloaded files and starts no network transfers. The individual world backends remain authoritative for
their files, job state, cancellation, pause/resume support, and deletion.

The page then makes two different questions visible:

1. **Now arriving** — live, queued, resolving, paused, packing/ingesting, or failed work across worlds;
2. **what has landed** — settled local media grouped into Tankoban, Biblio, Theatre, plus completed audiobooks.

Progress/resume state is not part of this subsystem's local-library truth. "Where did I stop watching/reading?" is
a different state owner.

## 2. The flow

**Ordinary cross-world downloads:**

```
owner backends
  |
  +-- MangaDownloader -------- chapters ----------------------+
  +-- ComicDownloader -------- issues ------------------------+
  +-- MangaTankobanService --- volumes -----------------------+
  +-- BookDownloader --------- books -------------------------+
  +-- DownloadStore ---------- movies / episodes / live jobs -+
                                                              |
                                                              v
                                                    LocalDownloads
                                              (NO files, NO network)
                                                              |
                           +----------------------------------+------------------+
                           |                                                     |
                           v                                                     v
                     activeJobs()                                      series(world)
                     "Now arriving"                                    items(world,key)
                           |                                                     |
                           +-------------------------+---------------------------+
                                                     |
                                                     v
                                              DownloadsPage.qml
```

`LocalDownloads` listens to each owner's mutation/progress signals and bumps one revision for QML. Chatty progress
signals are coalesced rather than repainting the whole page on every page/chunk.

For settled rows it normalizes the owners into three worlds:

```
Tankoban
  manga chapters
  + western comic issues
  + manga volumes
       |
       +-- chapters and volumes with the same manga series id
           intentionally land in ONE series card

Biblio
  downloaded books
       |
       +-- author known    -> group by author
       +-- author missing  -> one honest book card; never invent an author

Theatre
  movies
  + episodes
       |
       +-- episodes -> group by show
       +-- movies   -> stand alone
```

Actions travel back down the same ownership boundary:

```
DownloadsPage
    |
    v
LocalDownloads.remove/cancel/retry/pause/resume
    |
    +-- tankoban volume id -> MangaTankobanService
    +-- known manga job    -> MangaDownloader
    +-- otherwise comic    -> ComicDownloader
    +-- biblio             -> BookDownloader
    +-- theatre            -> DownloadStore
```

**Audiobooks are the exception:**

```
book page chooses audiobook torrent
          |
          v
AudiobookDownloader.downloadAudiobook(pairKey, infoHash, title, author, bookId, bookPath)
          |
          v
StreamServer.prefetch(infoHash, 0)
          |
          +-- fetchReady stream URL ----------+
          |                                    |
          +-- watchdog polls streamUrl() ------+
                                               |
                                               v
                              derive localhost engine base
                                               |
                                               v
                          POST <base>/<infoHash>/create
                                               |
                                               v
                          torrent file manifest: files[]
                                               |
                              filter audio extensions
                              natural-sort tracks
                                               |
                                               v
                    GET <base>/<infoHash>/<fileIdx>
                                               |
                                      <track>.part
                                               |
                                      atomic rename
                                               |
                                               v
             <AppData>/audiobooks/<pairKey-hash>/<NN - track.ext>
                                               |
                                               v
                         <AppData>/audiobooks/index.json
                                               |
                                  +------------+------------+
                                  |                         |
                                  v                         v
                       DownloadsPage audiobook lane   AudioPairingStore
                                                      (when bookId exists)
```

Only one audiobook job is active at a time; later jobs queue. The engine-base handshake deliberately races
`StreamServer::fetchReady` against polling because a cold engine can lose the fast signal path.

`DownloadsPage.qml` therefore has two APIs:

```
LocalDownloads  -> normal Tankoban / Biblio / Theatre rows
Audiobooks      -> audiobook completed rows + active/failed jobs
```

The page combines their counts and presentation. It does **not** pretend they share one persistence engine.

## 3. The files that matter

Full per-file descriptions belong in [`downloads-index.md`](downloads-index.md).

| File | Role |
|---|---|
| `native/engine/LocalDownloads.h` | contract for the unified Downloads read-model: totals, world/series/items, live jobs, and routed actions |
| `native/engine/LocalDownloads.cpp` | normalizes the five owner backends, coalesces changes, retains terminal failures, applies capability flags, and routes mutations back to the correct owner |
| `native/engine/AudiobookDownloader.h` | audiobook job/read-model contract exposed to QML: queue, progress, completed sets, delete/cancel, and read-along attachment |
| `native/engine/AudiobookDownloader.cpp` | Stremio-engine manifest/stream transport, per-track `.part` writes, audiobook index, queue promotion, cleanup, and pairing |
| `native/engine/DownloadFileOps.h` | shared destructive-operation seam: idempotent missing-file success, post-delete existence check, and bounded user-facing failure result |
| `qml/DownloadsPage.qml` | the actual Downloads product surface: "Now arriving" first, landed world shelves second, plus the separate audiobook lane |
| `native/player/downloadstore.cpp/.h` | **owned by `player.paths`** — Theatre download authority; Downloads only reads/routes to it |
| `native/engine/BookDownloader.cpp/.h` | **owned by `biblio.paths`** — ordinary ebook download authority |
| `native/engine/ComicDownloader.cpp/.h` | **owned by `comics.paths`** — western issue download authority |
| `native/engine/MangaDownloader.cpp/.h`, `MangaTankobanService.cpp/.h` | **owned by `tankoban.paths`** — manga chapter and volume authorities |
| `qml/BiblioBook.qml`, `qml/reader2/ReaderShell.qml` | **owned by `biblio.paths`** — initiate/consume the audiobook read-along route |
| `qml/Main.qml`, `native/main.cpp` | shell composition boundary: create/inject the backends and route page open/play requests |

Do not solve manifest gaps by duplicating every backend into `downloads.paths`. Their implementation belongs to
their world guides. This page owns the **aggregation contract and the audiobook lane**.

## 4. Where state lives

- **The ordinary downloaded files do not live in `LocalDownloads`.** Their owner backends remain the storage
  authority. `LocalDownloads` rebuilds its rows by asking those owners what is present.

- **`LocalDownloads::m_failures` is memory-only presentation state.** Terminal failures from ordinary non-Theatre
  owners are retained long enough for the Downloads page to show an honest failure row and let the user dismiss
  it. Restarting the application does not turn this map into a durable job ledger.

- **`LocalDownloads::m_revision` is a wake-up counter, not data.** Backend signals arm a 400 ms single-shot timer
  (verified: `native/engine/LocalDownloads.cpp:35`, `coalesce->setInterval(400)`); QML responds to the resulting
  `changed()` notification by rereading the authoritative rows.

- **`<AppData>/audiobooks/` is audiobook storage.** Each `pairKey` maps to a SHA-1-derived directory name. Tracks
  are written as `.part` files and renamed only after the transfer completes.

- **`<AppData>/audiobooks/index.json` is the completed-audiobook catalogue.** It records the final directory,
  ordered file paths, title, author, bytes, timestamp, and — when supplied — the reader's `bookId` / ebook path.

- **Audiobook active jobs and failures are memory-only.** One `m_active` job plus `m_queue` represents work in
  flight. The Downloads page seeds itself from `activeDownloads()` when opened because signals alone cannot
  reconstruct work that started before the page existed.

- **`AudioPairingStore` owns read-along attachment.** `AudiobookDownloader` writes to it after completion only
  when it was given a real reader `bookId`. The Downloads page does not own that relationship.

- **`DownloadsPage.qml` owns transient presentation state only:** expanded groups/seasons, confirmation UI,
  cooldown countdowns, mutation copy, and the current snapshots of the two APIs.

## 5. Traps

1. **Do not turn `LocalDownloads` into a sixth download engine.** Its contract explicitly says it owns no files
   and no network (verified: `native/engine/LocalDownloads.h`'s own header comment, near-verbatim). Every
   remove/cancel/pause/retry call eventually belongs to one of the real backends.
   **WHY:** duplicating ownership here creates two sources of truth for the same file/job. A row could disappear
   from the Downloads page while the owner still believes it exists, or vice versa.

2. **Audiobooks do NOT pass through `LocalDownloads`.** `DownloadsPage.qml` queries `Audiobooks` separately, seeds
   completed and active audiobook state from it, then combines the totals at the presentation layer.
   **WHY:** adding audiobooks to `LocalDownloads` without simultaneously removing the separate lane would
   double-count them and could draw the same job twice. The split is architecture, not unfinished cleanup.

3. **A `changed()` signal is intentionally not synchronous with every byte.** `LocalDownloads` coalesces noisy
   backend progress through a 400 ms timer.
   **WHY:** code or tests that expect `revision` to advance once per source signal are asserting an implementation
   that the subsystem deliberately avoids. The contract is eventual refresh on a human UI timescale.

4. **Theatre cancellation must be exact-row.** The Downloads route calls `DownloadStore::cancelJob(id)` (verified:
   `native/player/downloadstore.h:46`, takes an explicit `id`; a self-test comment at
   `downloadstore.cpp:700` literally names this "the bug's exact click"), never the old no-argument
   active-download cancellation.
   **WHY:** a Downloads card can represent a queued or failed job while another video is actively downloading.
   Cancelling "the active one" from a clicked row already proved capable of killing the wrong transfer — this is
   a real, previously-shipped regression, not a hypothetical.

5. **Do not infer capabilities from the world name in QML.** Rows carry `canRetry`, `canPause`, `canResume`,
   `canPlay`, `canCancel`, and `canDismiss`. Today retry/pause/resume are Theatre-only; other engines do not
   retain enough failure/transport state for a blind retry.
   **WHY:** drawing a button just because "downloads ought to support pause" creates dead controls or, worse,
   calls an operation with different semantics on another backend.

6. **The `tankoban:` id namespace is routing information.** Volume-mode records are sent to
   `MangaTankobanService` before the facade tries the ordinary manga/comic lanes.
   **WHY:** Tankoban deliberately folds manga chapters, western issues, and whole volumes into one visible world.
   Their similar presentation does not make their delete/cancel owners interchangeable.

7. **The Downloads page must seed live jobs when it opens.** This matters most for audiobooks:
   `activeDownloads()` exists because a job may have begun before lazy `DownloadsPage.qml` was instantiated.
   **WHY:** listening only to `resolving/progress/finished` signals makes already-running work invisible until its
   next signal — and on a cold engine that interval can be long enough to look broken.

8. **An audiobook's torrent manifest index is part of the URL contract.** The downloader filters the manifest to
   audio files for presentation/order, but `fileIdx` remains the file's index in the ORIGINAL torrent `files[]`
   array (verified: `native/engine/AudiobookDownloader.cpp:429`, `fj.fileIdx = i` set during the original
   iteration, comment reads "the streaming URL segment = original array index"; used directly to build the
   fetch URL at line 462).
   **WHY:** renumbering the filtered list to `0..N-1` can ask the StreamServer for a cover, text file, or
   completely different torrent entry instead of the intended chapter.

9. **Audiobook completion is per-file `.part` → final rename, not a directory transaction.** Existing final files
   of the exact expected length are reused when a job restarts; incomplete current transfers are removed on
   failure/cancel.
   **WHY:** changing that to "delete the directory before every retry" throws away already-landed chapters.
   Conversely, treating any existing filename as complete ignores the manifest length guard.

10. **`index.json` is weaker than the track writes.** Track landing uses the `.part`/rename boundary, but the
    audiobook index itself is written with ordinary `QFile` truncate/write, not `QSaveFile` (verified:
    `native/engine/AudiobookDownloader.cpp:155`, `saveIndex()` opens a plain `QFile`).
    **WHY:** a crash during index rewrite can lose catalogue metadata even though the audio directories survived.
    Do not describe the audiobook catalogue as transactionally durable; hardening it is a separate decision.

11. **Startup validates only enough of an audiobook to keep the entry, not every chapter.** Index loading keeps an
    entry when its stored file list is non-empty and the first file exists; `localFiles()` later filters missing
    individual paths.
    **WHY:** "downloaded" does not prove every indexed track is still present after external filesystem damage.
    Code that needs a complete set must check the returned files, not only the top-level downloaded flag.

12. **Delete failure must preserve the logical row.** `DownloadFileOps` checks both the remover's return value and
    whether the payload still exists. `AudiobookDownloader::deleteAudiobook` erases its index entry only after
    directory deletion succeeds.
    **WHY:** optimistic UI removal after a filesystem denial creates the worst state: the bytes remain on disk but
    the application forgets how to show or retry removing them.

13. **A boot smoke does not prove DownloadsPage can open.** The page is lazy-loaded. The repository has a
    dedicated headless instantiation test because a creation-time QML error once survived normal boot coverage.
    **WHY:** syntax/static lint and "the main window opened" can both be green while the first click on Downloads
    produces no page.

14. **"Play while arriving" crosses the Downloads boundary without transferring playback ownership.** A Theatre
    live row may expose its already-resolved URL; the page emits a request and the shell/player creates the
    session.
    **WHY:** Downloads should expose the truthful job data, not grow its own player or resolver. Once the landed
    local copy exists, normal playback ownership resumes elsewhere.

## 6. How to test it

The current repository has several different kinds of Downloads gates. They do not prove the same thing.

**Static manager/action contract:**

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests/test_downloads_manager_p0.ps1
```

This protects exact-row Theatre cancellation, grouping fields, pause/resume wiring, season grouping, and the
LocalDownloads/page contract.

**Native + page truth/safety contract:**

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests/test_downloads_essentials.ps1
```

For only its native assertions:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests/test_downloads_essentials.ps1 -NativeOnly
```

This checks retained failure semantics, destructive-action result handling, audiobook metadata, confirmation
wording, capability-gated actions, and the narrow completed-audiobook route.

**Actually instantiate the QML surface:**

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests/test_downloads_essentials_qml.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File tests/test_downloads_page_loads.ps1
```

These require the Qt path encoded in the scripts. The second test exists specifically because the lazy page can
fail at creation time while ordinary shell boot tests stay green.

**Play while arriving:**

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests/test_downloads_play_arriving_p0.ps1
```

This checks the read-model chain from Theatre DownloadStore through LocalDownloads into the page and shell/player
routing.

**Read-along attachment:** the native build defines `reader2_autoattach_harness`, which exercises audiobook
auto-attach without a live torrent by passing a null StreamServer. It is a useful deterministic seam, but at this
SHA it is not registered in the current CTest pilot list.

**Filesystem deletion — CORRECTED (Hemanth-seat verification, 2026-08-09):** `tests/download_file_ops_harness.cpp`
checks that an injected delete failure leaves the payload in place, real deletion removes it, and deleting an
already-missing payload is success. It **does compile as a real build target**
(`native/CMakeLists.txt`, `add_executable(download_file_ops_harness ../tests/download_file_ops_harness.cpp)`) —
this is not missing from the build. What is true is narrower: it has **no CTest registration**
(`tests/CMakeLists.txt` has no matching `add_test`), so `ctest -L unit` never runs it. Run it directly:

```
native\build-msvc\download_file_ops_harness.exe
```

until someone registers it — this is a real instance of the documented "39 of 69 harnesses invoked by nobody" gap
(`docs/colosseum-test-verification.md`), not a build-system omission.

**Live audiobook transport:** `audiobook_engine_probe` is built explicitly as a real
AudiobookDownloader + StreamServer network probe. CMake labels it network-dependent and deliberately does not put
it in the ordinary suite. Use it only as a deliberate live integration check after reading its current invocation
contract.

A green static PowerShell contract is **not a working Downloads page**. A green headless page is **not a working
download**. Before adopting a change to transport or destructive actions, the human/live pass should still prove:

- a job started before opening Downloads appears immediately;
- a cross-world set of jobs groups under the correct world/series;
- clicking cancel affects exactly the selected job;
- a refused filesystem delete remains visible with an honest error;
- a completed audiobook opens through its paired Biblio reader;
- a multi-file audiobook preserves natural chapter order;
- cancelling midway leaves no current `.part` while not destroying already-complete tracks unintentionally;
- a live Theatre row can play the resolved arriving URL without stopping the download.

## Keeping this page honest

```bash
# refresh the generated index after changing a covered source file
python scripts/code_encyclopedia.py --paths docs/encyclopedia/downloads.paths \
  --output docs/encyclopedia/downloads-index.md --state docs/encyclopedia/downloads-state.json

# gate: fails when a covered file changed since its description was accepted
python scripts/code_encyclopedia.py --paths docs/encyclopedia/downloads.paths \
  --output docs/encyclopedia/downloads-index.md --state docs/encyclopedia/downloads-state.json --check

# after reviewing a changed description, ratify that file
python scripts/code_encyclopedia.py --paths docs/encyclopedia/downloads.paths \
  --output docs/encyclopedia/downloads-index.md --state docs/encyclopedia/downloads-state.json --accept <path>
```
