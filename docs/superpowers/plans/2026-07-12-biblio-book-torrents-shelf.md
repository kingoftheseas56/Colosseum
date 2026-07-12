# Biblio Book Torrents Shelf — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a ranked TORRENTS shelf above LibGen on the Biblio book page — live federated torrent search per title, ranked best-match then seeders-desc, one-click single-best-ebook-file download via the Stremio engine into the reader.

**Architecture:** Port TB2's UI-free `TankorentSearchService` + 4 book indexers (HTTP-to-indexers, no libtorrent) into `native/torrent/`. Add two pure, headless-tested helpers — a **ranker** (dedup + match-tier + seeders sort) and a **file picker** (best single ebook file inside a torrent's manifest). Add a Stremio-fed `BookTorrentDownloader` (sibling of the proven `AudiobookDownloader`, single-file). Expose one QML facade `BookTorrents`; render the shelf in `BiblioBook.qml`.

**Tech Stack:** Qt6 (Core/Network/Qml), C++17, CMake (MSVC), QML. Headless verdict-by-exit-code harnesses (house pattern, see `tests/comic_dls_parse_harness.cpp`). Stremio sidecar `StreamServer` (exposed to QML as `Stream`).

**Source of truth for ports:** `~/Desktop/Tankoban 2/src/core/`. **Sibling-of** for the downloader: `native/engine/AudiobookDownloader.{h,cpp}` in THIS repo.

**Build note (house doctrine):** one build per out-dir; a running `colosseum.exe` locks its files — kill by PID before rebuilding. `CMakeLists.txt` is the highest multi-agent collision surface — grep-verify every edit, commit only your own hunks (`git hash-object` + `update-index`, never `git add` the whole file if a brother has unstaged hunks in it).

---

## File Structure

New unit `native/torrent/` (all torrent search + rank + download + facade live together — they change together):

| File | New/Port | Responsibility |
|---|---|---|
| `native/torrent/TorrentResult.h` | Port | The result struct + `humanSize()`/`buildMagnet()`/`canonicalizeInfoHash()` helpers |
| `native/torrent/TorrentIndexer.{h,cpp}` | Port | Abstract indexer base + health/telemetry |
| `native/torrent/PirateBayIndexer.{h,cpp}` | Port | apibay.org JSON indexer |
| `native/torrent/ExtTorrentsIndexer.{h,cpp}` | Port | HTML indexer |
| `native/torrent/TorrentsCsvIndexer.{h,cpp}` | Port | torrents-csv JSON indexer |
| `native/torrent/X1337xIndexer.{h,cpp}` | Port | 1337x HTML indexer |
| `native/torrent/TankorentSearchService.{h,cpp}` | Port | Headless fan-out; books allowlist |
| `native/torrent/BookTorrentRanker.{h,cpp}` | New (pure) | Dedup + match-tier + seeders-desc sort + pack/format flags |
| `native/torrent/BookTorrentFilePicker.{h,cpp}` | New (pure) | Pick the single best ebook file from a manifest |
| `native/torrent/BookTorrentDownloader.{h,cpp}` | New (sibling) | Stremio-fed single-file pull |
| `native/torrent/BookTorrents.{h,cpp}` | New | QML facade composing search + ranker + downloader |
| `native/main.cpp` | Modify | Register `BookTorrents` context property |
| `native/CMakeLists.txt` | Modify | Add torrent sources + two harness executables |
| `qml/BiblioBook.qml` | Modify | TORRENTS shelf above EDITIONS |
| `tests/book_torrent_ranker_harness.cpp` | New | Ranker unit contract |
| `tests/book_torrent_filepicker_harness.cpp` | New | File-picker unit contract |

---

## Task 1: Port the torrent search core into `native/torrent/`

**Files:**
- Create (copy): `native/torrent/TorrentResult.h`, `TorrentIndexer.{h,cpp}`, `PirateBayIndexer.{h,cpp}`, `ExtTorrentsIndexer.{h,cpp}`, `TorrentsCsvIndexer.{h,cpp}`, `X1337xIndexer.{h,cpp}`, `TankorentSearchService.{h,cpp}`
- Modify: `native/CMakeLists.txt:32-61` (the `add_executable(colosseum …)` source list)

- [ ] **Step 1: Copy the source files verbatim from TB2**

```bash
cd "/c/Users/Suprabha/Desktop/Brotherhood/Colosseum"
mkdir -p native/torrent
TB2="/c/Users/Suprabha/Desktop/Tankoban 2/src/core"
cp "$TB2/TorrentResult.h"              native/torrent/
cp "$TB2/TorrentIndexer.h"             native/torrent/
cp "$TB2/TorrentIndexer.cpp"           native/torrent/
cp "$TB2/TankorentSearchService.h"     native/torrent/
cp "$TB2/TankorentSearchService.cpp"   native/torrent/
cp "$TB2/indexers/PirateBayIndexer.h"    native/torrent/
cp "$TB2/indexers/PirateBayIndexer.cpp"  native/torrent/
cp "$TB2/indexers/ExtTorrentsIndexer.h"  native/torrent/
cp "$TB2/indexers/ExtTorrentsIndexer.cpp" native/torrent/
cp "$TB2/indexers/TorrentsCsvIndexer.h"  native/torrent/
cp "$TB2/indexers/TorrentsCsvIndexer.cpp" native/torrent/
cp "$TB2/indexers/X1337xIndexer.h"       native/torrent/
cp "$TB2/indexers/X1337xIndexer.cpp"     native/torrent/
```

- [ ] **Step 2: Flatten include paths (all files now sit in one flat dir)**

TB2 used `#include "core/TorrentResult.h"`, `#include "core/TorrentIndexer.h"`, and `#include "TorrentResult.h"`. In `native/torrent/` everything is a sibling. Rewrite every `core/` include prefix in the copied files to flat:

```bash
cd "/c/Users/Suprabha/Desktop/Brotherhood/Colosseum/native/torrent"
sed -i 's#include "core/TorrentResult.h"#include "TorrentResult.h"#g; s#include "core/TorrentIndexer.h"#include "TorrentIndexer.h"#g; s#include "core/TankorentSearchService.h"#include "TankorentSearchService.h"#g' *.h *.cpp
# indexers referenced their base as "core/TorrentIndexer.h"; some as "../TorrentIndexer.h"
sed -i 's#include "../TorrentIndexer.h"#include "TorrentIndexer.h"#g; s#include "../TorrentResult.h"#include "TorrentResult.h"#g' *.h *.cpp
```

Then grep to confirm no stale prefixes remain (must print nothing):

```bash
grep -nE 'include "(core|\.\.)/' native/torrent/*.h native/torrent/*.cpp
```

- [ ] **Step 3: Drop EZTV/YTS/Nyaa references from the ported search service**

`TankorentSearchService.cpp` `kMediaTypeIndexers` includes video/comic indexers we are NOT porting (`nyaa`, `yts`, `eztv`). `buildIndexersFor()` instantiates concrete indexer classes by id. Keep ONLY the four book indexers. Edit `native/torrent/TankorentSearchService.cpp`:

- In `kMediaTypeIndexers`, keep only:
```cpp
const QHash<QString, QSet<QString>> kMediaTypeIndexers = {
    { "books",      { "piratebay", "exttorrents", "torrentscsv", "1337x" } },
    { "audiobooks", { "piratebay", "exttorrents", "torrentscsv", "1337x" } },
};
```
- In `buildIndexersFor()`, delete the `if (id == "nyaa") …`, `"yts"`, `"eztv"` instantiation branches and their `#include` lines at the top. Keep only the four book-indexer branches + their includes:
```cpp
#include "PirateBayIndexer.h"
#include "ExtTorrentsIndexer.h"
#include "TorrentsCsvIndexer.h"
#include "X1337xIndexer.h"
```

- [ ] **Step 4: Add the ported files to the colosseum target in CMake**

Edit `native/CMakeLists.txt`, inside `add_executable(colosseum …)` (after line ~61, alongside the other `engine/…` entries), add:

```cmake
    torrent/TorrentResult.h
    torrent/TorrentIndexer.cpp
    torrent/TorrentIndexer.h
    torrent/PirateBayIndexer.cpp
    torrent/PirateBayIndexer.h
    torrent/ExtTorrentsIndexer.cpp
    torrent/ExtTorrentsIndexer.h
    torrent/TorrentsCsvIndexer.cpp
    torrent/TorrentsCsvIndexer.h
    torrent/X1337xIndexer.cpp
    torrent/X1337xIndexer.h
    torrent/TankorentSearchService.cpp
    torrent/TankorentSearchService.h
```

- [ ] **Step 5: Build to verify the port compiles**

Run (uses the repo's absolute-path MSVC build — invoking via a relative `//c` path fails, per prior finding):

```bash
cmd //c "C:\Users\Suprabha\Desktop\Brotherhood\Colosseum\native\build-msvc.bat"
```

Expected: `colosseum.exe` links cleanly. If `QNetworkAccessManager`/`QNetworkReply` symbols are unresolved, confirm `native/CMakeLists.txt` already links `Qt6::Network` on the colosseum target (it does — the app uses it); no change needed. Fix any missed `core/` include from Step 2 that only surfaces now.

- [ ] **Step 6: Commit (surgical — only your CMake hunk + new files)**

```bash
cd "/c/Users/Suprabha/Desktop/Brotherhood/Colosseum"
git add native/torrent/ && git add -p native/CMakeLists.txt   # stage ONLY the torrent/ block
git commit -m "feat(biblio): port TB2 Tankorent torrent search core into native/torrent/"
```

---

## Task 2: Indexer live smoke — find which indexers still work

**Files:**
- Modify: `native/torrent/TankorentSearchService.cpp` (add an env-gated headless self-test)
- Modify: `native/main.cpp` (call the self-test at boot when the env var is set, then quit)

- [ ] **Step 1: Add a headless self-test entry point to the search service**

Add to `TankorentSearchService.h` (public):

```cpp
// Dev smoke (env COLOSSEUM_TORRENT_SEARCHTEST="<query>"): run a "books" search
// headless, log per-indexer row counts + total, then emit searchFinished. Reveals
// rotted indexers (dead domain / changed markup) BEFORE they ship silently.
void selfTest(const QString& query);
```

Implement in `TankorentSearchService.cpp`:

```cpp
void TankorentSearchService::selfTest(const QString& query)
{
    const QString handle = startSearch("books", "all", query, 30);
    if (handle.isEmpty()) { qInfo() << "[torrent-smoke] NO indexers matched"; return; }
    connect(this, &TankorentSearchService::resultsReady, this,
            [](const QString&, const QList<TorrentResult>& r){
                qInfo() << "[torrent-smoke] indexer returned" << r.size() << "rows"
                        << (r.isEmpty() ? "" : ("e.g. " + r.first().title));
            });
    connect(this, &TankorentSearchService::indexerError, this,
            [](const QString&, const QString& id, const QString& e){
                qInfo() << "[torrent-smoke] ERROR" << id << e;
            });
    connect(this, &TankorentSearchService::searchFinished, this,
            [](const QString&){ qInfo() << "[torrent-smoke] finished"; });
}
```

- [ ] **Step 2: Wire the boot hook in main.cpp**

In `native/main.cpp`, after the QNetworkAccessManager (`dlNam`) is constructed and before `engine.load(...)`, add:

```cpp
if (qEnvironmentVariableIsSet("COLOSSEUM_TORRENT_SEARCHTEST")) {
    auto *svc = new TankorentSearchService(dlNam, &app);
    svc->selfTest(qEnvironmentVariable("COLOSSEUM_TORRENT_SEARCHTEST"));
    QTimer::singleShot(20000, &app, &QCoreApplication::quit);  // let indexers settle
}
```

Add `#include "torrent/TankorentSearchService.h"` and `#include <QTimer>` at the top of main.cpp if not present.

- [ ] **Step 3: Build**

```bash
cmd //c "C:\Users\Suprabha\Desktop\Brotherhood\Colosseum\native\build-msvc.bat"
```

- [ ] **Step 4: Run the live smoke and record the verdict**

```bash
cd "/c/Users/Suprabha/Desktop/Brotherhood/Colosseum/native/build-msvc"
COLOSSEUM_TORRENT_SEARCHTEST="dune frank herbert" QT_FORCE_STDERR_LOGGING=1 ./colosseum.exe 2>&1 | grep torrent-smoke
```

Expected: one `[torrent-smoke] indexer returned N rows` line per live indexer, `ERROR <id>` for dead ones. **Record which indexers are alive** in the commit message and in the recap. Any indexer that errors or returns 0 across several queries ships **disabled** — note it; do not silently keep a dead lane.

- [ ] **Step 5: Commit**

```bash
git add native/torrent/TankorentSearchService.* native/main.cpp
git commit -m "test(biblio): torrent indexer live smoke (COLOSSEUM_TORRENT_SEARCHTEST) — <alive: ...>"
```

---

## Task 3: `BookTorrentRanker` — pure dedup + rank (TDD)

**Files:**
- Create: `native/torrent/BookTorrentRanker.h`, `native/torrent/BookTorrentRanker.cpp`
- Test: `tests/book_torrent_ranker_harness.cpp`
- Modify: `native/CMakeLists.txt` (new `add_executable(book_torrent_ranker_harness …)` + add ranker to colosseum target)

- [ ] **Step 1: Write the failing test**

Create `tests/book_torrent_ranker_harness.cpp`:

```cpp
// book_torrent_ranker_harness.cpp — rank() contract: dedup by infoHash keeping
// max seeders; sort by match tier desc then seeders desc; pack + format flags.
#include "torrent/BookTorrentRanker.h"
#include <cstdlib>
#include <iostream>

namespace {
void require(bool c, const char* m){ if(!c){ std::cerr<<"FAIL: "<<m<<'\n'; std::exit(1);} }
TorrentResult mk(const QString& title, int seeders, const QString& hash, qint64 size=2*1024*1024){
    TorrentResult r; r.title=title; r.seeders=seeders; r.infoHash=hash; r.sizeBytes=size; return r;
}
}

int main(){
    // 1) Dedup by infoHash keeps the max-seeder copy
    QList<TorrentResult> a{ mk("Dune",10,"a"), mk("Dune",99,"a"), mk("Dune",5,"b") };
    auto r1 = BookTorrentRanker::rank("Dune","Frank Herbert",a);
    require(r1.size()==2, "dedup by infoHash");
    require(r1.first().src.seeders==99, "kept max-seeder copy of the dup");

    // 2) Exact title beats partial regardless of seeders
    QList<TorrentResult> b{ mk("Random Unrelated Book",900,"x"), mk("Dune",3,"y") };
    auto r2 = BookTorrentRanker::rank("Dune","Frank Herbert",b);
    require(r2.first().src.infoHash=="y", "exact-title match outranks higher-seed partial");

    // 3) Same tier -> seeders desc
    QList<TorrentResult> c{ mk("Dune epub",7,"p"), mk("Dune epub",50,"q") };
    auto r3 = BookTorrentRanker::rank("Dune","",c);
    require(r3.first().src.seeders==50, "within a tier, most seeders first");

    // 4) Article-insensitive match ("The Hobbit" ~ "Hobbit")
    QList<TorrentResult> d{ mk("Hobbit",4,"h") };
    auto r4 = BookTorrentRanker::rank("The Hobbit","",d);
    require(r4.first().matchTier >= 3, "leading-article stripped for matching");

    // 5) Pack flagged for a big collection title
    QList<TorrentResult> e{ mk("Sci-Fi EPUB Collection 5000 books",800,"c",40LL*1024*1024*1024) };
    auto r5 = BookTorrentRanker::rank("Dune","",e);
    require(r5.first().pack, "big collection flagged as pack");

    // 6) Format guessed from the title suffix
    QList<TorrentResult> f{ mk("Dune.epub",4,"z") };
    auto r6 = BookTorrentRanker::rank("Dune","",f);
    require(r6.first().formatGuess=="EPUB", "format guessed from .epub in title");

    std::cout<<"book_torrent_ranker_harness PASS\n"; return 0;
}
```

- [ ] **Step 2: Add the harness to CMake and run it to verify it FAILS (no ranker yet)**

Add to `native/CMakeLists.txt` (after the `comic_dls_parse_harness` block ~line 103):

```cmake
add_executable(book_torrent_ranker_harness
    ../tests/book_torrent_ranker_harness.cpp
    torrent/BookTorrentRanker.cpp
    torrent/BookTorrentRanker.h
)
target_include_directories(book_torrent_ranker_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(book_torrent_ranker_harness PRIVATE Qt6::Core)
```

Run:
```bash
cmd //c "C:\Users\Suprabha\Desktop\Brotherhood\Colosseum\native\build-msvc.bat"
```
Expected: **build FAILS** — `BookTorrentRanker.h` not found. That's the red state.

- [ ] **Step 3: Write the minimal implementation**

Create `native/torrent/BookTorrentRanker.h`:

```cpp
#pragma once
#include "TorrentResult.h"
#include <QList>
#include <QString>

struct RankedTorrent {
    TorrentResult src;
    int     matchTier = 0;   // 4 exact · 3 prefix · 2 all-tokens · 1 partial · 0 none
    bool    pack = false;
    QString formatGuess;     // "EPUB"/"PDF"/"MOBI"/"AZW3"/"DJVU"/"FB2"/"EBOOK"
};

class BookTorrentRanker {
public:
    // Dedup by canonical infoHash (keep max seeders), score each row, sort:
    // matchTier desc, then seeders desc. Rows with no infoHash are kept as-is
    // (deduped by lowercased title instead).
    static QList<RankedTorrent> rank(const QString& title, const QString& author,
                                     const QList<TorrentResult>& raw);
    // exposed for tests
    static QString stripArticles(QString s);
    static int     matchTier(const QString& title, const QString& author, const QString& candidate);
    static bool    looksLikePack(const QString& title, qint64 sizeBytes);
    static QString guessFormat(const QString& title);
};
```

Create `native/torrent/BookTorrentRanker.cpp`:

```cpp
#include "BookTorrentRanker.h"
#include <QHash>
#include <QRegularExpression>
#include <algorithm>

static QString norm(QString s){
    s = s.toLower();
    s.replace(QRegularExpression("[._\\-]+"), " ");
    s.replace(QRegularExpression("[^a-z0-9 ]"), "");
    s.replace(QRegularExpression("\\s+"), " ");
    return s.trimmed();
}

QString BookTorrentRanker::stripArticles(QString s){
    s = norm(s);
    static const QRegularExpression lead("^(the|a|an)\\s+");
    s.remove(lead);
    return s;
}

int BookTorrentRanker::matchTier(const QString& title, const QString& author, const QString& candidate){
    const QString t = stripArticles(title);
    const QString c = stripArticles(candidate);
    if (t.isEmpty()) return 0;
    if (c == t) return 4;
    if (c.startsWith(t)) return 3;
    // all title tokens present?
    const QStringList toks = t.split(' ', Qt::SkipEmptyParts);
    bool all = !toks.isEmpty();
    for (const auto& tok : toks) if (!c.contains(tok)) { all = false; break; }
    if (all) {
        // author present too is still tier 2 here; author only sharpens ties via seeders
        Q_UNUSED(author);
        return 2;
    }
    // any token present -> partial
    for (const auto& tok : toks) if (c.contains(tok)) return 1;
    return 0;
}

bool BookTorrentRanker::looksLikePack(const QString& title, qint64 sizeBytes){
    const QString t = title.toLower();
    static const QRegularExpression packWords(
        "\\b(collection|collections|pack|library|anthology|omnibus|bundle|books|volumes?|\\d{2,}\\s*(books|epubs|ebooks))\\b");
    if (packWords.match(t).hasMatch()) return true;
    return sizeBytes > 60LL * 1024 * 1024;   // > 60MB is not a lone ebook
}

QString BookTorrentRanker::guessFormat(const QString& title){
    const QString t = title.toLower();
    struct { const char* ext; const char* label; } m[] = {
        {"epub","EPUB"},{"azw3","AZW3"},{"azw","AZW"},{"mobi","MOBI"},
        {"pdf","PDF"},{"djvu","DJVU"},{"fb2","FB2"}
    };
    for (auto& e : m) if (t.contains(QString(".")+e.ext) || t.contains(QString(" ")+e.ext)) return e.label;
    return "EBOOK";
}

QList<RankedTorrent> BookTorrentRanker::rank(const QString& title, const QString& author,
                                             const QList<TorrentResult>& raw){
    // dedup: infoHash if present, else normalized title
    QHash<QString, TorrentResult> best;
    for (const auto& r : raw) {
        const QString key = !r.infoHash.isEmpty() ? r.infoHash.toLower() : ("t:" + norm(r.title));
        auto it = best.find(key);
        if (it == best.end() || r.seeders > it.value().seeders) best.insert(key, r);
    }
    QList<RankedTorrent> out;
    for (const auto& r : best) {
        RankedTorrent rt;
        rt.src = r;
        rt.matchTier = matchTier(title, author, r.title);
        rt.pack = looksLikePack(r.title, r.sizeBytes);
        rt.formatGuess = guessFormat(r.title);
        out.push_back(rt);
    }
    std::sort(out.begin(), out.end(), [](const RankedTorrent& a, const RankedTorrent& b){
        if (a.matchTier != b.matchTier) return a.matchTier > b.matchTier;   // best match first
        return a.src.seeders > b.src.seeders;                               // then most seeders
    });
    return out;
}
```

- [ ] **Step 4: Build and run the harness — verify PASS**

```bash
cmd //c "C:\Users\Suprabha\Desktop\Brotherhood\Colosseum\native\build-msvc.bat"
cd "/c/Users/Suprabha/Desktop/Brotherhood/Colosseum/native/build-msvc" && ./book_torrent_ranker_harness.exe
```
Expected: `book_torrent_ranker_harness PASS`, exit 0.

- [ ] **Step 5: Add the ranker to the colosseum target too**

In `native/CMakeLists.txt` `add_executable(colosseum …)`, add:
```cmake
    torrent/BookTorrentRanker.cpp
    torrent/BookTorrentRanker.h
```

- [ ] **Step 6: Commit**

```bash
git add native/torrent/BookTorrentRanker.* tests/book_torrent_ranker_harness.cpp && git add -p native/CMakeLists.txt
git commit -m "feat(biblio): BookTorrentRanker — dedup + match-tier + seeders-desc (headless-tested)"
```

---

## Task 4: `BookTorrentFilePicker` — pick the single best ebook file (TDD)

**Files:**
- Create: `native/torrent/BookTorrentFilePicker.h`, `native/torrent/BookTorrentFilePicker.cpp`
- Test: `tests/book_torrent_filepicker_harness.cpp`
- Modify: `native/CMakeLists.txt` (harness executable + add to colosseum target)

- [ ] **Step 1: Write the failing test**

Create `tests/book_torrent_filepicker_harness.cpp`:

```cpp
// book_torrent_filepicker_harness.cpp — pick() contract: choose the single best
// ebook file inside a torrent manifest; honest -1 when no ebook file exists.
#include "torrent/BookTorrentFilePicker.h"
#include <cstdlib>
#include <iostream>

namespace {
void require(bool c, const char* m){ if(!c){ std::cerr<<"FAIL: "<<m<<'\n'; std::exit(1);} }
ManifestFile mf(int idx, const QString& name, qint64 len=2*1024*1024){ return ManifestFile{idx,name,len}; }
}

int main(){
    // 1) Picks the title-matching epub over an unrelated pdf
    QList<ManifestFile> a{ mf(0,"readme.txt"), mf(1,"Dune - Frank Herbert.epub"), mf(2,"Some Other Book.pdf") };
    auto p1 = BookTorrentFilePicker::pick("Dune","Frank Herbert",a);
    require(p1.idx==1, "picks the matching epub");
    require(p1.ext=="epub", "records ext");

    // 2) No ebook files -> honest -1
    QList<ManifestFile> b{ mf(0,"cover.jpg"), mf(1,"metadata.opf") };
    auto p2 = BookTorrentFilePicker::pick("Dune","",b);
    require(p2.idx==-1, "no ebook file -> -1");

    // 3) On equal name match, epub beats pdf
    QList<ManifestFile> c{ mf(0,"Dune.pdf"), mf(1,"Dune.epub") };
    auto p3 = BookTorrentFilePicker::pick("Dune","",c);
    require(p3.idx==1, "epub preferred over pdf on equal match");

    // 4) Inside a pack, picks the one titled file, not the biggest
    QList<ManifestFile> d{ mf(0,"Asimov - Foundation.epub",3*1024*1024),
                           mf(1,"Herbert - Dune.epub",2*1024*1024),
                           mf(2,"Tolkien - LOTR.epub",9*1024*1024) };
    auto p4 = BookTorrentFilePicker::pick("Dune","Frank Herbert",d);
    require(p4.idx==1, "matches the requested title inside a pack");

    std::cout<<"book_torrent_filepicker_harness PASS\n"; return 0;
}
```

- [ ] **Step 2: Add harness to CMake and run to verify it FAILS**

Add to `native/CMakeLists.txt`:
```cmake
add_executable(book_torrent_filepicker_harness
    ../tests/book_torrent_filepicker_harness.cpp
    torrent/BookTorrentFilePicker.cpp
    torrent/BookTorrentFilePicker.h
)
target_include_directories(book_torrent_filepicker_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(book_torrent_filepicker_harness PRIVATE Qt6::Core)
```
Run the build; expected FAIL: `BookTorrentFilePicker.h` not found.

- [ ] **Step 3: Write the minimal implementation**

Create `native/torrent/BookTorrentFilePicker.h`:

```cpp
#pragma once
#include <QList>
#include <QString>

struct ManifestFile { int idx = 0; QString name; qint64 length = 0; };
struct PickedFile   { int idx = -1; QString name; QString ext; };

class BookTorrentFilePicker {
public:
    // Choose the single best ebook file for {title, author}. Returns idx == -1
    // when the manifest holds no ebook file (caller fails honestly).
    static PickedFile pick(const QString& title, const QString& author,
                           const QList<ManifestFile>& files);
    static bool isEbook(const QString& name);     // ext in the ebook set
    static int  formatRank(const QString& ext);    // higher = preferred (epub best)
    static QString extOf(const QString& name);
};
```

Create `native/torrent/BookTorrentFilePicker.cpp`:

```cpp
#include "BookTorrentFilePicker.h"
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>

static QString normName(QString s){
    s = s.toLower();
    s.replace(QRegularExpression("[._\\-]+"), " ");
    s.replace(QRegularExpression("[^a-z0-9 ]"), "");
    return s.replace(QRegularExpression("\\s+"), " ").trimmed();
}

QString BookTorrentFilePicker::extOf(const QString& name){
    return QFileInfo(name).suffix().toLower();
}

bool BookTorrentFilePicker::isEbook(const QString& name){
    static const QSet<QString> exts{"epub","pdf","mobi","azw3","azw","djvu","fb2"};
    return exts.contains(extOf(name));
}

int BookTorrentFilePicker::formatRank(const QString& ext){
    if (ext=="epub") return 6;
    if (ext=="azw3"||ext=="azw"||ext=="mobi") return 5;
    if (ext=="fb2") return 4;
    if (ext=="pdf") return 3;
    if (ext=="djvu") return 2;
    return 0;
}

PickedFile BookTorrentFilePicker::pick(const QString& title, const QString& author,
                                       const QList<ManifestFile>& files){
    const QStringList want = normName(title + " " + author).split(' ', Qt::SkipEmptyParts);
    PickedFile best;                 // idx == -1 by default
    int bestScore = -1, bestFmt = -1;
    qint64 bestLen = 0;
    for (const auto& f : files) {
        if (!isEbook(f.name)) continue;
        const QString n = normName(f.name);
        int overlap = 0;
        for (const auto& w : want) if (n.contains(w)) ++overlap;
        const int fmt = formatRank(extOf(f.name));
        // rank: title overlap, then format preference, then smaller file (a lone book, not the biggest)
        const bool better =
            overlap > bestScore ||
            (overlap == bestScore && fmt > bestFmt) ||
            (overlap == bestScore && fmt == bestFmt && (bestLen == 0 || f.length < bestLen));
        if (better) { best.idx=f.idx; best.name=f.name; best.ext=extOf(f.name);
                      bestScore=overlap; bestFmt=fmt; bestLen=f.length; }
    }
    return best;
}
```

- [ ] **Step 4: Build and run — verify PASS**

```bash
cmd //c "C:\Users\Suprabha\Desktop\Brotherhood\Colosseum\native\build-msvc.bat"
cd "/c/Users/Suprabha/Desktop/Brotherhood/Colosseum/native/build-msvc" && ./book_torrent_filepicker_harness.exe
```
Expected: `book_torrent_filepicker_harness PASS`.

- [ ] **Step 5: Add the picker to the colosseum target**

In `add_executable(colosseum …)` add:
```cmake
    torrent/BookTorrentFilePicker.cpp
    torrent/BookTorrentFilePicker.h
```

- [ ] **Step 6: Commit**

```bash
git add native/torrent/BookTorrentFilePicker.* tests/book_torrent_filepicker_harness.cpp && git add -p native/CMakeLists.txt
git commit -m "feat(biblio): BookTorrentFilePicker — single best ebook file, honest empty (headless-tested)"
```

---

## Task 5: `BookTorrentDownloader` — Stremio-fed single-file pull (sibling)

**Files:**
- Create: `native/torrent/BookTorrentDownloader.h`, `native/torrent/BookTorrentDownloader.cpp`
- Modify: `native/CMakeLists.txt` (add to colosseum target)

This is a **sibling of `native/engine/AudiobookDownloader.{h,cpp}`** — copy it, then apply the edits below. Do NOT modify `AudiobookDownloader.*` (it was hardened under eyes-on 2026-07-12; keep it untouched).

- [ ] **Step 1: Copy the audiobook downloader as the starting point**

```bash
cd "/c/Users/Suprabha/Desktop/Brotherhood/Colosseum"
cp native/engine/AudiobookDownloader.h   native/torrent/BookTorrentDownloader.h
cp native/engine/AudiobookDownloader.cpp native/torrent/BookTorrentDownloader.cpp
```

- [ ] **Step 2: Rewrite the header for the book, single-file, infoHash-keyed shape**

Replace `native/torrent/BookTorrentDownloader.h` with:

```cpp
// BookTorrentDownloader.h
//
// The torrent-ebook half of the download-fed backbone. Sibling of
// AudiobookDownloader: same proven Stremio transport (prefetch -> /create manifest
// -> stream-to-disk with the cold-engine watchdog), but pulls the SINGLE best ebook
// file (via BookTorrentFilePicker) and keys everything by infoHash.
//
// On-disk: <appdata>/books-torrent/<infoHash>/<name>.<ext> + .../index.json.

#pragma once
#include <QObject>
#include <QHash>
#include <QPointer>
#include <QString>
#include <QVariantMap>

class QNetworkAccessManager;
class QNetworkReply;
class QFile;
class StreamServer;

class BookTorrentDownloader : public QObject {
    Q_OBJECT
public:
    BookTorrentDownloader(QNetworkAccessManager* nam, StreamServer* stream, QObject* parent = nullptr);
    ~BookTorrentDownloader() override;

    // Resolve the torrent's files via the Stream engine, pick the best ebook file,
    // download just that one. Idempotent: a done infoHash re-emits finished(); an
    // active infoHash is a no-op.
    Q_INVOKABLE void download(const QString& infoHash, const QString& title, const QString& author);
    Q_INVOKABLE QString localFile(const QString& infoHash) const;   // path or ""
    Q_INVOKABLE bool    isDownloaded(const QString& infoHash) const;
    Q_INVOKABLE QVariantMap statusOf(const QString& infoHash) const; // {state,received,total}
    Q_INVOKABLE void cancelDownload(const QString& infoHash);

    void selfTest(const QString& infoHash, const QString& title); // COLOSSEUM_TORRENT_DLTEST

signals:
    void resolving(const QString& infoHash);
    void progress(const QString& infoHash, double received, double total);
    void finished(const QString& infoHash, const QString& path);
    void failed(const QString& infoHash, const QString& reason);

private:
    struct Job {
        QString infoHash, title, author;
        QString baseUrl;                 // http://127.0.0.1:<port>/<infoHash>
        int     pickedIdx = -1;          // chosen file index in the manifest
        QString fileName, ext;
        qint64  totalBytes = 0, received = 0;
        int     enginePolls = 0, createAttempts = 0;
        QPointer<QNetworkReply> reply;
        QFile*  file = nullptr;
        QString finalPath, partPath;
    };

    void onFetchReady(const QString& url, const QString& infoHash, int fileIdx);
    void beginManifest(Job* job, const QString& url);
    void pollEngine(Job* job);
    void requestManifest(Job* job);
    void onManifestReply(QNetworkReply* reply, Job* job);
    void startFile(Job* job);
    void onFileReadyRead();
    void onFileFinished();
    void finalizeJob(Job* job);
    void failJob(Job* job, const QString& reason);
    void cleanupInFlight(Job* job);

    Job* jobForHash(const QString& infoHash) const;
    QString baseDir() const;
    QString dirFor(const QString& infoHash) const;
    void loadIndex(); void saveIndex() const;

    struct Entry { QString path, title, author; qint64 bytes=0, addedAt=0; };

    QNetworkAccessManager* m_nam = nullptr;
    StreamServer* m_stream = nullptr;
    QHash<QString, Job*> m_active;       // infoHash -> job
    QHash<QString, Entry> m_index;       // infoHash -> entry
};
```

- [ ] **Step 3: Adapt the .cpp — reuse the transport, change 4 things**

Edit `native/torrent/BookTorrentDownloader.cpp`. Keep the engine handshake verbatim from AudiobookDownloader (the `prefetch` → `onFetchReady` → `beginManifest` → `pollEngine` watchdog → `requestManifest` → `/create` POST + timeout/retry). Apply these concrete changes:

1. **Includes + rename:** change `#include "AudiobookDownloader.h"` → `#include "BookTorrentDownloader.h"`; add `#include "BookTorrentFilePicker.h"` and `#include "player/streamserver.h"`. Replace every `AudiobookDownloader::` with `BookTorrentDownloader::`. Delete audiobook-only methods (`downloadedAudiobooks`, `deleteAudiobook`, `localFiles`, `localAudiobook`, `promoteQueue`, the queue).

2. **Key by infoHash, not pairKey.** The entry point becomes:
```cpp
void BookTorrentDownloader::download(const QString& infoHash, const QString& title, const QString& author){
    if (isDownloaded(infoHash)) { emit finished(infoHash, m_index.value(infoHash).path); return; }
    if (m_active.contains(infoHash)) return;                 // no-op if in flight
    auto* job = new Job{}; job->infoHash=infoHash; job->title=title; job->author=author;
    m_active.insert(infoHash, job);
    emit resolving(infoHash);
    connect(m_stream, &StreamServer::fetchReady, this, &BookTorrentDownloader::onFetchReady, Qt::UniqueConnection);
    m_stream->prefetch(infoHash, 0);
    pollEngine(job);                                         // watchdog (fetchReady can be lost cold)
}
```

3. **Parse the manifest, then pick ONE file** (replace the audio-filter + natural-sort + multi-file loop). In `onManifestReply`, after JSON parse into `QList<ManifestFile>`:
```cpp
QList<ManifestFile> mfs;
const auto arr = doc.object().value("files").toArray();
for (int i=0;i<arr.size();++i){ const auto o=arr[i].toObject();
    mfs.push_back({ i, o.value("name").toString(), (qint64)o.value("length").toDouble() }); }
const PickedFile pick = BookTorrentFilePicker::pick(job->title, job->author, mfs);
if (pick.idx < 0) { failJob(job, "no ebook file in torrent"); return; }
job->pickedIdx = pick.idx; job->fileName = pick.name; job->ext = pick.ext;
job->totalBytes = 0;
for (const auto& m : mfs) if (m.idx==pick.idx) job->totalBytes = m.length;
startFile(job);
```

4. **Stream just the picked file** to the books-torrent dir (replace `startNextFile`/`finalizeJob` multi-file logic with single-file):
```cpp
void BookTorrentDownloader::startFile(Job* job){
    QDir().mkpath(dirFor(job->infoHash));
    const QString safe = QFileInfo(job->fileName).fileName();
    job->finalPath = dirFor(job->infoHash) + "/" + safe;
    job->partPath  = job->finalPath + ".part";
    job->file = new QFile(job->partPath);
    job->file->open(QIODevice::WriteOnly | QIODevice::Truncate);
    const QString url = job->baseUrl + "/" + QString::number(job->pickedIdx);
    QNetworkRequest req{QUrl(url)};
    job->reply = m_nam->get(req);                            // plain GET, no Range -> whole file
    connect(job->reply, &QNetworkReply::readyRead, this, &BookTorrentDownloader::onFileReadyRead);
    connect(job->reply, &QNetworkReply::finished,  this, &BookTorrentDownloader::onFileFinished);
}
void BookTorrentDownloader::finalizeJob(Job* job){
    job->file->close(); QFile::remove(job->finalPath); QFile::rename(job->partPath, job->finalPath);
    m_index.insert(job->infoHash, Entry{ job->finalPath, job->title, job->author, job->totalBytes, 0 });
    saveIndex();
    emit finished(job->infoHash, job->finalPath);
    m_active.remove(job->infoHash); cleanupInFlight(job); delete job;
}
```
(`onFileReadyRead` writes chunks + emits `progress(infoHash, received, totalBytes)`; `onFileFinished` calls `finalizeJob` on success or `failJob` on error — same bodies as the audiobook version with `pairKey`→`infoHash` and the single-file early-out. `baseDir()` returns `<appdata>/books-torrent`; `dirFor` returns `baseDir()+"/"+infoHash`.)

5. **selfTest:** mirror `AudiobookDownloader::selfTest` — parse `COLOSSEUM_TORRENT_DLTEST="<infoHash>|<title>"`, call `download(hash, title, "")`, log manifest + picked file + final path on `finished`/`failed`.

- [ ] **Step 4: Add the boot hook for the download smoke in main.cpp**

In `native/main.cpp`, near the search smoke hook from Task 2:
```cpp
if (qEnvironmentVariableIsSet("COLOSSEUM_TORRENT_DLTEST")) {
    const QStringList a = qEnvironmentVariable("COLOSSEUM_TORRENT_DLTEST").split('|');
    auto* dl = new BookTorrentDownloader(dlNam, stream, &app);
    if (a.size()==2) dl->selfTest(a[0], a[1]);
    QTimer::singleShot(120000, &app, &QCoreApplication::quit);
}
```
Add `#include "torrent/BookTorrentDownloader.h"`.

- [ ] **Step 5: Add to CMake, build**

In `add_executable(colosseum …)` add:
```cmake
    torrent/BookTorrentDownloader.cpp
    torrent/BookTorrentDownloader.h
```
Build:
```bash
cmd //c "C:\Users\Suprabha\Desktop\Brotherhood\Colosseum\native\build-msvc.bat"
```

- [ ] **Step 6: Live download smoke**

Use a healthy infoHash surfaced by the Task 2 search smoke (a single-book torrent with seeders):
```bash
cd "/c/Users/Suprabha/Desktop/Brotherhood/Colosseum/native/build-msvc"
COLOSSEUM_TORRENT_DLTEST="<infoHash>|dune" QT_FORCE_STDERR_LOGGING=1 ./colosseum.exe 2>&1 | grep -iE 'torrent|manifest|picked|finished|fail'
ls -la "$APPDATA/colosseum/books-torrent/" 2>/dev/null || ls -la ~/AppData/Roaming/*/books-torrent/ 2>/dev/null
```
Expected: manifest logged, one ebook file picked, a real file on disk under `books-torrent/<infoHash>/`. If the engine is cold it may take up to a minute (watchdog) — that's the known cold-DHT behavior, not a bug.

- [ ] **Step 7: Commit**

```bash
git add native/torrent/BookTorrentDownloader.* native/main.cpp && git add -p native/CMakeLists.txt
git commit -m "feat(biblio): BookTorrentDownloader — Stremio-fed single-best-file pull (live-proven)"
```

---

## Task 6: `BookTorrents` facade + main.cpp registration

**Files:**
- Create: `native/torrent/BookTorrents.h`, `native/torrent/BookTorrents.cpp`
- Modify: `native/main.cpp` (register context property), `native/CMakeLists.txt`

- [ ] **Step 1: Write the facade header**

Create `native/torrent/BookTorrents.h`:

```cpp
#pragma once
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

class QNetworkAccessManager;
class StreamServer;
class TankorentSearchService;
class BookTorrentDownloader;

// One QML-facing object (`BookTorrents`) composing search + rank + download.
class BookTorrents : public QObject {
    Q_OBJECT
public:
    // searchNam: IPv4-pinned CachingNam for indexer HTTP. dlNam: uncached NAM for bytes.
    BookTorrents(QNetworkAccessManager* searchNam, QNetworkAccessManager* dlNam,
                 StreamServer* stream, QObject* parent = nullptr);

    Q_INVOKABLE void search(const QString& title, const QString& author);
    Q_INVOKABLE void download(const QString& infoHash, const QString& title, const QString& author);
    Q_INVOKABLE bool    isDownloaded(const QString& infoHash) const;
    Q_INVOKABLE QString localFile(const QString& infoHash) const;
    Q_INVOKABLE QVariantMap statusOf(const QString& infoHash) const;

signals:
    void resultsReady(const QVariantList& rankedRows);  // [{title,infoHash,seeders,size,format,pack}]
    void searchFinished();
    // re-broadcast downloader signals so QML rows can bind by infoHash
    void resolving(const QString& infoHash);
    void progress(const QString& infoHash, double received, double total);
    void finished(const QString& infoHash, const QString& path);
    void failed(const QString& infoHash, const QString& reason);

private:
    TankorentSearchService* m_search;
    BookTorrentDownloader*  m_dl;
    QString m_handle, m_title, m_author;   // per-search state; the result accumulator is lambda-local in .cpp
};
```

- [ ] **Step 2: Write the facade .cpp**

Create `native/torrent/BookTorrents.cpp`:

```cpp
#include "BookTorrents.h"
#include "TankorentSearchService.h"
#include "BookTorrentDownloader.h"
#include "BookTorrentRanker.h"
#include "TorrentResult.h"
#include <QVariantMap>

BookTorrents::BookTorrents(QNetworkAccessManager* searchNam, QNetworkAccessManager* dlNam,
                           StreamServer* stream, QObject* parent)
    : QObject(parent),
      m_search(new TankorentSearchService(searchNam, this)),
      m_dl(new BookTorrentDownloader(dlNam, stream, this))
{
    // re-broadcast downloader signals verbatim
    connect(m_dl, &BookTorrentDownloader::resolving, this, &BookTorrents::resolving);
    connect(m_dl, &BookTorrentDownloader::progress,  this, &BookTorrents::progress);
    connect(m_dl, &BookTorrentDownloader::finished,  this, &BookTorrents::finished);
    connect(m_dl, &BookTorrentDownloader::failed,    this, &BookTorrents::failed);
}

void BookTorrents::search(const QString& title, const QString& author){
    m_title = title; m_author = author;
    auto* accum = new QList<TorrentResult>();
    m_handle = m_search->startSearch("books", "all", title + " " + author, 30);
    if (m_handle.isEmpty()) { emit resultsReady({}); emit searchFinished(); delete accum; return; }

    connect(m_search, &TankorentSearchService::resultsReady, this,
        [this, accum](const QString& h, const QList<TorrentResult>& r){
            if (h != m_handle) return; *accum += r; }, Qt::UniqueConnection);
    connect(m_search, &TankorentSearchService::searchFinished, this,
        [this, accum](const QString& h){
            if (h != m_handle) return;
            const auto ranked = BookTorrentRanker::rank(m_title, m_author, *accum);
            QVariantList rows;
            for (const auto& rt : ranked) {
                QVariantMap m;
                m["title"]=rt.src.title; m["infoHash"]=rt.src.infoHash;
                m["seeders"]=rt.src.seeders; m["sizeBytes"]=(double)rt.src.sizeBytes;
                m["size"]=humanSize(rt.src.sizeBytes);
                m["format"]=rt.formatGuess; m["pack"]=rt.pack; m["source"]=rt.src.sourceName;
                if (!rt.src.infoHash.isEmpty()) rows.push_back(m);   // undownloadable without a hash
            }
            emit resultsReady(rows); emit searchFinished(); delete accum; }, Qt::UniqueConnection);
}

void BookTorrents::download(const QString& infoHash, const QString& title, const QString& author){
    m_dl->download(infoHash, title, author);
}
bool    BookTorrents::isDownloaded(const QString& h) const { return m_dl->isDownloaded(h); }
QString BookTorrents::localFile(const QString& h) const    { return m_dl->localFile(h); }
QVariantMap BookTorrents::statusOf(const QString& h) const { return m_dl->statusOf(h); }
```

(The accumulator is a local `QList<TorrentResult>*` captured in the search lambdas — the header carries no `m_accum` member.)

- [ ] **Step 3: Register the context property in main.cpp**

In `native/main.cpp`, right after the `Audiobooks` registration (`~line 358`), add:

```cpp
// Book torrents shelf: federated indexer search + Stremio-fed single-file pull.
// searchNam pins indexer hosts to IPv4 (21s-stall doctrine); dlNam carries bytes.
auto *bookTorrents = new BookTorrents(nam /*CachingNam, IPv4-pinned*/, dlNam, stream, &app);
engine.rootContext()->setContextProperty(QStringLiteral("BookTorrents"), bookTorrents);
```

Add `#include "torrent/BookTorrents.h"`. Use the app's existing IPv4-pinning `nam` (the `CachingNam` already built in main.cpp) as `searchNam`; if an indexer host stalls on IPv6, add its host to the pin list where `CachingNam` is constructed.

- [ ] **Step 4: Add to CMake + build**

In `add_executable(colosseum …)` add:
```cmake
    torrent/BookTorrents.cpp
    torrent/BookTorrents.h
```
Build:
```bash
cmd //c "C:\Users\Suprabha\Desktop\Brotherhood\Colosseum\native\build-msvc.bat"
```
Expected: clean link. Boot the app once (no crash, no QML error about `BookTorrents`):
```bash
cd "/c/Users/Suprabha/Desktop/Brotherhood/Colosseum/native/build-msvc" && QT_FORCE_STDERR_LOGGING=1 ./colosseum.exe 2>&1 | grep -iE 'error|BookTorrents' | head
```

- [ ] **Step 5: Commit**

```bash
git add native/torrent/BookTorrents.* native/main.cpp && git add -p native/CMakeLists.txt
git commit -m "feat(biblio): BookTorrents facade + QML registration (search+rank+download)"
```

---

## Task 7: TORRENTS shelf in `BiblioBook.qml`

**Files:**
- Modify: `qml/BiblioBook.qml` (add properties + `loadTorrents()`; insert the shelf before the EDITIONS block at ~line 353)

- [ ] **Step 1: Add state properties + the loader**

In `BiblioBook.qml`, add near the other `detail` properties (by `property var editions: []` ~line 19):

```qml
property var torrents: []              // ranked rows from BookTorrents (native order — no re-sort)
property bool torLoading: false
```

In `loadEditions()` (~line 64), add a sibling call at the end of the function body:

```qml
detail.loadTorrents()
```

Add the function right after `loadEditions()`:

```qml
function loadTorrents() {
    if (typeof BookTorrents === 'undefined' || !detail.book) return
    detail.torrents = []
    detail.torLoading = true
    BookTorrents.search(detail.book.title, detail.book.author || "")
}
Connections {
    target: (typeof BookTorrents !== 'undefined') ? BookTorrents : null
    function onResultsReady(rows) { detail.torrents = rows; detail.torLoading = false }
    function onSearchFinished()   { detail.torLoading = false }
}
```

- [ ] **Step 2: Insert the shelf UI directly above the EDITIONS block**

In `BiblioBook.qml`, immediately BEFORE the `// ── Editions ──` comment (~line 353, before `Item { width: 1; height: 40 }`), insert:

```qml
                // ── Torrents — live federated indexer search; ranked best-match × seeders ──
                Item { width: 1; height: 40; visible: typeof BookTorrents !== 'undefined' }
                Text {
                    visible: typeof BookTorrents !== 'undefined'
                    text: "TORRENTS" + (detail.torLoading ? "  ·  SEARCHING…"
                          : (detail.torrents.length > 0 ? "  ·  " + detail.torrents.length : "  ·  NONE"))
                    color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 12
                    font.weight: Font.DemiBold; font.letterSpacing: 1.6
                }
                Item { width: 1; height: 12; visible: typeof BookTorrents !== 'undefined' }
                Glass {
                    visible: typeof BookTorrents !== 'undefined'
                    backdrop: detail.backdrop
                    width: Math.min(parent.width, 640); radius: 14
                    height: torCol.implicitHeight
                    Column {
                        id: torCol
                        width: parent.width
                        Item {                                   // loading / empty
                            visible: detail.torLoading || detail.torrents.length === 0
                            width: parent.width; height: 52
                            Text {
                                anchors.left: parent.left; anchors.leftMargin: 18
                                anchors.verticalCenter: parent.verticalCenter
                                text: detail.torLoading ? "Searching torrents…" : "No torrents found"
                                color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 13
                            }
                        }
                        Repeater {
                            model: detail.torrents
                            delegate: Item {
                                id: torRow
                                required property var modelData
                                required property int index
                                width: parent.width; height: 52
                                property string dlState:
                                    (typeof BookTorrents !== 'undefined' && BookTorrents.isDownloaded(modelData.infoHash)) ? "done" : "idle"
                                property real dlPct: 0
                                Connections {
                                    target: (typeof BookTorrents !== 'undefined') ? BookTorrents : null
                                    function onResolving(h){ if(h===torRow.modelData.infoHash) torRow.dlState="resolving" }
                                    function onProgress(h,rcv,tot){ if(h===torRow.modelData.infoHash){ torRow.dlState="downloading"; torRow.dlPct = tot>0 ? rcv/tot : 0 } }
                                    function onFinished(h,path){ if(h===torRow.modelData.infoHash){ torRow.dlState="done"; torRow.dlPct=1 } }
                                    function onFailed(h,why){ if(h===torRow.modelData.infoHash) torRow.dlState="failed" }
                                }
                                Rectangle { anchors.fill: parent; color: torMa.containsMouse ? Qt.rgba(1,1,1,0.06) : "transparent" }
                                Rectangle { visible: index>0; anchors.top: parent.top; width: parent.width; height: 1; color: Qt.rgba(1,1,1,0.06) }
                                Row {
                                    anchors.left: parent.left; anchors.leftMargin: 18
                                    anchors.verticalCenter: parent.verticalCenter; spacing: 14
                                    Rectangle {                  // format pill
                                        width: Math.max(54, fmtTt.implicitWidth + 16); height: 24; radius: 7
                                        color: "transparent"; border.width: 1; border.color: theme.edge
                                        anchors.verticalCenter: parent.verticalCenter
                                        Text { id: fmtTt; anchors.centerIn: parent; text: torRow.modelData.format
                                            color: theme.inkDim; font.family: theme.ui; font.pixelSize: 11
                                            font.weight: Font.Bold; font.letterSpacing: 0.8 }
                                    }
                                    Text {                        // seeders · size · pack
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: "▲ " + torRow.modelData.seeders + "   " + torRow.modelData.size
                                              + (torRow.modelData.pack ? "   · PACK" : "")
                                        color: theme.inkDim; font.family: theme.ui; font.pixelSize: 13
                                    }
                                }
                                Text {                            // download-state indicator
                                    anchors.right: parent.right; anchors.rightMargin: 18
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: torRow.dlState==="done" ? "✓"
                                        : torRow.dlState==="downloading" ? (Math.round(torRow.dlPct*100)+"%")
                                        : torRow.dlState==="resolving" ? "…"
                                        : torRow.dlState==="failed" ? "retry" : "↓"
                                    color: torRow.dlState==="done" ? theme.gold : (torMa.containsMouse ? theme.gold : theme.inkDimmer)
                                    font.family: theme.ui
                                    font.pixelSize: (torRow.dlState==="downloading" || torRow.dlState==="failed") ? 12 : 16
                                }
                                MouseArea { id: torMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        if (torRow.dlState==="done")
                                            detail.readRequested(BookTorrents.localFile(torRow.modelData.infoHash), detail.book)
                                        else if (torRow.dlState!=="downloading" && torRow.dlState!=="resolving")
                                            BookTorrents.download(torRow.modelData.infoHash, detail.book.title, detail.book.author || "")
                                    }
                                }
                            }
                        }
                    }
                }
```

- [ ] **Step 2b: Guard against the lazy-Loader blind spot**

The lint + boot smoke are blind to lazy-`Loader` QML errors. Run the headless Loader harness (house `colosseum-lazy-page-load-gate`) against `BiblioBook.qml` to prove it instantiates with the new block. If no per-page harness exists, at minimum boot the app, navigate to a book page, and watch stderr:

```bash
cd "/c/Users/Suprabha/Desktop/Brotherhood/Colosseum/native/build-msvc" && QT_FORCE_STDERR_LOGGING=1 ./colosseum.exe 2>&1 | grep -iE 'BiblioBook|torCol|torRow|ReferenceError|is not defined' | head
```
Expected: no `ReferenceError`/binding errors referencing the new ids.

- [ ] **Step 3: Commit**

```bash
git add qml/BiblioBook.qml
git commit -m "feat(biblio): TORRENTS shelf above LibGen — ranked rows, download-to-reader"
```

---

## Task 8: Eyes-on verification + close-out

**Files:** none (verification + recap)

- [ ] **Step 1: Full build, clean boot**

```bash
cmd //c "C:\Users\Suprabha\Desktop\Brotherhood\Colosseum\native\build-msvc.bat"
```
Confirm `colosseum.exe` is fresh (log mtime) and boots with no QML errors.

- [ ] **Step 2: Hand Hemanth the eyes-on script**

Pixels are his eyes (the app is uncapturable headless). Ask him to:
1. Open a well-known book (e.g. *Dune*).
2. Confirm a **TORRENTS** section appears **above EDITIONS**, populates with ranked rows (top row = best title match with the most seeders), each showing format · seeders · size, packs badged.
3. Click the top row → confirm it downloads (progress %) then flips to ✓, and Read opens it in the foliate reader.
4. Try a book with no torrents → confirm honest "No torrents found", LibGen still populates below.

- [ ] **Step 3: Push**

```bash
cd "/c/Users/Suprabha/Desktop/Brotherhood/Colosseum" && git push origin master
```

- [ ] **Step 4: Scribe the wake**

Invoke the `scribe` skill: record the ported indexer set + which indexers shipped alive vs disabled (Task 2 verdict), the sibling-not-generalize downloader call, and any eyes-on fixes. Update MEMORY.md.

---

## Self-Review (against the spec)

- **Spec §Part 1 (search port)** → Task 1. ✓ (TorrentResult, base, 4 indexers, service; books allowlist trimmed)
- **Spec §Part 2 (rank)** → Task 3 (`BookTorrentRanker`, dedup + tier + seeders-desc + pack/format). ✓ Sort matches Hemanth's formula: `matchTier` desc then `seeders` desc.
- **Spec §Part 3 (download, single best file)** → Task 4 (`BookTorrentFilePicker`) + Task 5 (`BookTorrentDownloader`). ✓ Sibling-not-generalize; audiobook lane untouched.
- **Spec §The shelf (QML)** → Task 7; placed above EDITIONS, mirrors edition-row pattern, `typeof BookTorrents` guard, no QML re-sort. ✓
- **Spec §Testing** → ranker + filepicker headless harnesses (Tasks 3–4); indexer live smoke (Task 2); download live smoke (Task 5); eyes-on (Task 8). ✓
- **Spec §Doctrine inheritance** → IPv4-pinned `CachingNam` for search (Task 6 Step 3); native C++ search (not QML XHR) so UA is honored; download-fed torrent→disk→reader (Task 7 onClicked). ✓
- **Spec §Risks** → indexer rot caught by Task 2 (ships dead ones disabled); cold-engine watchdog reused (Task 5); book-swarm death = honest `failed`. ✓
- **Type consistency:** facade signals (`resultsReady`/`resolving`/`progress`/`finished`/`failed`) match the QML `Connections` handlers and the downloader signals; `infoHash` keying is consistent across downloader, facade, and QML rows; `RankedTorrent{src,matchTier,pack,formatGuess}` and `PickedFile{idx,name,ext}` used consistently. ✓
- **Header/`.cpp` consistency:** facade header carries no result-accumulator member; the accumulator is lambda-local in the search `.cpp`. ✓
