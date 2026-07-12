# Biblio Book Torrents Shelf — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.
>
> **Revision 2 (2026-07-13):** hardened after an adversarial pre-flight review (23 confirmed defects, 0 refuted). Key changes from rev 1: **1337x dropped** (its indexer needs a Cloudflare-cookie harvester that pulls in banned QtWebEngine) → **3 indexers**; the downloader task now gives **complete files** (the "copy + 4 edits" recipe left 8 methods uncompilable); the facade's `Qt::UniqueConnection`-lambda **use-after-free** is replaced with connect-once + member accumulator; the search NAM is a **pinned, UA-stamped, uncached** `CachingNam` (a new `useCache` flag) instead of a nonexistent `nam`; the two pure helpers get word-boundary matching and title/author-separated file scoring; `git add -p` → non-interactive surgical staging; `djvu` dropped (foliate can't render it).

**Goal:** Add a ranked TORRENTS shelf above LibGen on the Biblio book page — live federated torrent search per title, ranked best-match then seeders-desc, one-click single-best-ebook-file download via the Stremio engine into the reader.

**Architecture:** Port TB2's UI-free `TankorentSearchService` + **3 book indexers** (PirateBay/apibay JSON, ExtTorrents HTML, torrents-csv JSON — all keyless, CF-free; HTTP-to-indexers, no libtorrent) into `native/torrent/`. Add two pure, headless-tested helpers — a **ranker** (dedup + match-tier + seeders sort) and a **file picker** (best single ebook file inside a torrent's manifest). Add a Stremio-fed `BookTorrentDownloader` (its own single-file, infoHash-keyed downloader, reusing the audiobook downloader's proven engine handshake). Expose one QML facade `BookTorrents`; render the shelf in `BiblioBook.qml`.

**Tech Stack:** Qt6 (Core/Network/Qml), C++17, CMake (MSVC), QML. Headless verdict-by-exit-code harnesses (house pattern, see `tests/comic_dls_parse_harness.cpp`). Stremio sidecar `StreamServer` (exposed to QML as `Stream`).

**Source of truth for ports:** `~/Desktop/Tankoban 2/src/core/`. **Transport reference** for the downloader: `native/engine/AudiobookDownloader.{h,cpp}` in THIS repo (do NOT modify it — it was hardened under eyes-on 2026-07-12).

**Build note (house doctrine):** one build per out-dir; a running `colosseum.exe` locks its files — kill by PID before rebuilding. `CMakeLists.txt` and `native/main.cpp` are high multi-agent collision surfaces (A5 edits main.cpp) — grep-verify every edit, and stage only your own hunks with the **non-interactive** surgical-blob commit: `BLOB=$(git hash-object -w <file>); git update-index --cacheinfo 100644 "$BLOB" <file>`. Never `git add -p` (interactive — hangs a headless session) and never `git add` a whole shared file a brother has unstaged hunks in.

---

## File Structure

New unit `native/torrent/` (all torrent search + rank + download + facade live together — they change together):

| File | New/Port | Responsibility |
|---|---|---|
| `native/torrent/TorrentResult.h` | Port | The result struct + `humanSize()`/`buildMagnet()`/`canonicalizeInfoHash()` helpers |
| `native/torrent/TorrentIndexer.{h,cpp}` | Port | Abstract indexer base + health/telemetry |
| `native/torrent/PirateBayIndexer.{h,cpp}` | Port | apibay.org JSON indexer |
| `native/torrent/ExtTorrentsIndexer.{h,cpp}` | Port | extto.org HTML indexer |
| `native/torrent/TorrentsCsvIndexer.{h,cpp}` | Port | torrents-csv.com JSON indexer |
| `native/torrent/TankorentSearchService.{h,cpp}` | Port | Headless fan-out; books allowlist (3 indexers) |
| `native/torrent/BookTorrentRanker.{h,cpp}` | New (pure) | Dedup + word-boundary match-tier + seeders-desc sort + pack/format flags |
| `native/torrent/BookTorrentFilePicker.{h,cpp}` | New (pure) | Pick the single best ebook file from a manifest (title/author-separated) |
| `native/torrent/BookTorrentDownloader.{h,cpp}` | New | Stremio-fed single-file pull, infoHash-keyed, concurrent (QHash) |
| `native/torrent/BookTorrents.{h,cpp}` | New | QML facade composing search + ranker + downloader |
| `native/main.cpp` | Modify | `CachingNam` `useCache` flag; pin 3 indexer hosts; register `BookTorrents` |
| `native/CMakeLists.txt` | Modify | Add torrent sources + two harness executables |
| `qml/BiblioBook.qml` | Modify | TORRENTS shelf above EDITIONS |
| `tests/book_torrent_ranker_harness.cpp` | New | Ranker unit contract |
| `tests/book_torrent_filepicker_harness.cpp` | New | File-picker unit contract |

> **1337x is deliberately NOT ported.** `X1337xIndexer.cpp` `#include`s `CloudflareCookieHarvester.h`, which is `QWebEngineView`/`QWebEngineProfile`-backed — hidden-browser CF harvesting, banned by house doctrine ([[cf-managed-challenge-blocks-qtwebengine]]) and not linked by the colosseum target. It cannot be ported without importing a banned WebEngine path. Park it; revive only as a plain-UA GET with the CF path stripped, if ever.

---

## Task 1: Port the torrent search core into `native/torrent/`

**Files:**
- Create (copy): `native/torrent/TorrentResult.h`, `TorrentIndexer.{h,cpp}`, `PirateBayIndexer.{h,cpp}`, `ExtTorrentsIndexer.{h,cpp}`, `TorrentsCsvIndexer.{h,cpp}`, `TankorentSearchService.{h,cpp}`
- Modify: `native/CMakeLists.txt` (the `add_executable(colosseum …)` source list)

- [ ] **Step 1: Copy the source files verbatim from TB2 (3 indexers — NO 1337x)**

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
```

- [ ] **Step 2: Flatten include paths (all files now sit in one flat dir)**

TB2 used `#include "core/TorrentResult.h"`, `#include "core/TorrentIndexer.h"`, `#include "core/indexers/…"`, `#include "TorrentResult.h"`, and `#include "../TorrentIndexer.h"`. In `native/torrent/` everything is a sibling. Strip **every** directory prefix — including `core/indexers/` (the search service includes its indexers that way; omitting it leaves the Step-3 grep gate non-empty):

```bash
cd "/c/Users/Suprabha/Desktop/Brotherhood/Colosseum/native/torrent"
sed -i -E 's#include "core/indexers/([A-Za-z0-9_]+\.h)"#include "\1"#g' *.h *.cpp
sed -i -E 's#include "core/([A-Za-z0-9_]+\.h)"#include "\1"#g' *.h *.cpp
sed -i -E 's#include "\.\./([A-Za-z0-9_]+\.h)"#include "\1"#g' *.h *.cpp
sed -i -E 's#include "indexers/([A-Za-z0-9_]+\.h)"#include "\1"#g' *.h *.cpp
```

Then grep to confirm no stale prefixes remain (must print nothing):

```bash
grep -nE 'include "(core|\.\.|indexers)/' native/torrent/*.h native/torrent/*.cpp
```

- [ ] **Step 3: Trim the search service to the 3 book indexers**

`TankorentSearchService.cpp` `kMediaTypeIndexers` includes video/comic indexers we are NOT porting (`nyaa`, `yts`, `eztv`, `1337x`). `buildIndexersFor()` instantiates concrete indexer classes by id. Edit `native/torrent/TankorentSearchService.cpp`:

- In `kMediaTypeIndexers`, keep only the three keyless CF-free book indexers:
```cpp
const QHash<QString, QSet<QString>> kMediaTypeIndexers = {
    { "books",      { "piratebay", "exttorrents", "torrentscsv" } },
    { "audiobooks", { "piratebay", "exttorrents", "torrentscsv" } },
};
```
- In `buildIndexersFor()`, delete the `nyaa`/`yts`/`eztv`/`1337x` instantiation branches and their `#include` lines at the top. Keep only:
```cpp
#include "PirateBayIndexer.h"
#include "ExtTorrentsIndexer.h"
#include "TorrentsCsvIndexer.h"
```

- [ ] **Step 4: Add the ported files to the colosseum target in CMake**

Edit `native/CMakeLists.txt`, inside `add_executable(colosseum …)` (alongside the other `engine/…` entries), add:

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
    torrent/TankorentSearchService.cpp
    torrent/TankorentSearchService.h
```

- [ ] **Step 5: Build to verify the port compiles**

First ensure no `colosseum.exe`/`qml.exe` is holding the build dir (`tasklist | grep -i colosseum`; kill by PID if so). Then:

```bash
cmd //c "C:\Users\Suprabha\Desktop\Brotherhood\Colosseum\native\build-msvc.bat"
```

Expected: `colosseum.exe` links cleanly. If a `CloudflareCookieHarvester.h: No such file` error appears, a 1337x file slipped into the copy — delete `native/torrent/X1337x*` and re-check Step 3. `Qt6::Network` is already linked on the colosseum target.

- [ ] **Step 6: Commit (non-interactive surgical staging)**

```bash
cd "/c/Users/Suprabha/Desktop/Brotherhood/Colosseum"
git add native/torrent/
BLOB=$(git hash-object -w native/CMakeLists.txt); git update-index --cacheinfo 100644 "$BLOB" native/CMakeLists.txt
git commit -m "feat(biblio): port TB2 Tankorent torrent search core (3 CF-free indexers) into native/torrent/"
```

---

## Task 2: Search-NAM infra + indexer live smoke

The smoke MUST run on the SAME pinned, UA-stamped, uncached NAM production uses — otherwise an indexer that works in production gets falsely declared "dead" by a 21s IPv6 stall on an unpinned NAM. So this task first builds that NAM, then smokes.

**Files:**
- Modify: `native/main.cpp` (CachingNam `useCache` flag; pin the 3 indexer hosts; env-gated smoke hook)
- Modify: `native/torrent/TankorentSearchService.{h,cpp}` (env-gated headless self-test)

- [ ] **Step 1: Add a `useCache` flag to `CachingNam` (uncached search, cached images)**

`CachingNam::createRequest` sets `CacheLoadControlAttribute = PreferCache` unconditionally, and the ctor always installs a `QNetworkDiskCache`. Live search must NOT serve stale seeder counts. Add a backward-compatible flag. In `native/main.cpp`:

Ctor (add the param AFTER `parent` so the existing factory call keeps working):
```cpp
    CachingNam(QStringList pinnedHosts, QHash<QString, QString> ipv4ByHost,
               QObject *parent = nullptr, bool useCache = true)
        : QNetworkAccessManager(parent),
          m_pinnedHosts(std::move(pinnedHosts)),
          m_ipv4ByHost(std::move(ipv4ByHost)),
          m_useCache(useCache) {
        if (m_useCache) {
            auto *cache = new QNetworkDiskCache(this);
            const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
                                + QStringLiteral("/colosseum-images");
            QDir().mkpath(dir);
            cache->setCacheDirectory(dir);
            cache->setMaximumCacheSize(qint64(1024) * 1024 * 1024);
            setCache(cache);
        }
    }
```
In `createRequest`, gate the PreferCache line:
```cpp
        if (m_useCache)
            r.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::PreferCache);
```
Add the member:
```cpp
    bool m_useCache = true;
```
(The pin/Host-header/`peerVerifyName`/HTTP2-off + browser-UA logic is untouched and now applies to search too.)

- [ ] **Step 2: Pin the 3 indexer hosts**

Find the `pinnedHosts` initializer list in `main.cpp` (near the other pinned hosts — `getcomics.org`, `leagueofcomicgeeks.com`). Add the three indexer hosts so the existing resolve loop populates `ipv4ByHost` for them:

```cpp
        // Book-torrent indexer hosts (2026-07-13): apibay/extto/torrents-csv publish AAAA
        // records → dead-IPv6 ~21s stall unless pinned to IPv4 (same scar as the comics lane).
        QStringLiteral("apibay.org"),
        QStringLiteral("extto.org"),
        QStringLiteral("torrents-csv.com"),
```
Verify the real host strings against the ported indexers before trusting them:
```bash
grep -rhoE 'https?://[a-z0-9.-]+' native/torrent/PirateBayIndexer.cpp native/torrent/ExtTorrentsIndexer.cpp native/torrent/TorrentsCsvIndexer.cpp | sort -u
```
If a base URL differs (mirror rotation), pin the host it actually uses.

- [ ] **Step 3: Add a headless self-test to the search service**

Add to `TankorentSearchService.h` (public):
```cpp
    // Dev smoke (env COLOSSEUM_TORRENT_SEARCHTEST="<query>"): run a "books" search
    // headless, log per-indexer row counts + total, then quit. Reveals rotted indexers.
    void selfTest(const QString& query);
```
Implement in `TankorentSearchService.cpp` (end on `searchFinished`, not a fixed timer, so a slow-but-alive indexer isn't cut off):
```cpp
void TankorentSearchService::selfTest(const QString& query)
{
    const QString handle = startSearch("books", "all", query, 30);
    if (handle.isEmpty()) { qInfo() << "[torrent-smoke] NO indexers matched"; QCoreApplication::quit(); return; }
    connect(this, &TankorentSearchService::resultsReady, this,
            [](const QString&, const QList<TorrentResult>& r){
                qInfo() << "[torrent-smoke] indexer returned" << r.size() << "rows"
                        << (r.isEmpty() ? QString() : ("e.g. " + r.first().title));
            });
    connect(this, &TankorentSearchService::indexerError, this,
            [](const QString&, const QString& id, const QString& e){
                qInfo() << "[torrent-smoke] ERROR" << id << e;
            });
    connect(this, &TankorentSearchService::searchFinished, this,
            [](const QString&){ qInfo() << "[torrent-smoke] finished"; QCoreApplication::quit(); });
}
```
Add `#include <QCoreApplication>` if not present.

- [ ] **Step 4: Wire the boot hook (uses the pinned uncached NAM)**

In `native/main.cpp`, after `pinnedHosts`/`ipv4ByHost` are built (the resolve loop) and after `dlNam` exists, before `engine.load(...)`:
```cpp
if (qEnvironmentVariableIsSet("COLOSSEUM_TORRENT_SEARCHTEST")) {
    auto *smokeNam = new CachingNam(pinnedHosts, ipv4ByHost, &app, /*useCache=*/false);
    auto *svc = new TankorentSearchService(smokeNam, &app);
    svc->selfTest(qEnvironmentVariable("COLOSSEUM_TORRENT_SEARCHTEST"));
    QTimer::singleShot(45000, &app, &QCoreApplication::quit);  // hard backstop only
}
```
Add `#include "torrent/TankorentSearchService.h"` and `#include <QTimer>` if not present.

- [ ] **Step 5: Build**

```bash
cmd //c "C:\Users\Suprabha\Desktop\Brotherhood\Colosseum\native\build-msvc.bat"
```

- [ ] **Step 6: Run the live smoke, record which indexers are alive**

```bash
cd "/c/Users/Suprabha/Desktop/Brotherhood/Colosseum/native/build-msvc"
COLOSSEUM_TORRENT_SEARCHTEST="dune frank herbert" QT_FORCE_STDERR_LOGGING=1 ./colosseum.exe 2>&1 | grep torrent-smoke
```
Expected: one `[torrent-smoke] indexer returned N rows` per live indexer, `ERROR <id>` for dead ones, then `finished`. Try 2-3 queries. **Record the alive/dead verdict** in the commit message + recap. Any indexer erroring or returning 0 across queries ships **disabled** (leave it out of the allowlist) — do not keep a dead lane.

- [ ] **Step 7: Commit (surgical)**

```bash
BLOB=$(git hash-object -w native/main.cpp); git update-index --cacheinfo 100644 "$BLOB" native/main.cpp
git add native/torrent/TankorentSearchService.h native/torrent/TankorentSearchService.cpp
git commit -m "feat(biblio): pinned uncached search NAM + indexer live smoke — alive: <piratebay,exttorrents,torrentscsv?>"
```

---

## Task 3: `BookTorrentRanker` — pure dedup + rank (TDD)

**Files:**
- Create: `native/torrent/BookTorrentRanker.h`, `native/torrent/BookTorrentRanker.cpp`
- Test: `tests/book_torrent_ranker_harness.cpp`
- Modify: `native/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

Create `tests/book_torrent_ranker_harness.cpp`:

```cpp
// book_torrent_ranker_harness.cpp — rank() contract: dedup by infoHash keeping max
// seeders; sort by match tier desc then seeders desc; WORD-BOUNDARY match; pack + format.
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

    // 5) WORD-BOUNDARY: short title must NOT match a longer unrelated word
    QList<TorrentResult> wb{ mk("Emmanuels Gift",500,"e"), mk("Emma",2,"g") };
    auto r5 = BookTorrentRanker::rank("Emma","",wb);
    require(r5.first().src.infoHash=="g", "\"Emma\" matches \"Emma\", not \"Emmanuels Gift\"");

    // 6) Pack flagged for an explicit multi-count title
    QList<TorrentResult> e{ mk("Sci-Fi EPUB Collection 5000 books",800,"c",40LL*1024*1024*1024) };
    auto r6 = BookTorrentRanker::rank("Dune","",e);
    require(r6.first().pack, "explicit '5000 books' flagged as pack");

    // 7) A legit single novel with a pack-ish word + normal size is NOT a pack
    QList<TorrentResult> f{ mk("The Midnight Library",30,"m",3LL*1024*1024) };
    auto r7 = BookTorrentRanker::rank("The Midnight Library","",f);
    require(!r7.first().pack, "single novel 'The Midnight Library' not badged pack");

    // 8) A large single PDF (scanned textbook) is NOT a pack
    QList<TorrentResult> g{ mk("Gray's Anatomy.pdf",12,"pdf1",120LL*1024*1024) };
    auto r8 = BookTorrentRanker::rank("Grays Anatomy","",g);
    require(!r8.first().pack, "120MB single PDF not badged pack");

    // 9) Format guessed from the title suffix
    QList<TorrentResult> h{ mk("Dune.epub",4,"z") };
    auto r9 = BookTorrentRanker::rank("Dune","",h);
    require(r9.first().formatGuess=="EPUB", "format guessed from .epub in title");

    std::cout<<"book_torrent_ranker_harness PASS\n"; return 0;
}
```

- [ ] **Step 2: Add the harness to CMake and run it to verify it FAILS**

Add to `native/CMakeLists.txt` (after the `comic_dls_parse_harness` block):
```cmake
add_executable(book_torrent_ranker_harness
    ../tests/book_torrent_ranker_harness.cpp
    torrent/BookTorrentRanker.cpp
    torrent/BookTorrentRanker.h
)
target_include_directories(book_torrent_ranker_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(book_torrent_ranker_harness PRIVATE Qt6::Core)
```
Build; expected FAIL: `BookTorrentRanker.h` not found.

- [ ] **Step 3: Write the implementation (word-boundary matching)**

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
    QString formatGuess;     // "EPUB"/"PDF"/"MOBI"/"AZW3"/"FB2"/"EBOOK"
};

class BookTorrentRanker {
public:
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
#include <QSet>
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

int BookTorrentRanker::matchTier(const QString& title, const QString& /*author*/, const QString& candidate){
    const QString t = stripArticles(title);
    const QString c = stripArticles(candidate);
    if (t.isEmpty()) return 0;
    if (c == t) return 4;                        // exact
    if (c.startsWith(t + ' ')) return 3;         // prefix ONLY on a token boundary
    const QStringList titleToks = t.split(' ', Qt::SkipEmptyParts);
    if (titleToks.isEmpty()) return 0;
    const QStringList candList = c.split(' ', Qt::SkipEmptyParts);
    const QSet<QString> candToks(candList.cbegin(), candList.cend());
    bool all = true;
    for (const auto& tok : titleToks) if (!candToks.contains(tok)) { all = false; break; }
    if (all) return 2;                           // all title tokens present as WHOLE tokens
    for (const auto& tok : titleToks) if (candToks.contains(tok)) return 1;  // any whole token
    return 0;
}

bool BookTorrentRanker::looksLikePack(const QString& title, qint64 sizeBytes){
    const QString t = title.toLower();
    // Unambiguous: an explicit multi-item count ("5000 books", "12 epubs").
    static const QRegularExpression countWords("\\b\\d{2,}\\s*(books|epubs|ebooks|volumes?)\\b");
    if (countWords.match(t).hasMatch()) return true;
    // Words that also appear in legit single-novel titles ("The Midnight Library",
    // "Omnibus") only count as a pack when the payload is also oversized.
    static const QRegularExpression softPackWords(
        "\\b(collection|collections|pack|library|anthology|omnibus|bundle)\\b");
    const bool oversize = sizeBytes > 800LL * 1024 * 1024;  // a single scanned PDF can hit 100s of MB
    if (softPackWords.match(t).hasMatch()) return oversize;
    return oversize;
}

QString BookTorrentRanker::guessFormat(const QString& title){
    const QString t = title.toLower();
    struct { const char* ext; const char* label; } m[] = {
        {"epub","EPUB"},{"azw3","AZW3"},{"mobi","MOBI"},{"pdf","PDF"},{"fb2","FB2"}
    };
    for (auto& e : m) if (t.contains(QString(".")+e.ext) || t.contains(QString(" ")+e.ext)) return e.label;
    return "EBOOK";
}

QList<RankedTorrent> BookTorrentRanker::rank(const QString& title, const QString& author,
                                             const QList<TorrentResult>& raw){
    QHash<QString, TorrentResult> best;   // dedup: infoHash, else normalized title
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

- [ ] **Step 4: Build and run — verify PASS**

```bash
cmd //c "C:\Users\Suprabha\Desktop\Brotherhood\Colosseum\native\build-msvc.bat"
cd "/c/Users/Suprabha/Desktop/Brotherhood/Colosseum/native/build-msvc" && ./book_torrent_ranker_harness.exe
```
Expected: `book_torrent_ranker_harness PASS`.

- [ ] **Step 5: Add the ranker to the colosseum target**

In `add_executable(colosseum …)` add:
```cmake
    torrent/BookTorrentRanker.cpp
    torrent/BookTorrentRanker.h
```

- [ ] **Step 6: Commit (surgical)**

```bash
git add native/torrent/BookTorrentRanker.h native/torrent/BookTorrentRanker.cpp tests/book_torrent_ranker_harness.cpp
BLOB=$(git hash-object -w native/CMakeLists.txt); git update-index --cacheinfo 100644 "$BLOB" native/CMakeLists.txt
git commit -m "feat(biblio): BookTorrentRanker — word-boundary match tier + seeders-desc (headless-tested)"
```

---

## Task 4: `BookTorrentFilePicker` — pick the single best ebook file (TDD)

**Files:**
- Create: `native/torrent/BookTorrentFilePicker.h`, `native/torrent/BookTorrentFilePicker.cpp`
- Test: `tests/book_torrent_filepicker_harness.cpp`
- Modify: `native/CMakeLists.txt`

Ebook set = `epub, pdf, mobi, azw3, fb2` — **`djvu` excluded** (the foliate reader has no DJVU backend; a djvu-only torrent would download then open blank). A djvu-only torrent correctly resolves to "no ebook file".

- [ ] **Step 1: Write the failing test**

Create `tests/book_torrent_filepicker_harness.cpp`:
```cpp
// book_torrent_filepicker_harness.cpp — pick() contract: choose the single best ebook
// file; title separated from author; exact-title wins in a same-author pack; djvu excluded.
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

    // 4) Inside a pack, picks the one titled file
    QList<ManifestFile> d{ mf(0,"Asimov - Foundation.epub"),
                           mf(1,"Herbert - Dune.epub"),
                           mf(2,"Tolkien - LOTR.epub") };
    auto p4 = BookTorrentFilePicker::pick("Dune","Frank Herbert",d);
    require(p4.idx==1, "matches the requested title inside a pack");

    // 5) Same-SERIES pack + same author: EXACT title beats sequels sharing the base token
    QList<ManifestFile> e{ mf(0,"Frank Herbert - Children of Dune.epub"),
                           mf(1,"Dune Messiah.epub"),
                           mf(2,"Dune.epub") };
    auto p5 = BookTorrentFilePicker::pick("Dune","Frank Herbert",e);
    require(p5.idx==2, "exact-title file beats same-series siblings + author-token overlap");

    // 6) djvu is NOT an ebook we pick (reader can't render it)
    QList<ManifestFile> f{ mf(0,"Dune.djvu") };
    auto p6 = BookTorrentFilePicker::pick("Dune","",f);
    require(p6.idx==-1, "djvu excluded -> no pickable ebook");

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
Build; expected FAIL: `BookTorrentFilePicker.h` not found.

- [ ] **Step 3: Write the implementation (title/author separated, whole-title bonus)**

Create `native/torrent/BookTorrentFilePicker.h`:
```cpp
#pragma once
#include <QList>
#include <QString>

struct ManifestFile { int idx = 0; QString name; qint64 length = 0; };
struct PickedFile   { int idx = -1; QString name; QString ext; };

class BookTorrentFilePicker {
public:
    static PickedFile pick(const QString& title, const QString& author,
                           const QList<ManifestFile>& files);
    static bool isEbook(const QString& name);     // ext in the ebook set (NO djvu)
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

static QString normName(QString s){
    s = s.toLower();
    s.replace(QRegularExpression("[._\\-]+"), " ");
    s.replace(QRegularExpression("[^a-z0-9 ]"), "");
    return s.replace(QRegularExpression("\\s+"), " ").trimmed();
}

QString BookTorrentFilePicker::extOf(const QString& name){ return QFileInfo(name).suffix().toLower(); }

bool BookTorrentFilePicker::isEbook(const QString& name){
    static const QSet<QString> exts{"epub","pdf","mobi","azw3","fb2"};   // NO djvu (reader can't render)
    return exts.contains(extOf(name));
}

int BookTorrentFilePicker::formatRank(const QString& ext){
    if (ext=="epub") return 6;
    if (ext=="azw3"||ext=="mobi") return 5;
    if (ext=="fb2") return 4;
    if (ext=="pdf") return 3;
    return 0;
}

PickedFile BookTorrentFilePicker::pick(const QString& title, const QString& author,
                                       const QList<ManifestFile>& files){
    const QString wantTitle = normName(title);
    const QStringList titleToks  = wantTitle.split(' ', Qt::SkipEmptyParts);
    const QStringList authorToks = normName(author).split(' ', Qt::SkipEmptyParts);
    PickedFile best;                 // idx == -1 by default
    int bestWhole=-1, bestTitleCov=-1, bestFmt=-1, bestAuthorCov=-1;
    for (const auto& f : files) {
        if (!isEbook(f.name)) continue;
        const QString stem = normName(QFileInfo(f.name).completeBaseName());  // ext stripped
        const int whole = (!wantTitle.isEmpty() && stem == wantTitle) ? 1 : 0;
        int titleCov = 0;  for (const auto& w : titleToks)  if (stem.contains(w)) ++titleCov;
        int authorCov = 0; for (const auto& w : authorToks) if (stem.contains(w)) ++authorCov;
        const int fmt = formatRank(extOf(f.name));
        // lexicographic: exact whole-title stem, then title-token coverage,
        // then format preference, then author tokens ONLY as a final tie-break.
        const bool better =
            whole > bestWhole ||
            (whole==bestWhole && titleCov > bestTitleCov) ||
            (whole==bestWhole && titleCov==bestTitleCov && fmt > bestFmt) ||
            (whole==bestWhole && titleCov==bestTitleCov && fmt==bestFmt && authorCov > bestAuthorCov);
        if (best.idx < 0 || better) {
            best.idx=f.idx; best.name=f.name; best.ext=extOf(f.name);
            bestWhole=whole; bestTitleCov=titleCov; bestFmt=fmt; bestAuthorCov=authorCov;
        }
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

```cmake
    torrent/BookTorrentFilePicker.cpp
    torrent/BookTorrentFilePicker.h
```

- [ ] **Step 6: Commit (surgical)**

```bash
git add native/torrent/BookTorrentFilePicker.h native/torrent/BookTorrentFilePicker.cpp tests/book_torrent_filepicker_harness.cpp
BLOB=$(git hash-object -w native/CMakeLists.txt); git update-index --cacheinfo 100644 "$BLOB" native/CMakeLists.txt
git commit -m "feat(biblio): BookTorrentFilePicker — title/author-separated best-file, djvu excluded (headless-tested)"
```

---

## Task 5: `BookTorrentDownloader` — Stremio-fed single-file pull

**Files:**
- Create: `native/torrent/BookTorrentDownloader.h`, `native/torrent/BookTorrentDownloader.cpp`
- Modify: `native/CMakeLists.txt`

This reuses `AudiobookDownloader`'s **proven engine handshake** (prefetch → `fetchReady` → `/create` manifest, with the cold-engine `pollEngine` watchdog + `/create` timeout/retry) but is otherwise a **fresh single-file, infoHash-keyed, concurrent (`QHash`)** downloader. Do NOT modify `AudiobookDownloader.*`. Read `native/engine/AudiobookDownloader.cpp` for the handshake bodies; copy those five methods (`onFetchReady`, `beginManifest`, `pollEngine`, `requestManifest`, and the metadata-retry tail of `onManifestReply`) and apply the **liveness-check swap** in Step 3.2. Everything else below is given in full.

- [ ] **Step 1: Write the header**

Create `native/torrent/BookTorrentDownloader.h`:
```cpp
// BookTorrentDownloader.h
// Sibling transport to AudiobookDownloader: same Stremio engine handshake, but pulls the
// SINGLE best ebook file (BookTorrentFilePicker) and keys everything by infoHash. Concurrent:
// multiple infoHashes can download at once (QHash), each its own Job.
// On-disk: <appdata>/books-torrent/<infoHash>/<name>.<ext> + .../index.json (keyed by infoHash).
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
        int     pickedIdx = -1;
        QString fileName, ext;
        qint64  totalBytes = 0, received = 0;
        int     enginePolls = 0, createAttempts = 0;
        QPointer<QNetworkReply> reply;
        QFile*  file = nullptr;
        QString finalPath, partPath;
    };

    // engine handshake (ported from AudiobookDownloader; liveness checks swapped to the QHash)
    void onFetchReady(const QString& url, const QString& infoHash, int fileIdx);
    void beginManifest(Job* job, const QString& url);
    void pollEngine(Job* job);
    void requestManifest(Job* job);
    void onManifestReply(QNetworkReply* reply, Job* job);
    // single-file streaming
    void startFile(Job* job);
    void onFileReadyRead();
    void onFileFinished();
    void finalizeJob(Job* job);
    void failJob(Job* job, const QString& reason);
    void cleanupInFlight(Job* job);
    Job* jobForHash(const QString& infoHash) const;
    Job* jobForReply(QNetworkReply* r) const;
    // disk + index
    QString baseDir() const;
    QString dirFor(const QString& infoHash) const;
    void loadIndex();
    void saveIndex() const;

    struct Entry { QString path, title, author; qint64 bytes=0, addedAt=0; };

    QNetworkAccessManager* m_nam = nullptr;
    StreamServer* m_stream = nullptr;
    QHash<QString, Job*> m_active;       // infoHash(lowercased) -> job
    QHash<QString, Entry> m_index;       // infoHash(lowercased) -> entry
};
```

- [ ] **Step 2: Create the .cpp with the given bodies + copied handshake**

Create `native/torrent/BookTorrentDownloader.cpp` starting with:
```cpp
#include "BookTorrentDownloader.h"
#include "BookTorrentFilePicker.h"
#include "player/streamserver.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

static const int kManifestPollMs = 1000;
static const int kMaxEnginePolls = 60;      // ~60s cold-engine watchdog (audiobook lineage)
static const int kMaxCreateAttempts = 30;   // metadata may still be loading

BookTorrentDownloader::BookTorrentDownloader(QNetworkAccessManager* nam, StreamServer* stream, QObject* parent)
    : QObject(parent), m_nam(nam), m_stream(stream) { loadIndex(); }

BookTorrentDownloader::~BookTorrentDownloader(){
    for (Job* j : m_active) { cleanupInFlight(j); delete j; }
    m_active.clear();
}
```

Then the entry point and lifecycle (given in full):
```cpp
void BookTorrentDownloader::download(const QString& infoHashRaw, const QString& title, const QString& author){
    const QString infoHash = infoHashRaw.toLower();
    if (isDownloaded(infoHash)) { emit finished(infoHash, m_index.value(infoHash).path); return; }
    if (m_active.contains(infoHash)) return;                 // already in flight (no-op)
    auto* job = new Job{}; job->infoHash = infoHash; job->title = title; job->author = author;
    m_active.insert(infoHash, job);
    emit resolving(infoHash);
    connect(m_stream, &StreamServer::fetchReady, this, &BookTorrentDownloader::onFetchReady, Qt::UniqueConnection);
    m_stream->prefetch(infoHash, 0);
    pollEngine(job);                                         // watchdog: fetchReady can be lost on a cold engine
}

BookTorrentDownloader::Job* BookTorrentDownloader::jobForHash(const QString& infoHash) const {
    return m_active.value(infoHash.toLower(), nullptr);
}
BookTorrentDownloader::Job* BookTorrentDownloader::jobForReply(QNetworkReply* r) const {
    if (!r) return nullptr;
    for (Job* j : m_active) if (j->reply.data() == r) return j;
    return nullptr;
}
```

Now paste the **five handshake methods** from `AudiobookDownloader.cpp` — `onFetchReady`, `beginManifest`, `pollEngine`, `requestManifest`, and the **metadata-retry tail** of `onManifestReply` — with these mechanical swaps applied to each (the audiobook versions compare a `Job*` against a single `m_active` pointer; ours is a `QHash`):

| audiobook (single pointer) | book (QHash) |
|---|---|
| `if (!job \|\| job != m_active) return;` | `if (!job \|\| m_active.value(job->infoHash) != job) return;` |
| `job != m_active` | `m_active.value(job->infoHash) != job` |
| `if (m_active == job) pollEngine(job);` (retry lambda) | `if (m_active.value(job->infoHash) == job) pollEngine(job);` |
| `if (m_active != job) { reply->deleteLater(); return; }` | `if (m_active.value(job->infoHash) != job) { reply->deleteLater(); return; }` |
| `if (m_active == job) requestManifest(job);` (metadata retry) | `if (m_active.value(job->infoHash) == job) requestManifest(job);` |
| `job->enginePolls`, `kMaxEnginePolls`, `createAttempts`, `kMaxCreateAttempts`, `beginManifest` derive-base logic | keep as-is (Job carries the same fields) |

`pollEngine`/`requestManifest` error paths call `failJob(job, ...)` — keep those calls (failJob is given below). `onFetchReady` looks up the job via `jobForHash(infoHash)`.

- [ ] **Step 3: The manifest parse + single-file selection (the head of `onManifestReply`)**

The head of `onManifestReply` (where the audiobook version filters audio + natural-sorts + queues all files) is REPLACED with single-file selection:
```cpp
void BookTorrentDownloader::onManifestReply(QNetworkReply* reply, Job* job){
    if (m_active.value(job->infoHash) != job) { reply->deleteLater(); return; }
    const QByteArray body = reply->readAll();
    reply->deleteLater();
    QJsonParseError perr{};
    const QJsonDocument doc = QJsonDocument::fromJson(body, &perr);
    const QJsonArray arr = doc.object().value("files").toArray();
    if (perr.error != QJsonParseError::NoError || arr.isEmpty()) {
        // metadata may still be loading — bounded retry (audiobook lineage)
        if (++job->createAttempts <= kMaxCreateAttempts) {
            QTimer::singleShot(kManifestPollMs, this, [this, job]() {
                if (m_active.value(job->infoHash) == job) requestManifest(job);
            });
            return;
        }
        failJob(job, "torrent metadata never resolved"); return;
    }
    QList<ManifestFile> mfs;
    for (int i = 0; i < arr.size(); ++i) {
        const QJsonObject o = arr[i].toObject();
        mfs.push_back({ i, o.value("name").toString(), (qint64)o.value("length").toDouble() });
    }
    const PickedFile pick = BookTorrentFilePicker::pick(job->title, job->author, mfs);
    if (pick.idx < 0) { failJob(job, "no ebook file in torrent"); return; }
    job->pickedIdx = pick.idx; job->fileName = pick.name; job->ext = pick.ext;
    job->totalBytes = 0;
    for (const auto& m : mfs) if (m.idx == pick.idx) job->totalBytes = m.length;
    startFile(job);
}
```
(If `AudiobookDownloader::onManifestReply` takes different arguments, match its real signature; the key change is the body: parse → pick one → `startFile`.)

- [ ] **Step 4: Single-file streaming + finalize/fail/cleanup (given in full)**

```cpp
void BookTorrentDownloader::startFile(Job* job){
    QDir().mkpath(dirFor(job->infoHash));
    const QString safe = QFileInfo(job->fileName).fileName();
    job->finalPath = dirFor(job->infoHash) + "/" + (safe.isEmpty() ? QStringLiteral("book.") + job->ext : safe);
    job->partPath  = job->finalPath + ".part";
    job->file = new QFile(job->partPath);
    if (!job->file->open(QIODevice::WriteOnly | QIODevice::Truncate)) { failJob(job, "cannot open .part for write"); return; }
    const QString url = job->baseUrl + "/" + QString::number(job->pickedIdx);
    QNetworkRequest req{QUrl(url)};
    job->reply = m_nam->get(req);                            // plain GET, no Range -> whole file
    connect(job->reply, &QNetworkReply::readyRead, this, &BookTorrentDownloader::onFileReadyRead);
    connect(job->reply, &QNetworkReply::finished,  this, &BookTorrentDownloader::onFileFinished);
}

void BookTorrentDownloader::onFileReadyRead(){
    auto* r = qobject_cast<QNetworkReply*>(sender());
    Job* job = jobForReply(r);
    if (!job || !job->file) return;
    const QByteArray chunk = r->readAll();
    if (chunk.isEmpty()) return;
    const qint64 written = job->file->write(chunk);
    if (written < 0) { failJob(job, "disk write failed: " + job->file->errorString()); return; }
    job->received += written;
    emit progress(job->infoHash, (double)job->received, (double)job->totalBytes);
}

void BookTorrentDownloader::onFileFinished(){
    auto* r = qobject_cast<QNetworkReply*>(sender());
    Job* job = jobForReply(r);
    if (!job) return;
    const QNetworkReply::NetworkError err = r->error();
    const QString errStr = r->errorString();
    if (err == QNetworkReply::NoError && job->file) {
        const QByteArray tail = r->readAll();
        if (!tail.isEmpty()) { job->file->write(tail); job->received += tail.size(); }
    }
    r->deleteLater(); job->reply.clear();
    if (err != QNetworkReply::NoError) {
        if (job->file) { job->file->close(); job->file->remove(); delete job->file; job->file = nullptr; }
        failJob(job, "download failed: " + errStr); return;
    }
    if (job->totalBytes > 0 && job->received < job->totalBytes) {
        if (job->file) { job->file->close(); job->file->remove(); delete job->file; job->file = nullptr; }
        failJob(job, QStringLiteral("truncated (%1/%2 bytes)").arg(job->received).arg(job->totalBytes)); return;
    }
    finalizeJob(job);
}

void BookTorrentDownloader::finalizeJob(Job* job){
    if (job->file) { job->file->close(); delete job->file; job->file = nullptr; }
    QFile::remove(job->finalPath);
    QFile::rename(job->partPath, job->finalPath);
    Entry e{ job->finalPath, job->title, job->author, job->totalBytes, 0 };
    m_index.insert(job->infoHash, e);
    saveIndex();
    const QString hash = job->infoHash, path = job->finalPath;
    m_active.remove(hash); delete job;
    emit finished(hash, path);
}

void BookTorrentDownloader::failJob(Job* job, const QString& reason){
    const QString hash = job->infoHash;
    cleanupInFlight(job);
    m_active.remove(hash);
    delete job;
    emit failed(hash, reason);
}

void BookTorrentDownloader::cleanupInFlight(Job* job){
    if (job->reply) { job->reply->abort(); job->reply->deleteLater(); job->reply.clear(); }
    if (job->file)  { job->file->close(); job->file->remove(); delete job->file; job->file = nullptr; }
}

void BookTorrentDownloader::cancelDownload(const QString& infoHash){
    const QString h = infoHash.toLower();
    if (Job* j = m_active.take(h)) { cleanupInFlight(j); const QString hh = j->infoHash; delete j; emit failed(hh, "cancelled"); }
}
```

- [ ] **Step 5: Accessors + disk index (given in full)**

```cpp
QString BookTorrentDownloader::baseDir() const {
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/books-torrent";
}
QString BookTorrentDownloader::dirFor(const QString& infoHash) const {
    return baseDir() + "/" + infoHash.toLower();
}
QString BookTorrentDownloader::localFile(const QString& infoHash) const {
    auto it = m_index.constFind(infoHash.toLower());
    if (it == m_index.constEnd() || it.value().path.isEmpty() || !QFileInfo::exists(it.value().path)) return {};
    return it.value().path;
}
bool BookTorrentDownloader::isDownloaded(const QString& infoHash) const {
    return !localFile(infoHash).isEmpty();
}
QVariantMap BookTorrentDownloader::statusOf(const QString& infoHash) const {
    const QString h = infoHash.toLower();
    QVariantMap s;
    if (isDownloaded(h)) { s["state"]="done"; s["received"]=(double)m_index.value(h).bytes; s["total"]=(double)m_index.value(h).bytes; return s; }
    if (Job* j = m_active.value(h, nullptr)) {
        s["state"] = j->baseUrl.isEmpty() ? "resolving" : "downloading";
        s["received"]=(double)j->received; s["total"]=(double)j->totalBytes; return s;
    }
    s["state"]="none"; s["received"]=0; s["total"]=0; return s;
}
void BookTorrentDownloader::loadIndex(){
    QFile f(baseDir() + "/index.json");
    if (!f.open(QIODevice::ReadOnly)) return;
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    for (auto it = root.begin(); it != root.end(); ++it) {
        const QJsonObject o = it.value().toObject();
        Entry e;
        e.path=o.value("path").toString(); e.title=o.value("title").toString();
        e.author=o.value("author").toString();
        e.bytes=(qint64)o.value("bytes").toDouble(); e.addedAt=(qint64)o.value("addedAt").toDouble();
        if (!e.path.isEmpty() && QFileInfo::exists(e.path)) m_index.insert(it.key().toLower(), e);  // prune ghosts
    }
}
void BookTorrentDownloader::saveIndex() const {
    QDir().mkpath(baseDir());
    QJsonObject root;
    for (auto it = m_index.constBegin(); it != m_index.constEnd(); ++it) {
        QJsonObject o;
        o["path"]=it.value().path; o["title"]=it.value().title; o["author"]=it.value().author;
        o["bytes"]=(double)it.value().bytes; o["addedAt"]=(double)it.value().addedAt;
        root[it.key()] = o;
    }
    QFile f(baseDir() + "/index.json");
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
}
```

- [ ] **Step 6: selfTest (dev smoke) + boot hook**

Append to `BookTorrentDownloader.cpp`:
```cpp
void BookTorrentDownloader::selfTest(const QString& infoHash, const QString& title){
    connect(this, &BookTorrentDownloader::finished, this,
            [](const QString& h, const QString& p){ qInfo() << "[bt-dl] DONE" << h << p; QCoreApplication::quit(); });
    connect(this, &BookTorrentDownloader::failed, this,
            [](const QString& h, const QString& why){ qInfo() << "[bt-dl] FAIL" << h << why; QCoreApplication::quit(); });
    connect(this, &BookTorrentDownloader::progress, this,
            [](const QString&, double r, double t){ if (t>0) qInfo() << "[bt-dl] progress" << (int)(100*r/t) << "%"; });
    download(infoHash, title, "");
}
```
In `native/main.cpp`, near the Task-2 smoke hook:
```cpp
if (qEnvironmentVariableIsSet("COLOSSEUM_TORRENT_DLTEST")) {
    const QStringList a = qEnvironmentVariable("COLOSSEUM_TORRENT_DLTEST").split('|');
    auto* dl = new BookTorrentDownloader(dlNam, stream, &app);
    if (a.size()==2) dl->selfTest(a[0], a[1]);
    QTimer::singleShot(120000, &app, &QCoreApplication::quit);
}
```
Add `#include "torrent/BookTorrentDownloader.h"`.

- [ ] **Step 7: Add to CMake + build**

In `add_executable(colosseum …)` add:
```cmake
    torrent/BookTorrentDownloader.cpp
    torrent/BookTorrentDownloader.h
```
Build:
```bash
cmd //c "C:\Users\Suprabha\Desktop\Brotherhood\Colosseum\native\build-msvc.bat"
```
Expected: clean link (no unresolved `localFile`/`isDownloaded`, no `m_queue`/`promoteQueue`/`pairKey`/`Entry.dir` errors).

- [ ] **Step 8: Live download smoke**

Use a healthy single-book infoHash from the Task 2 search verdict:
```bash
cd "/c/Users/Suprabha/Desktop/Brotherhood/Colosseum/native/build-msvc"
COLOSSEUM_TORRENT_DLTEST="<infoHash>|dune" QT_FORCE_STDERR_LOGGING=1 ./colosseum.exe 2>&1 | grep -iE 'bt-dl|torrent'
ls -la "$APPDATA/colosseum/books-torrent/" 2>/dev/null || ls -la ~/AppData/Roaming/*/books-torrent/ 2>/dev/null
```
Expected: `[bt-dl] DONE <hash> <path>` and a real ebook file under `books-torrent/<infoHash>/`. Cold engine may take up to a minute (watchdog) — expected, not a bug.

- [ ] **Step 9: Commit (surgical)**

```bash
git add native/torrent/BookTorrentDownloader.h native/torrent/BookTorrentDownloader.cpp
BLOB=$(git hash-object -w native/main.cpp); git update-index --cacheinfo 100644 "$BLOB" native/main.cpp
BLOB=$(git hash-object -w native/CMakeLists.txt); git update-index --cacheinfo 100644 "$BLOB" native/CMakeLists.txt
git commit -m "feat(biblio): BookTorrentDownloader — Stremio-fed single-best-file pull (live-proven)"
```

---

## Task 6: `BookTorrents` facade + main.cpp registration

**Files:**
- Create: `native/torrent/BookTorrents.h`, `native/torrent/BookTorrents.cpp`
- Modify: `native/main.cpp`, `native/CMakeLists.txt`

- [ ] **Step 1: Write the facade header (connect-once, member accumulator — NO per-search lambda)**

Create `native/torrent/BookTorrents.h`:
```cpp
#pragma once
#include "TorrentResult.h"
#include <QObject>
#include <QList>
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
    // searchNam: pinned, UA-stamped, UNCACHED CachingNam for indexer HTTP.
    // dlNam: uncached NAM for torrent bytes.
    BookTorrents(QNetworkAccessManager* searchNam, QNetworkAccessManager* dlNam,
                 StreamServer* stream, QObject* parent = nullptr);

    Q_INVOKABLE void search(const QString& title, const QString& author);
    Q_INVOKABLE void download(const QString& infoHash, const QString& title, const QString& author);
    Q_INVOKABLE bool    isDownloaded(const QString& infoHash) const;
    Q_INVOKABLE QString localFile(const QString& infoHash) const;
    Q_INVOKABLE QVariantMap statusOf(const QString& infoHash) const;

signals:
    void resultsReady(const QVariantList& rankedRows);
    void searchFinished();
    void resolving(const QString& infoHash);
    void progress(const QString& infoHash, double received, double total);
    void finished(const QString& infoHash, const QString& path);
    void failed(const QString& infoHash, const QString& reason);

private slots:
    void onIndexerResults(const QString& handle, const QList<TorrentResult>& r);
    void onSearchDone(const QString& handle);

private:
    TankorentSearchService* m_search;
    BookTorrentDownloader*  m_dl;
    QString m_handle, m_title, m_author;
    QList<TorrentResult> m_accum;   // accumulator for the ACTIVE handle; reset per search — no heap, no lambda capture
};
```

- [ ] **Step 2: Write the facade .cpp (connect ONCE in ctor; guard by member m_handle; cancel superseded)**

Create `native/torrent/BookTorrents.cpp`:
```cpp
#include "BookTorrents.h"
#include "TankorentSearchService.h"
#include "BookTorrentDownloader.h"
#include "BookTorrentRanker.h"

BookTorrents::BookTorrents(QNetworkAccessManager* searchNam, QNetworkAccessManager* dlNam,
                           StreamServer* stream, QObject* parent)
    : QObject(parent),
      m_search(new TankorentSearchService(searchNam, this)),
      m_dl(new BookTorrentDownloader(dlNam, stream, this))
{
    // search wiring — connected ONCE (PMF slots; never per-search, which would stack)
    connect(m_search, &TankorentSearchService::resultsReady,   this, &BookTorrents::onIndexerResults);
    connect(m_search, &TankorentSearchService::searchFinished, this, &BookTorrents::onSearchDone);
    // re-broadcast downloader signals so QML rows bind by infoHash
    connect(m_dl, &BookTorrentDownloader::resolving, this, &BookTorrents::resolving);
    connect(m_dl, &BookTorrentDownloader::progress,  this, &BookTorrents::progress);
    connect(m_dl, &BookTorrentDownloader::finished,  this, &BookTorrents::finished);
    connect(m_dl, &BookTorrentDownloader::failed,    this, &BookTorrents::failed);
}

void BookTorrents::search(const QString& title, const QString& author){
    m_title = title; m_author = author;
    if (!m_handle.isEmpty()) { m_search->cancelSearch(m_handle); }   // supersede the prior open shelf
    m_accum.clear();
    m_handle = m_search->startSearch("books", "all", title + " " + author, 30);
    if (m_handle.isEmpty()) { emit resultsReady({}); emit searchFinished(); }
}

void BookTorrents::onIndexerResults(const QString& handle, const QList<TorrentResult>& r){
    if (handle != m_handle) return;     // drop stale results from a superseded search
    m_accum += r;
}

void BookTorrents::onSearchDone(const QString& handle){
    if (handle != m_handle) return;
    const auto ranked = BookTorrentRanker::rank(m_title, m_author, m_accum);
    QVariantList rows;
    for (const auto& rt : ranked) {
        if (rt.src.infoHash.isEmpty()) continue;   // undownloadable without a hash
        QVariantMap m;
        m["title"]=rt.src.title; m["infoHash"]=rt.src.infoHash;
        m["seeders"]=rt.src.seeders; m["sizeBytes"]=(double)rt.src.sizeBytes;
        m["size"]=humanSize(rt.src.sizeBytes);
        m["format"]=rt.formatGuess; m["pack"]=rt.pack; m["source"]=rt.src.sourceName;
        rows.push_back(m);
    }
    m_handle.clear(); m_accum.clear();
    emit resultsReady(rows); emit searchFinished();
}

void BookTorrents::download(const QString& infoHash, const QString& title, const QString& author){ m_dl->download(infoHash, title, author); }
bool    BookTorrents::isDownloaded(const QString& h) const { return m_dl->isDownloaded(h); }
QString BookTorrents::localFile(const QString& h) const    { return m_dl->localFile(h); }
QVariantMap BookTorrents::statusOf(const QString& h) const { return m_dl->statusOf(h); }
```
(`humanSize` comes from `TorrentResult.h`, already included via the header. If `TankorentSearchService::cancelSearch` differs in name, match its real signature — it exists per the service header.)

- [ ] **Step 3: Register in main.cpp with a pinned UNCACHED search NAM**

After the `Audiobooks` registration in `native/main.cpp`, add:
```cpp
// Book torrents shelf: federated indexer search + Stremio-fed single-file pull.
// searchNam = pinned + UA-stamped + UNCACHED CachingNam (live seeder counts, no stale cache);
// dlNam carries the torrent bytes. `pinnedHosts`/`ipv4ByHost` already include the 3 indexer hosts (Task 2).
auto *searchNam = new CachingNam(pinnedHosts, ipv4ByHost, &app, /*useCache=*/false);
auto *bookTorrents = new BookTorrents(searchNam, dlNam, stream, &app);
engine.rootContext()->setContextProperty(QStringLiteral("BookTorrents"), bookTorrents);
```
Add `#include "torrent/BookTorrents.h"`. Verify `pinnedHosts`, `ipv4ByHost`, `dlNam`, and `stream` are the real local names in scope at this point (grep around the `Audiobooks` registration); adjust if a name differs.

- [ ] **Step 4: Add to CMake + build + boot**

In `add_executable(colosseum …)` add:
```cmake
    torrent/BookTorrents.cpp
    torrent/BookTorrents.h
```
Build, then boot once and confirm no QML error about `BookTorrents`:
```bash
cmd //c "C:\Users\Suprabha\Desktop\Brotherhood\Colosseum\native\build-msvc.bat"
cd "/c/Users/Suprabha/Desktop/Brotherhood/Colosseum/native/build-msvc" && QT_FORCE_STDERR_LOGGING=1 ./colosseum.exe 2>&1 | grep -iE 'error|BookTorrents' | head
```

- [ ] **Step 5: Commit (surgical)**

```bash
git add native/torrent/BookTorrents.h native/torrent/BookTorrents.cpp
BLOB=$(git hash-object -w native/main.cpp); git update-index --cacheinfo 100644 "$BLOB" native/main.cpp
BLOB=$(git hash-object -w native/CMakeLists.txt); git update-index --cacheinfo 100644 "$BLOB" native/CMakeLists.txt
git commit -m "feat(biblio): BookTorrents facade + registration (connect-once search, pinned uncached NAM)"
```

---

## Task 7: TORRENTS shelf in `BiblioBook.qml`

**Files:**
- Modify: `qml/BiblioBook.qml`

- [ ] **Step 1: Add state properties + the loader**

In `BiblioBook.qml`, add near the other `detail` properties (by `property var editions: []`):
```qml
property var torrents: []              // ranked rows from BookTorrents (native order — no re-sort)
property bool torLoading: false
```
In `loadEditions()`, add at the end of the function body:
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
```
Add a `Connections` block at the SAME top-level scope as the existing `Audiobooks` `Connections` (NOT inside the content `Column` — a `Connections` placed in a positioner is an error). Confirm `detail.book.author` exists first:
```bash
grep -nE '\.author' qml/BiblioBook.qml
```
If books carry no `.author`, pass `""` (the `|| ""` already guards it).
```qml
Connections {
    target: (typeof BookTorrents !== 'undefined') ? BookTorrents : null
    function onResultsReady(rows) { detail.torrents = rows; detail.torLoading = false }
    function onSearchFinished()   { detail.torLoading = false }
}
```

- [ ] **Step 2: Insert the shelf UI directly above the EDITIONS block**

In `BiblioBook.qml`, immediately BEFORE the `// ── Editions ──` comment (before its leading `Item { width: 1; height: 40 }`), insert the block below. It mirrors the EDITIONS delegate (format pill + meta + right-side state indicator) but reads seeders/size/pack and keys the per-row `Connections` by `infoHash`:
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
                        Item {
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
                                    Rectangle {
                                        width: Math.max(54, fmtTt.implicitWidth + 16); height: 24; radius: 7
                                        color: "transparent"; border.width: 1; border.color: theme.edge
                                        anchors.verticalCenter: parent.verticalCenter
                                        Text { id: fmtTt; anchors.centerIn: parent; text: torRow.modelData.format
                                            color: theme.inkDim; font.family: theme.ui; font.pixelSize: 11
                                            font.weight: Font.Bold; font.letterSpacing: 0.8 }
                                    }
                                    Text {
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: "▲ " + torRow.modelData.seeders + "   " + torRow.modelData.size
                                              + (torRow.modelData.pack ? "   · PACK" : "")
                                        color: theme.inkDim; font.family: theme.ui; font.pixelSize: 13
                                    }
                                }
                                Text {
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
Confirm `readRequested(path, book)`'s real signature matches (`grep -n 'signal readRequested' qml/BiblioBook.qml`).

- [ ] **Step 3: Guard the lazy-Loader blind spot + eyes-on the page**

Lint/boot-smoke are blind to lazy-`Loader` QML errors ([[colosseum-lazy-page-load-gate]]). Boot the app, open a book page, and watch stderr for binding/reference errors in the new block:
```bash
cd "/c/Users/Suprabha/Desktop/Brotherhood/Colosseum/native/build-msvc" && QT_FORCE_STDERR_LOGGING=1 ./colosseum.exe 2>&1 | grep -iE 'BiblioBook|torCol|torRow|ReferenceError|is not defined|Unable to assign' | head
```
Expected: no errors referencing the new ids.

- [ ] **Step 4: Commit**

```bash
git add qml/BiblioBook.qml
git commit -m "feat(biblio): TORRENTS shelf above LibGen — ranked rows, download-to-reader"
```

---

## Task 8: Eyes-on verification + close-out

**Files:** none (verification + recap)

- [ ] **Step 1: Full build, clean boot** — confirm `colosseum.exe` is fresh (log mtime) and boots with no QML errors.

- [ ] **Step 2: Hand Hemanth the eyes-on script** (pixels are his eyes; the app is uncapturable headless):
  1. Open a well-known book (e.g. *Dune*).
  2. Confirm a **TORRENTS** section appears **above EDITIONS**, populates ranked (top = best title match, most seeders), rows show format · seeders · size, packs badged.
  3. Click the top row → downloads (progress %), flips to ✓, Read opens it in the reader.
  4. Open a second book quickly after the first (before its search settles) → confirm the shelf shows the SECOND book's torrents, not the first's (the supersede/`cancelSearch` guard).
  5. A book with no torrents → honest "No torrents found"; LibGen still populates below.

- [ ] **Step 3: Push** — `git push origin master` (pull --rebase first if a brother pushed; A5 is active in this repo).

- [ ] **Step 4: Scribe the wake** — record the alive/disabled indexer set (Task 2 verdict), the `CachingNam useCache` addition, `books-torrent/` on-disk layout, and any eyes-on fixes. Update MEMORY.md.

---

## Self-Review (against the spec, post-hardening)

- **Spec §Part 1 (search port)** → Task 1. ✓ **3 indexers** (1337x dropped: CF-harvester/WebEngine, banned + won't link). `sed` strips `core/indexers/` too.
- **Spec §Part 2 (rank)** → Task 3. ✓ Sort = matchTier desc then seeders desc (Hemanth's formula). matchTier is **word-boundary** (token-set + `startsWith(t+' ')`) so "Emma" ≠ "Emmanuel's Gift". `looksLikePack` splits explicit-count (always) from soft words (size-gated, 800MB) — no false PACK on single novels / large PDFs.
- **Spec §Part 3 (download, single best file)** → Task 4 (`BookTorrentFilePicker`, title/author separated, **djvu excluded**) + Task 5 (`BookTorrentDownloader`, full files, QHash-concurrent, `jobForReply` routes bytes per reply). Audiobook lane untouched.
- **Spec §The shelf (QML)** → Task 7; above EDITIONS, `typeof BookTorrents` guard, `Connections` at top-level scope, no QML re-sort, `readRequested`/`.author` verified before use.
- **Spec §Testing** → ranker + filepicker headless harnesses (9 + 6 cases); indexer live smoke on the **production NAM** (Task 2); download live smoke (Task 5); eyes-on incl. the supersede case (Task 8). ✓
- **Spec §Doctrine inheritance** → search rides a **pinned (21s-stall), UA-stamped, uncached (live)** `CachingNam`; native C++ search (UA honored, not QML XHR); download-fed torrent→disk→reader. ✓
- **Facade memory-safety (the rev-1 blocker):** connect-once PMF slots + member `m_accum` + `m_handle` guard + `cancelSearch` on supersede — no `Qt::UniqueConnection`-lambda stacking, no heap accumulator, no UAF/double-free. ✓
- **Build/doctrine:** `nam` → a real `new CachingNam(..., useCache=false)`; indexer hosts pinned; every `git add -p` → non-interactive `hash-object`+`update-index`; new `Q_OBJECT` sources in the AUTOMOC'd colosseum target. ✓
- **Type consistency:** `m_active`/`m_index` are `QHash` keyed by lowercased infoHash across every method; `RankedTorrent{src,matchTier,pack,formatGuess}`, `PickedFile{idx,name,ext}`, `ManifestFile{idx,name,length}`, `Entry{path,title,author,bytes,addedAt}` used consistently; facade signals match QML `Connections` handlers and downloader signals. ✓
