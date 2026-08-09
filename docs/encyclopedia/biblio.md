# Biblio (books world + EPUB reader) — subsystem guide

> **Hand-written. Keep it true.** If you change how Biblio catalogs, acquires, or reads books,
> update this file in the same commit. The per-file index beside it
> ([`biblio-index.md`](biblio-index.md)) is generated — never edit that one.
>
> Verified against `master` (2026-08-07 state — `reader2` is the only reader). Drafted via the
> encyclopedia arc, ground-truthed and adopted by Agent 0. Related guides: [`shell.md`](shell.md),
> [`player.md`](player.md), [`comics.md`](comics.md).

## 1. What this subsystem is for

Find a book, get it onto disk once (download-fed, never streamed), and read it in the **reader2**
EPUB reader — with progress, bookmarks, and annotations that survive restart and even reader swaps.

## 2. The flow — two flows that meet at the book detail page

**Acquire** — catalog → detail → disk:

```
BiblioWorld.qml  (tabs: Discover | Explore | Library; Discover is always the entry tab)
  → BiblioDiscoverPage / BiblioExplorePage  (shared DiscoverBrowser shell; the native
     BiblioCatalog daily snapshot is the source of truth once ready — BiblioWorld.qml:55–72)
  → BiblioBook.qml  (dust-jacket detail = the acquire door)
      LibGen HTTP:  BiblioApi.search → editions (md5) → Books.downloadBook(md5)
                    (BiblioBook.qml:67, 161)
      Torrent:      BookTorrents.search(title, author) → ranked rows →
                    BookTorrents.download(infoHash)      (BiblioBook.qml:103, 490)
  → <appdata>/books/<name>.epub + index.json        (BookDownloader — LibGen)
    or the shared TorrentEngine's download          (BookTorrentDownloader)
  → readRequested(localPath) → Main.qml openBookReader → bookReaderLayer  (Main.qml:1149–1156)
```

**Read** — reader2 (the one reader):

```
bookReaderLayer → reader2/ReaderShell.qml  (native chrome: ReaderChrome, LeftPanel, BottomRail,
  AppearancePanel, SearchSheet, SelectionMenu, DictCard, FootnoteCard, RulerOverlay)
  → Paper.qml — a WebEngineView hosting the vendored Anx foliate fork
     (resources/reader2/paper.html + paper_glue.js)
     commands DOWN → runJavaScript → window.paper.*         (Paper.qml:27–45)
     events   UP   → Reader2PaperGate (registered as "bridge") → Reader2Bridge.paperEventReceived
                     → ReaderShell matches events by per-open gen       (Paper.qml:60–68)
  → every 'relocated' → debounced progressSave → progress.json
     keyed by BookStores::keyFor(path)                      (ReaderShell.qml:30, 58–64)
```

The meeting point: `BiblioBook.readRequested(path)` → `Main.qml:openBookReader` → `ReaderShell.openBook(path)`.

**Audiobook finding (out-of-scope but live):** the standalone audiobook *player UI* was retired
2026-07-18, but the engine is alive and Biblio-shaped: `AudiobookSession` (mpv, one engine at the
window root — Main.qml:2603–2608), `AudiobookDownloader` (AudioBookBay → `<appdata>/audiobooks`),
and the reader's read-along strip + Audio tab. The reader is now "the one audiobook surface"
(Main.qml:1410–1413).

## 3. The files that matter

| File | Role |
|---|---|
| `native/reader2/Reader2Bridge.{h,cpp}` | the reader's native seam: full bridge is a QML context property; the web paper's QWebChannel gets ONLY the nested `Reader2PaperGate` |
| `native/reader/BookStores.{h,cpp}` | shared JSON stores under `<appdata>/book_reader/` + **the ONE key derivation** (`keyFor`, SHA1[:20] of the normalized abs path) |
| `qml/reader2/ReaderShell.qml` | the reader chrome + wiring: gen guard, resume seam, debounced progress saves, panels' view-models |
| `qml/reader2/Paper.qml` | the WebEngineView "paper" wrapper; registers the gate as `bridge`, forwards commands/events |
| `resources/reader2/paper.html` + `paper_glue.js` | the vendored foliate page + glue — **untrusted web content** |
| `qml/reader2/Reader2Logic.js` | pure resume-record logic (progressRecord / resumeCfiOf) — headless-testable |
| `qml/BiblioWorld.qml` | the world: Discover · Explore · Library tabs, featured-carousel hydration from `BiblioCatalog` |
| `qml/BiblioDiscoverPage.qml` / `BiblioExplorePage.qml` / `BiblioLibraryPage.qml` | the three tabs (Discover = shared DiscoverBrowser shell; Library = Collection-driven shelf) |
| `qml/BiblioBook.qml` | the dust-jacket detail page = the acquire door (LibGen editions + torrent rows + read) |
| `native/engine/BookDownloader.{h,cpp}` | the LibGen HTTP download backbone (`Books`): resolve → stream → atomic rename → index |
| `native/engine/BiblioCatalog.{h,cpp}` + `BiblioCatalogStore.{h,cpp}` + `BiblioProviders.{h,cpp}` + `BiblioCanonicalizer.{h,cpp}` + `BiblioRanking.{h,cpp}` | the keyless daily catalogue pipeline: Apple Books + Open Library → canonical works → validated SQLite snapshot |
| `native/torrent/BookTorrents.{h,cpp}` + `BookTorrentRanker.{h,cpp}` + `BookTorrentFilePicker.{h,cpp}` + `BookTorrentMagnet.h` + `BookTorrentDownloader.{h,cpp}` | the torrent acquire path (`BookTorrents` = search+rank+download compose; engine-direct transport) |
| `native/net/BiblioImageDiag.{h,cpp}` | per-URL image-network recorder behind the Lanista `biblio.imageDiag` probe |
| `qml/BiblioApi.js` | libgen search + `pairKey(title, author)` — the book/audiobook pairing identity |

## 4. Where state lives

- **Reader state — `<appdata>/book_reader/`**, owned by `BookStores`: `progress.json`,
  `settings.json`, `bookmarks.json`, `annotations.json`, `display_names.json`. Every entry is keyed
  by **`BookStores::keyFor(path)`** — SHA1[:20] of the path-normalized absolute path — the single
  derivation shared by the old reader and reader2 (zero-migration promise; BookStores.h:22–27).
  QML reaches it through `Reader2Bridge`; the paper never touches these files directly.
  **Consequence: moving or renaming a book file orphans its progress and marks** (the store still
  holds them, keyed by the old fingerprint).
- **Books — `<appdata>/books/`**: `<name>.epub` + `index.json` (`md5 → path, title, bytes, addedAt`),
  owned by `BookDownloader` (BookDownloader.h:23–26). AppData, not the purgeable image cache.
- **Torrents — `<appdata>/torrent-engine/`**: libtorrent session/resume state (main.cpp:834–839).
- **Catalogue — `<appdata>/catalog/biblio-v1.sqlite`**: the writable per-user BiblioCatalog
  snapshot; atomic publish, refreshed at most once per local day; a failed refresh leaves the last
  valid snapshot intact (BiblioCatalog.h:19–24).
- **Audiobook pairing — QSettings** (`AudioPairingStore`): book↔audiobook read-along pairs.
- **What does NOT persist:** the open book *session* (Sessions are in-memory; Continue Watching
  recreates them after restart), and the active Biblio tab — every fresh load starts at Discover
  on purpose (BiblioWorld.qml:29–31).

## 5. Traps

1. **Identity is the path fingerprint, never the raw path.** `BookStores::keyFor` is the ONE
   derivation: `ReaderShell.bookId` (ReaderShell.qml:30), `Reader2Bridge.bookKey`, and the old
   reader's `BookBridge::progressKey` all delegate to it. **Moving/renaming a book orphans its
   progress, bookmarks, and annotations** — they persist, under the old fingerprint.
2. **The paper is untrusted web content; the gate is its entire native surface.** Only
   `Reader2PaperGate` (filesRead + paperEvent) is registered on the web channel (Paper.qml:60).
   Every method you add to the gate is handed to a rigged book. `setAuthorizedBook` runs before
   EVERY open; `filesRead` refuses ("") any other path — and authorization is QML-only, never on
   the channel, so the paper can't authorize itself (Reader2Bridge.h:34–52).
3. **Two stale comments say books are LibGen-only — both are wrong today.** `BookDownloader.h:6–9`
   ("Colosseum has no TorrentClient — its books come from LibGen over HTTP") predates Phase 2: the
   `BookTorrent*` family + embedded TorrentEngine are live (main.cpp:834–845, 880–881) and
   BiblioBook drives both paths. `BiblioBook.qml:3–6` says "The Editions rows are a STUB… the
   download list is a preview" — but the file's own body ships real LibGen search + `downloadBook`
   + torrent rows (BiblioBook.qml:67, 161, 490). Neither comment lies about a live thing; both
   describe an older state.
4. **reader2 ≠ comicreader.** Two things have historically been called "Reader 2". `reader2` is
   Biblio's EPUB reader (foliate paper inside WebEngine); `comicreader` is the comics reader with
   its own `image://` providers. Keep the stores and providers apart.
5. **`native/reader/` is shared storage only — BookBridge is gone.** Deleted 2026-08-07. Mentions
   of `BookBridge` in Reader2Bridge/BookStores comments are deliberate lineage (where the key
   derivation came from), not a live second reader. There is no "reader 1 / reader 2" split.
6. **The paper cannot reach the network, on purpose.** `localContentCanAccessRemoteUrls=false`
   (Paper.qml:87) — a rigged book can't phone home; a book's remote images simply won't load
   (acceptable — offline reader). Dictionary lookups live in C++ (Wiktionary REST, IPv4-pinned,
   one 8s DNS+HTTP budget, ~512 KB cap — Reader2Bridge.h:78–84). Never add raw XHR on the paper
   thread: house rule "QML paints, C++ decides".
7. **The web view owns focus and keyboard.** Page turns and Esc are handled IN-PAGE by
   `paper_glue.js` and emitted up as semantic events; the shell must not fight the view for focus
   (focusPaper/forceActiveFocus, Paper.qml:47–50, 75–80). A selection or double-click that dies is
   usually a focus fight, not a paper bug.
8. **The per-open `gen` guard is what keeps books from bleeding into each other.** QML issues the
   generation; the glue echoes it on book-scoped events; 'ready' is honored only on exact match
   (ReaderShell.qml:42–49). A late event from book A in flight over the channel can't land in book
   B's window. Never drop the gen from a new event path.
9. **Progress saves are debounced (~60ms), with identity stashed at stash time** — so a flush
   after a book switch writes to the right book (ReaderShell.qml:58–64). Tests and teardown paths
   must flush explicitly, not assume the timer fired.
10. **BookDownloader's resolve-then-stream discipline is load-bearing.** LibGen keys rotate ~60s,
    so resolve happens immediately before streaming; a text/html first chunk means a stale key →
    failover; bytes are written to `.part` with an atomic rename; `readAll` is forbidden (books can
    be hundreds of MB) (BookDownloader.h:11–22).
11. **BookTorrents is one-per-book.** Opening book B supersedes A's in-flight search (guard +
    cancel, BookTorrents.h:14–16); `deleteDownload` removes the copy and files; the ranker drops
    audiobooks/video so an audiobook never masquerades as an epub
    (`BookTorrentRanker::isReadableBook`, BookTorrentRanker.h:18–24).
12. **`reader2_harness` reads the REAL books folder even when sandboxed.** Stores redirect under
    QStandardPaths test mode; the book shelf deliberately does not (`--real-stores` flips the
    stores) (reader2_harness_main.cpp:9–17). Don't expect a hermetic shelf from it.

## 6. How to test it

- **ctest:** `colosseum.biblio_catalog_logic_harness` is the one Biblio harness registered in the
  unit set (tests/CMakeLists.txt:35).
- **Native harnesses (build targets, run directly, deterministic + offline):** `reader2_stores_harness`
  (store delegation), `reader2_bridge_harness` (filesRead bytes, store delegation, event relay —
  dictLookup deliberately excluded so it stays offline), `reader2_autoattach_harness`,
  `book_torrent_ranker_harness`, `book_torrent_magnet_harness`, `book_torrent_filepicker_harness`.
- **The full reader seam:** `reader2_harness` exe (native/reader2/reader2_harness_main.cpp) boots
  the real chrome over a real Qt viewport with a book shelf — proof the paper loads and reports
  position ahead of shell integration.
- **QML + JS:** `reader2_logic_harness.qml` (pure resume logic), `reader2_chrome_smoke.qml`,
  `reader2_webengine_rhi_smoke.qml`, `reader2_paper_text_test.mjs`, `biblio_world_harness.qml`,
  `biblio_discover_api_harness.qml`, `biblio_discover_page_harness.qml`,
  `biblio_library_api_harness.qml`, `biblio_library_page_harness.qml`, `biblio_explore_harness.qml`,
  `biblio_pairkey_test.mjs`.
- **Gates:** `test_biblio_discover_explore.ps1`, `test_biblio_explore_index.ps1`,
  `test_biblio_library.ps1`, `test_book_reader_minimize_p0.ps1`, `test_reader2_readalong.ps1`.
- **What it cannot cover:** real paper rendering (needs WebEngine + GPU), live LibGen/torrent
  network behavior, and read-along audio feel — those stay eyes-on with the running app.

## Keeping this page honest

```bash
# refresh the index after editing any source comment
python scripts/code_encyclopedia.py --paths docs/encyclopedia/biblio.paths \
  --output docs/encyclopedia/biblio-index.md --state docs/encyclopedia/biblio-state.json

# gate: fails if a file changed since its description was accepted
python scripts/code_encyclopedia.py ... --check

# after reviewing a changed comment, ratify it
python scripts/code_encyclopedia.py ... --accept <path>
```
