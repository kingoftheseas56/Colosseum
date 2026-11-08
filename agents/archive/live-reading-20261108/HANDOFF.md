TASK: live-reading-build-20261108
REQUIRED REPLY FILE: C:\Users\Suprabha\Desktop\Preflight-Architect\missions\2026-11-08-live-reading\REPLY.md

From Claude (Agent 0) to the implementer. Build **opt-in live reading for Tankoban Chapter Mode** in Colosseum, land it on `master`, commit and push. The product decisions below are settled by Hemanth; the technical calls are mine. Don't reopen them. Where this brief is silent, choose the smallest design that works and record why.

## What the user gets
Tankoban gains one setting, **Live reading**, off by default. With it on, pressing **Read** on a chapter that isn't downloaded opens the reader within a few seconds at page 1, and the rest of the chapter downloads a few pages ahead of you while you read. The pages are **temporary**: they live in a self-clearing cache and never appear in Downloads. Everything else stays exactly as it is today: Read on a downloaded chapter, the Download button, Volume Mode, the western comics reader, progress, and Continue Reading.

## Evidence behind the numbers (already measured; don't re-measure)
In `C:\Users\Suprabha\Desktop\Loop-Missions\live-reading-6a328d\` (`REPLY-0001.md`, `PAGE-RAW.csv`, `CHAPTER-RAW.csv`), the app's exact WeebCentral request behaviour was measured from India: 6/6 chapters, 98/98 pages, 0 retries, 0 blocks. First page p50/p95 was 3.3/3.9 s, and pages arrived 0.37 s apart at p50 (1.47 s at p95). A simulated 6-seconds-per-page reader stalled 0 times with 3, 5 and 8 pages of read-ahead. Foreign networks were not measured (the VPN failed); make no claims about them.
**Settled numbers:** read-ahead **5 pages**, at most **3** image requests at once for the live chapter, and the current page always first.

## Source pins (re-read on current master before coding)
Base: kingoftheseas56/Colosseum `master` (`ca5e23602` when this was written). Read `Colosseum/AGENTS.md` first for build, test and harness conventions.
- **Chapter Mode Read today:** `qml/TankobanSeriesView.qml` `_readChapter()` (~L513). If the chapter is downloaded it opens it (`_openChapter`). Otherwise it calls `downloadsRef.downloadChapter(...)` and opens only when the full permanent download finishes (`pendingReadChapterId`). That download-then-wait is what live reading replaces when the setting is on. `qml/TankobanChapterList.qml` emits `readChapterRequested`; keep that contract.
- **Downloader:** `native/engine/MangaDownloader.{h,cpp}`. It already has the page-list fetch for both WeebCentral and Tankoyomi-qualified chapter IDs (~L495-536, `TankoyomiChapterService`, `MangaPageTransport::normalizeTankoyomiPages`), image headers via `MangaPageTransport`, `MangaImageHostResolver`, `IMAGE_CONCURRENCY = 3`, and 3 attempts with 2/4 s backoff plus the soft-block checks. **Reuse all of it.** Never write a second scraper or transport. Permanent downloads go to `AppDataLocation/manga/...` with `index.json`; the image cache uses `CacheLocation` (~L58).
- **Reader shell:** `qml/comicreader/ComicReaderShell.qml`. It has an injectable `pageStore` (~L59, L82-85: "injected pageStore wins") and calls `store.localPages(curChapterId)` once on load (~L453), then `core.openEntry(...)` (~L487).
- **Reader core:** `native/comicreader/ComicReaderCore.{h,cpp}`. `openEntry` marks any page whose `file://` doesn't exist as `PageError::MissingFile` (~L160-190). `retryPage(int)`, `setVisible`, `requestRange` and `setStripViewport` drive decoding. Read the MissingFile cooldown note in `ComicReaderDecode` (~L812-825 of the core).

## Design (build this)
1. **Live jobs in MangaDownloader.** Add a live mode alongside permanent jobs, sharing the page-list fetch, transport, resolver, validation and retry paths.
   - Target folder: `CacheLocation/manga-live/<sanitized chapterId>/page_NNN.<ext>`.
   - Never touches `index.json`, never emits the permanent `finished`, and never appears in `downloadedChapters()` / `activeChapterJobs()` / the Downloads UI.
   - New API, names your choice:
     - `startLive(chapterId, seriesId)`: emits the page count once known.
     - `setLiveFocus(chapterId, page)`: the reader's current page.
     - `stopLive(chapterId)`.
   - New signal `livePageReady(chapterId, index, fileUrl)`, plus a live failure signal carrying a reason.
   - **Scheduling:** at most 3 in flight, fetching pages [focus, focus+5] in order (focus first). On a jump, abandon queued (not in-flight) far pages and refill around the new focus. Don't duplicate a page that is in flight or already saved. The 5-page window refills as the focus advances.
   - **Throttle/soft-block:** apply the existing backoff. Never raise concurrency to catch up. After the existing attempts are exhausted, a page is failed (reader shows its normal per-page retry) and the job pauses rather than hammering.
   - While a live chapter is open, permanent image jobs pause (resuming when the reader closes), so the app never has more than 3 image requests in flight in total. This is settled: it keeps us under the measured burst profile.
2. **Cache lifecycle.**
   - Keep whole chapters (they are small). Keep the open chapter plus the 2 most recently read live chapters, under a 300 MB cap.
   - Evict least-recently-used chapters on each new live open and on app start, including leftovers from a crash.
   - Closing the reader stops fetching for that chapter but keeps its pages (back-navigation and quick reopen).
   - If the user later presses Download on a chapter that has complete or partial live pages, the permanent job **reuses** those files (copy/move into the permanent folder, then fetch only the missing ones) instead of refetching.
3. **Reader core: pages that arrive later.**
   - Add a distinct **pending** page state, which is not `MissingFile`/broken.
   - `openEntry` accepts a page entry flagged pending (for example `{url, pending:true}` for a not-yet-existing `file://`). Pending pages are not failures, have no decode cooldown, and report `pending` through `pageInfo`.
   - Add `Q_INVOKABLE void pageArrived(int page)` (or equivalent): it clears pending, takes the file, invalidates any cooldown or negative cache for that page, and re-requests decode if the page is visible or pinned. Strip layout, page heights and pairing must recompute correctly when a page's real dimensions arrive. Check that single, paired and long-strip layouts (and RTL/LTR) all behave.
   - Don't change behaviour for entries without pending pages. Every existing harness must pass unchanged.
4. **Live page store (QML or C++, your call), injected as `pageStore`.**
   - `localPages(chapterId)` returns the full ordered list: arrived pages as `file://` and the rest flagged pending.
   - It forwards `livePageReady` → `core.pageArrived`, and the reader's current page → `setLiveFocus`.
   - It is only constructed for a live read, so downloaded reads keep the default `Downloads` store.
5. **Chapter Mode wiring** (`TankobanSeriesView.qml` `_readChapter`):
   - Setting on and chapter not downloaded: `startLive`, then open the reader with the live store **once the page count is known and page 1 has arrived**. Until then, show the existing pending-read affordance, cancellable by Back.
   - Downloaded chapter: unchanged. Setting off: today's behaviour exactly.
   - A superseded Read (another chapter, back, series or language change) cancels stale callbacks. The wrong chapter must never open.
   - Next/previous chapter crossing inside the reader uses the same rule (live if not downloaded and the setting is on).
6. **Progress / Continue:** recorded exactly as for a downloaded chapter (same entryId and kind). Never mark a chapter downloaded or completed because it was live-read. Progress must not advance past a page that never arrived. Reopening a live chapter whose cache was evicted simply goes live again and resumes.
7. **The setting:** one toggle, "Live reading", with a single description line ("Read chapters without downloading them first. Pages are kept temporarily."). Put it in the **existing** Tankoban/manga settings surface. Find it; don't invent a new page or new chrome. If no such surface exists, put it nowhere, stop, and report back. Persist it like neighbouring settings. Default **off**.

## Out of scope
Volume Mode, western comics, Biblio, Theatre, CBZ, any new reader or visual redesign, new sources, raising concurrency, desktop/Android divergence (whatever builds for both stays shared), and any change to permanent download semantics beyond the reuse in design §2.

## Verification (required before commit)
- **Unit/harness tests** following the repo's existing harnesses (`tests/comicreader_*`, the downloader tests), with a fake transport and fake clock:
  - never more than 3 in flight;
  - focus-first ordering and refill on page jump;
  - no duplicate fetches;
  - pending → arrived re-renders in single, paired and strip layouts;
  - stale-callback cancellation;
  - soft-block/403/429/DNS failure → page failed + retry works;
  - cache eviction/cap and crash-leftover cleanup;
  - Download reuses live pages;
  - live jobs never touch `index.json` or the Downloads lists.
- **Regression:** every existing comicreader and downloader test passes, Volume Mode is untouched, and downloaded Chapter reads are unchanged. Don't weaken any existing test.
- **One real smoke on Wi-Fi**, after the tests pass, with the desktop app on Laptop 1: setting on, read one current WeebCentral chapter. Record time-to-first-page, confirm the reader opened before the chapter finished, check that jumping 10 pages ahead shows pending and then the page, and check that closing leaves nothing in Downloads. Keep traffic to one or two chapters.
- CI green on the usual required desktop checks (public-trust, linux-desktop, windows-desktop). Android builds stay green if they compile this code.

## Landing
- Work directly on `master` (Rule 28). The git index is shared with other agents: **stage only your own files by explicit path**. Never `git add -A`, and never commit someone else's staged work.
- Use small logical commits (downloader live jobs → core pending pages → store + wiring + setting → tests). Each commit message ends with the attribution your harness requires.
- Push `master` to **both** remotes: `origin` (GitHub) and `bitbucket`. If a push is rejected because master moved, rebase onto it, rerun tests, and push again. Never force-push.
- Don't touch `native/account/Nuvio*` (Arc 78 Nuvio work is active) or the Android player files (`native/platform/android/**`, separate audio work).

## Reply (write `REPLY.md` at the path above)
Start with `TASK: live-reading-build-20261108` and `STATUS: complete | needs-input | blocked`. Include:
- commit hashes and the pushed-to-both-remotes confirmation;
- the design choices you made where this brief left room (store language, API names, setting location);
- test names and results;
- smoke numbers;
- the CI run links;
- anything unverified, stated plainly, keeping **authored / built / tested / runtime-verified / pushed** separate.

End with a **test checklist for Hemanth**: about 6 steps he can do in the app to feel the feature, including turning it off again.
