# Your Collection + AF2 Theatre Detail Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the "Your Collection" saved-items shelf (native store + ＋ Library toggle on all five detail surfaces + a Collection row per world), then adopt Arctic Fuse 2's composition on Theatre detail (fact block, series Watch button, Cast row, More Like This) below an untouched episode list.

**Architecture:** A `CollectionStore` (ProgressStore-shaped: QSettings JSON-blob persistence, `revision` reactivity, hermetic INI test constructor) exposed as context property `Collection`; one shared `LibraryButton.qml` ghost toggle dropped onto each detail surface with a world-specific entry snapshot; world rows REUSE `ContinueRow` via a new optional `forgetHandler`; clicks route through one new `Main.qml` switch (`openCollectionEntry`). AF2 tiers read Cinemeta meta fields we already fetch and discard, plus one new AniList per-title query for anime cast/studio/source.

**Tech Stack:** Qt 6 / QML, C++ (header-only store, GoogleTest-free `require()` harness), PowerShell grep contracts, Node pure-logic contracts, MSVC via `native\build-target.bat`.

**Spec:** `docs/superpowers/specs/2026-07-19-your-collection-af2-detail-arc-design.md` (ratified rulings: episodes untouched; new rows BELOW episodes; all-world button sweep; row order Continue → Next Up → Your Collection; Tankoban Collection rows filter per tab; trailer OUT).

**House gotchas (read first):**
- Colosseum is its OWN git repo (`cd` here before git; push here). After EVERY commit: `git pull --rebase --autostash` then `git push origin master`.
- Commit with EXPLICIT PATHSPEC (`git commit <paths> -m …`), never bare `git commit` — brothers keep staged WIP in this tree.
- Harness build: `native/build-target.bat <target>` (configures nothing new by itself — new CMake targets get picked up because editing `native/CMakeLists.txt` triggers reconfigure on next build). Harness exes land in `native/build-msvc/`.
- App relink (only Task 2 needs it): kill any running colosseum.exe by PID first (`powershell "Get-Process colosseum -ErrorAction SilentlyContinue | Stop-Process"`), then `native/build-target.bat colosseum`.
- Boot smoke: from repo root, `QT_FORCE_STDERR_LOGGING=1 ./native/build-msvc/colosseum.exe qml/Main.qml 2> boot_smoke.log`, let it boot ~10s, kill it, then `grep -iE "error|failed to load" boot_smoke.log` must be empty of QML errors.
- QML `font.pixelSize` must be an int literal. New shared components may use bare `theme` (resolved via context chain from `Theme { id: theme }` in Main.qml:1069).

---

## File structure

| File | Role |
|---|---|
| Create `native/CollectionStore.h` | The store (header-only, mirrors ProgressStore.h) |
| Create `tests/collection_store_harness.cpp` | Behavioral harness (mirrors progress_store_harness.cpp) |
| Modify `native/CMakeLists.txt` | Add harness target after progress_store_harness block (~L296) |
| Modify `native/main.cpp` | Include ~L34; register `Collection` in the L619–665 store cluster |
| Create `qml/LibraryButton.qml` | The ONE ghost toggle, reused by all five surfaces |
| Modify `qml/ContinueRow.qml` | Optional `forgetHandler` (default keeps Progress.forget) |
| Modify `qml/Main.qml` | `openCollectionEntry()` switch + connect lines in world-loader wiring (~L1325) |
| Modify `qml/TheatreSeries.qml` | Button (actions row ~L656), fact block (~L702), series Watch, CastRow + MoreLikeThisRow after `episodesSection`, `openItemRequested` signal |
| Modify `qml/BiblioBook.qml` | Wire the dead button L324–333 |
| Modify `qml/MangaSeries.qml` | Button in hero Row L392–447 |
| Modify `qml/ComicSeries.qml` | Button in hero (L286–337 area) |
| Modify `qml/ComicSeriesPage.qml` | Button in its header area |
| Modify `qml/TheatreWorld.qml` | Collection row after Continue (L154–160) |
| Modify `qml/BiblioWorld.qml` | Collection row after Continue (L45–51) |
| Modify `qml/TankobanWorld.qml` + `qml/TankobanMangaTab.qml` + `qml/TankobanComicsTab.qml` | Per-tab Collection rows + signal bubble |
| Create `qml/TheatreFacts.js` | Pure fact-row assembly (`.pragma library`) |
| Modify `qml/TheatreApi.js` | Capture-side: `postJson`, `animeIdFor`, `loadAnimeCast`, `moreLikeThis` |
| Create `qml/CastRow.qml`, `qml/MoreLikeThisRow.qml` | The two new rows |
| Create `tests/test_collection_p0.ps1`, `tests/test_theatre_af2_p0.ps1` | Grep contracts (+ harness runner) |
| Create `tests/test_theatre_facts.js`, `tests/test_theatre_cast_pick.js` | Node pure-logic contracts |

**Identity snapshot per surface (the universe-tile law — type always rides):**

| Surface | world | entry |
|---|---|---|
| TheatreSeries | `theatre` | `{id: String(itemData.id \|\| resolvedId), type: mediaType, title, cover, payload: {art: banner}}` |
| BiblioBook | `biblio` | `{id: detail.pairKey, type: "book", title: book.title, cover: book.cover, payload: {book: detail.book}}` |
| MangaSeries | `tankoban` | `{id: seriesTitle, type: "manga", title: seriesTitle, cover, payload: {}}` (manga reopens BY TITLE) |
| ComicSeries | `tankoban` | `{id: seriesId, type: "comic", title: seriesTitle, cover: poster, payload: {tag: tagSlug, tagId: tagId}}` (`seriesId` = `gc:`/`gcd:` prefixed) |
| ComicSeriesPage | `tankoban` | `{id: locgId, type: "comic", title: seriesTitle, cover, payload: {locgMeta: locgMeta}}` |

---

# Sub-arc A — Your Collection

### Task 1: CollectionStore + harness (TDD)

**Files:**
- Create: `tests/collection_store_harness.cpp`
- Create: `native/CollectionStore.h`
- Modify: `native/CMakeLists.txt` (after the `progress_store_harness` block, ~L296)

- [ ] **Step 1: Write the failing harness**

`tests/collection_store_harness.cpp`:

```cpp
// CollectionStore is the "Your Collection" shelf: what the user CHOSE to save via
// + Library. Distinct from ProgressStore (auto history): an entry can exist
// unstarted and survives finishing. This proves: add/has/remove round-trip,
// newest-first items() per world, world isolation, upsert-not-duplicate,
// blank-id/world rejection, and persistence across reload.
//
// House convention: require() prints "FAIL: <msg>" and exits 1 (Release-safe).
#include "CollectionStore.h"

#include <QCoreApplication>
#include <QTemporaryDir>
#include <QVariantList>
#include <QVariantMap>

#include <cstdlib>
#include <iostream>

namespace {

void require(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

QVariantMap entry(const QString &id, const QString &type, const QString &title, qint64 addedAt)
{
    QVariantMap m;
    m.insert(QStringLiteral("id"), id);
    m.insert(QStringLiteral("type"), type);
    m.insert(QStringLiteral("title"), title);
    m.insert(QStringLiteral("cover"), QStringLiteral("file:///c.jpg"));
    m.insert(QStringLiteral("addedAt"), addedAt);   // explicit for deterministic ordering
    return m;
}

void runSuite()
{
    QTemporaryDir tmp;
    require(tmp.isValid(), "temporary QSettings directory exists");
    const QString path = tmp.filePath(QStringLiteral("collection.ini"));

    CollectionStore store(path);
    require(store.items(QStringLiteral("theatre")).isEmpty(), "starts empty");

    const int rev0 = store.revision();
    store.add(QStringLiteral("theatre"), entry("tt0388629", "series", "One Piece", 1000));
    store.add(QStringLiteral("theatre"), entry("tt1234567", "movie", "Some Movie", 2000));
    store.add(QStringLiteral("tankoban"), entry("Berserk", "manga", "Berserk", 1500));
    store.add(QStringLiteral("biblio"), entry("pk:joe-country", "book", "Joe Country", 1600));
    require(store.revision() > rev0, "revision bumps on add");

    require(store.has(QStringLiteral("theatre"), QStringLiteral("tt0388629")), "has() finds a saved id");
    require(!store.has(QStringLiteral("tankoban"), QStringLiteral("tt0388629")), "worlds are isolated");

    QVariantList theatre = store.items(QStringLiteral("theatre"));
    require(theatre.size() == 2, "items() is per-world");
    require(theatre.at(0).toMap().value(QStringLiteral("id")) == QStringLiteral("tt1234567"),
            "items() is newest-first by addedAt");
    require(theatre.at(0).toMap().value(QStringLiteral("type")) == QStringLiteral("movie"),
            "type rides on the entry (universe-tile law)");

    // Upsert: re-adding the same (world,id) replaces, never duplicates.
    store.add(QStringLiteral("theatre"), entry("tt0388629", "series", "One Piece (renamed)", 3000));
    theatre = store.items(QStringLiteral("theatre"));
    require(theatre.size() == 2, "re-add upserts, no duplicate");
    require(theatre.at(0).toMap().value(QStringLiteral("title")) == QStringLiteral("One Piece (renamed)"),
            "upsert replaces the entry and reorders by new addedAt");

    // Rejection: blank world/id are no-ops.
    const int revBefore = store.revision();
    store.add(QString(), entry("x", "movie", "X", 1));
    store.add(QStringLiteral("theatre"), entry(QString(), "movie", "X", 1));
    require(store.revision() == revBefore, "blank world/id rejected without a bump");

    store.remove(QStringLiteral("theatre"), QStringLiteral("tt1234567"));
    require(!store.has(QStringLiteral("theatre"), QStringLiteral("tt1234567")), "remove() drops the entry");
    require(store.items(QStringLiteral("theatre")).size() == 1, "only the removed entry left");
    require(store.has(QStringLiteral("biblio"), QStringLiteral("pk:joe-country")), "other worlds untouched by remove");

    // Persistence: a fresh store over the same INI reflects everything.
    CollectionStore reloaded(path);
    require(reloaded.items(QStringLiteral("theatre")).size() == 1, "entries persist across reload");
    require(reloaded.has(QStringLiteral("tankoban"), QStringLiteral("Berserk")), "manga title-key persists");
    require(!reloaded.has(QStringLiteral("theatre"), QStringLiteral("tt1234567")), "removal persists");
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    runSuite();
    std::cout << "CollectionStore behavioral tests passed.\n";
    return 0;
}
```

- [ ] **Step 2: Add the CMake target** — in `native/CMakeLists.txt`, directly AFTER the `progress_store_harness` block (ends ~L296):

```cmake
add_executable(collection_store_harness
    ../tests/collection_store_harness.cpp
    CollectionStore.h
)
target_include_directories(collection_store_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(collection_store_harness PRIVATE Qt6::Core)
```

- [ ] **Step 3: Run to verify it fails**

Run: `cd native && ./build-target.bat collection_store_harness 2>&1 | tail -5`
Expected: FAIL — `CollectionStore.h: No such file or directory`.

- [ ] **Step 4: Write the store**

`native/CollectionStore.h`:

```cpp
#pragma once
// CollectionStore — the "Your Collection" shelf: what the user CHOSE to save via
// the + Library toggle. NOT Continue: distinct from ProgressStore (auto history) —
// an entry can exist unstarted and survives finishing. One store, three worlds.
//
// QML side (the only contract):
//   Collection.add(world, { id, type, title, cover, payload })   // upsert; stamps addedAt
//   Collection.remove(world, id)
//   Collection.has(world, id)
//   Collection.items(world)        // newest-first by addedAt
//   Collection.revision            // bump on every change — name it in a binding to make
//                                  //   has()/items()-based bindings re-evaluate reactively.
//
// `type` must ride on every entry (universe-tile law: a tile without type opens a
// series as a movie and dies). `payload` is the world-specific reopen snapshot.
// Persistence mirrors ProgressStore: one JSON blob under "collection/entries".

#include <QDateTime>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QSettings>
#include <QString>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

#include <algorithm>

class CollectionStore : public QObject {
    Q_OBJECT
    Q_PROPERTY(int revision READ revision NOTIFY changed)
public:
    explicit CollectionStore(QObject *parent = nullptr)
        : QObject(parent) {
        load();
    }

    // Test/diagnostic constructor: back the store with an explicit INI file so
    // harnesses stay hermetic. Mirrors ProgressStore's path constructor.
    explicit CollectionStore(const QString &iniPath, QObject *parent = nullptr)
        : QObject(parent),
          m_settings(iniPath, QSettings::IniFormat) {
        load();
    }

    int revision() const { return m_revision; }

    Q_INVOKABLE void add(const QString &world, const QVariantMap &entry) {
        const QString id = entry.value(QStringLiteral("id")).toString();
        if (world.isEmpty() || id.isEmpty())
            return;
        QVariantMap e = entry;
        e.insert(QStringLiteral("world"), world);
        if (!e.value(QStringLiteral("addedAt")).toLongLong())
            e.insert(QStringLiteral("addedAt"), QDateTime::currentMSecsSinceEpoch());
        m_map.insert(mapKey(world, id), e);
        save();
        bump();
    }

    Q_INVOKABLE void remove(const QString &world, const QString &id) {
        if (m_map.remove(mapKey(world, id))) { save(); bump(); }
    }

    Q_INVOKABLE bool has(const QString &world, const QString &id) const {
        return m_map.contains(mapKey(world, id));
    }

    Q_INVOKABLE QVariantList items(const QString &world) const {
        QVariantList out;
        for (auto it = m_map.constBegin(); it != m_map.constEnd(); ++it) {
            const QVariantMap e = it.value().toMap();
            if (e.value(QStringLiteral("world")).toString() == world)
                out.append(e);
        }
        std::sort(out.begin(), out.end(), [](const QVariant &a, const QVariant &b) {
            return a.toMap().value(QStringLiteral("addedAt")).toLongLong()
                 > b.toMap().value(QStringLiteral("addedAt")).toLongLong();
        });
        return out;
    }

signals:
    void changed();

private:
    static QString mapKey(const QString &world, const QString &id) {
        return world + QStringLiteral("\x1f") + id;   // unit-separator: safe joiner
    }
    void bump() { ++m_revision; emit changed(); }

    void load() {
        const QByteArray raw = m_settings.value(QStringLiteral("collection/entries")).toByteArray();
        if (raw.isEmpty())
            return;
        const QJsonDocument doc = QJsonDocument::fromJson(raw);
        const QJsonObject obj = doc.object();
        for (auto it = obj.constBegin(); it != obj.constEnd(); ++it)
            m_map.insert(it.key(), it.value().toObject().toVariantMap());
    }
    void save() {
        QJsonObject obj;
        for (auto it = m_map.constBegin(); it != m_map.constEnd(); ++it)
            obj.insert(it.key(), QJsonObject::fromVariantMap(it.value().toMap()));
        m_settings.setValue(QStringLiteral("collection/entries"),
                            QJsonDocument(obj).toJson(QJsonDocument::Compact));
        m_settings.sync();
    }

    QSettings m_settings;
    QHash<QString, QVariant> m_map;
    int m_revision = 0;
};
```

- [ ] **Step 5: Build + run to verify it passes**

Run: `cd native && ./build-target.bat collection_store_harness 2>&1 | tail -3 && ./build-msvc/collection_store_harness.exe`
Expected: `TARGET_BUILD_OK` then `CollectionStore behavioral tests passed.` (exit 0)

- [ ] **Step 6: Commit + push**

```bash
git add native/CollectionStore.h tests/collection_store_harness.cpp native/CMakeLists.txt
git commit native/CollectionStore.h tests/collection_store_harness.cpp native/CMakeLists.txt -m "[Agent 4 (Claude), player] CollectionStore: the Your Collection shelf's memory (add/remove/has/items, harness green)"
git pull --rebase --autostash && git push origin master
```

---

### Task 2: Register `Collection` + rebuild the app

**Files:**
- Modify: `native/main.cpp` (include ~L34; registration in the L619–665 cluster)
- Create: `tests/test_collection_p0.ps1` (started here, grown in later tasks)

- [ ] **Step 1: Write the failing grep contract** — `tests/test_collection_p0.ps1`:

```powershell
# Your Collection arc — wiring contracts (SHAPE, not behavior; behavior = harness).
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
function Read-File($p) { Get-Content -Raw (Join-Path $root $p) }
function Assert-Contains($hay, $needle, $msg) {
    if ($hay -notlike "*$needle*") { Write-Host "FAIL: $msg"; exit 1 }
}

$main = Read-File "native/main.cpp"
Assert-Contains $main '#include "CollectionStore.h"' "main.cpp includes CollectionStore.h"
Assert-Contains $main 'setContextProperty(QStringLiteral("Collection")' "Collection registered as context property"

# The behavioral harness must exist and pass.
$harness = Join-Path $root "native\build-msvc\collection_store_harness.exe"
if (-not (Test-Path $harness)) { Write-Host "FAIL: build collection_store_harness first"; exit 1 }
& $harness | Out-Null
if ($LASTEXITCODE -ne 0) { Write-Host "FAIL: collection_store_harness red"; exit 1 }

Write-Host "test_collection_p0 PASSED"
```

Run: `powershell -ExecutionPolicy Bypass -File tests/test_collection_p0.ps1`
Expected: FAIL — main.cpp doesn't include CollectionStore.h yet.

- [ ] **Step 2: Register the store** — in `native/main.cpp`: add `#include "CollectionStore.h"` beside the ProgressStore include (~L34). In the store cluster, directly after the Progress registration (L625–626):

```cpp
auto *collection = new CollectionStore(&app);
engine.rootContext()->setContextProperty(QStringLiteral("Collection"), collection);
```

- [ ] **Step 3: Relink the app.** Kill any running exe first:

```bash
powershell "Get-Process colosseum -ErrorAction SilentlyContinue | Stop-Process"
cd native && ./build-target.bat colosseum 2>&1 | tail -3
```
Expected: `TARGET_BUILD_OK`.

- [ ] **Step 4: Run the contract** — `powershell -ExecutionPolicy Bypass -File tests/test_collection_p0.ps1` → `test_collection_p0 PASSED`.

- [ ] **Step 5: Commit + push**

```bash
git add native/main.cpp tests/test_collection_p0.ps1
git commit native/main.cpp tests/test_collection_p0.ps1 -m "[Agent 4 (Claude), player] Expose Collection to QML beside Progress"
git pull --rebase --autostash && git push origin master
```

---

### Task 3: Row + routing infrastructure (LibraryButton, ContinueRow forgetHandler, openCollectionEntry)

**Files:**
- Create: `qml/LibraryButton.qml`
- Modify: `qml/ContinueRow.qml` (~L15 API block and ~L44–53 delegate wiring)
- Modify: `qml/Main.qml` (new `openCollectionEntry` beside `detailContinue` ~L789; connect line in world-loader wiring ~L1325–1327)
- Modify: `tests/test_collection_p0.ps1`

- [ ] **Step 1: LibraryButton.qml** — the ONE ghost toggle (recipe = BiblioBook's dead button; 42px/radius 11 to sit beside gold pills; override `width`/`height` at use sites that need block shape):

```qml
import QtQuick

// + Library — the Collection arc's one button. Ghost recipe (translucent white,
// theme.edge border); flips to a gold check when saved. `entry` is the world's
// reopen snapshot: { id, type, title, cover, payload } — type ALWAYS rides
// (universe-tile law). Naming Collection.revision keeps `saved` live.
Rectangle {
    id: lib
    property string world: ""
    property var entry: null
    readonly property bool saved: (Collection.revision,
        entry && entry.id ? Collection.has(world, String(entry.id)) : false)

    width: libRow.implicitWidth + 36
    height: 42
    radius: 11
    color: libMa.containsMouse ? Qt.rgba(1, 1, 1, 0.10) : Qt.rgba(1, 1, 1, 0.05)
    border.width: 1
    border.color: theme.edge

    Row {
        id: libRow
        anchors.centerIn: parent
        spacing: 8
        Text {
            text: lib.saved ? "✓" : "+"
            color: lib.saved ? theme.gold : theme.ink
            font.family: theme.ui; font.pixelSize: 15; font.weight: Font.DemiBold
            anchors.verticalCenter: parent.verticalCenter
        }
        Text {
            text: lib.saved ? "In Library" : "Library"
            color: theme.ink
            font.family: theme.ui; font.pixelSize: 15; font.weight: Font.DemiBold
            anchors.verticalCenter: parent.verticalCenter
        }
    }
    MouseArea {
        id: libMa
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: {
            if (!lib.entry || !lib.entry.id) return
            if (lib.saved) Collection.remove(lib.world, String(lib.entry.id))
            else Collection.add(lib.world, lib.entry)
        }
    }
}
```

- [ ] **Step 2: ContinueRow forgetHandler** — in `qml/ContinueRow.qml`, add below the existing API block (~L15):

```qml
    // Optional: rows not backed by Progress (e.g. Your Collection) supply their own
    // remove. Default preserves the Progress.forget wiring untouched.
    property var forgetHandler: null
```

and change the delegate's remove line (~L52) from
`onRemoveRequested: Progress.forget(modelData.kind, modelData.id)` to:

```qml
                onRemoveRequested: cont.forgetHandler
                    ? cont.forgetHandler(modelData)
                    : Progress.forget(modelData.kind, modelData.id)
```

(`cont` is the row's root id — verify at the top of the file and match it.)

- [ ] **Step 3: openCollectionEntry** — in `qml/Main.qml`, beside `detailContinue` (~L789). Saved ≠ downloaded ≠ started: ALWAYS opens the detail surface, never player/reader:

```qml
    // A Collection tile always opens the DETAIL surface (saved is a bookmark, not a
    // promise it's downloaded/started). Routes by world + saved snapshot; the gc:/gcd:/
    // locg: prefixes pick the comics lane; manga reopens BY TITLE.
    function openCollectionEntry(e) {
        if (!e || !e.world) return
        var id = String(e.id || "")
        if (e.world === "theatre") {
            win.openTheatreSeries({ "id": id, "type": e.type || "series", "title": e.title || "",
                                    "cover": e.cover || "", "art": (e.payload && e.payload.art) || "" })
        } else if (e.world === "biblio") {
            win.openBook((e.payload && e.payload.book) || e)
        } else if (e.world === "tankoban") {
            if (e.type === "manga") { win.openSeries(e.title || id); return }
            if (id.indexOf("locg:") === 0) {
                win.openComicSeries({ "id": id, "title": e.title || "", "cover": e.cover || "",
                                      "locgMeta": (e.payload && e.payload.locgMeta) || null })
            } else if (id.indexOf("gcd:") === 0) {
                win.openGcdSeries({ "gcdId": parseInt(id.substring(4)), "title": e.title || "", "cover": e.cover || "" })
            } else {
                win.openWestern({ "title": e.title || "", "tag": (e.payload && e.payload.tag) || "",
                                  "tagId": (e.payload && e.payload.tagId) || 0, "cover": e.cover || "" })
            }
        }
    }
```

- [ ] **Step 4: connect line** — in the world-loader wiring block (Main.qml ~L1325–1327, where `continueDetailRequested` etc. connect), add:

```qml
                if (item.collectionOpenRequested) item.collectionOpenRequested.connect(win.openCollectionEntry)
```

- [ ] **Step 5: Extend the contract** — append to `tests/test_collection_p0.ps1` before the final `Write-Host`:

```powershell
$libBtn = Read-File "qml/LibraryButton.qml"
Assert-Contains $libBtn 'Collection.has(world, String(entry.id))' "LibraryButton reads live saved state"
Assert-Contains $libBtn 'Collection.revision' "LibraryButton names revision for reactivity"
$crow = Read-File "qml/ContinueRow.qml"
Assert-Contains $crow 'forgetHandler' "ContinueRow grew the forgetHandler seam"
Assert-Contains $crow 'Progress.forget(modelData.kind, modelData.id)' "default Progress wiring survives"
$mainQml = Read-File "qml/Main.qml"
Assert-Contains $mainQml 'function openCollectionEntry(' "Main.qml routes collection clicks"
Assert-Contains $mainQml 'collectionOpenRequested.connect(win.openCollectionEntry)' "world loaders connect the signal"
```

Run: `powershell -ExecutionPolicy Bypass -File tests/test_collection_p0.ps1` → `test_collection_p0 PASSED`.

- [ ] **Step 6: Commit + push**

```bash
git add qml/LibraryButton.qml qml/ContinueRow.qml qml/Main.qml tests/test_collection_p0.ps1
git commit qml/LibraryButton.qml qml/ContinueRow.qml qml/Main.qml tests/test_collection_p0.ps1 -m "[Agent 4 (Claude), player] Collection plumbing: LibraryButton, ContinueRow forget seam, openCollectionEntry router"
git pull --rebase --autostash && git push origin master
```

---

### Task 4: ＋ Library on Theatre detail

**Files:**
- Modify: `qml/TheatreSeries.qml` (actions Row L656–698; helper function near `currentId()` ~L56)
- Modify: `tests/test_collection_p0.ps1`

- [ ] **Step 1: Snapshot helper** — near `currentId()` (~L56–59):

```qml
    // The Collection snapshot. itemData.id preserved over resolvedId: anime ids
    // (mal:/kitsu:) pivot to tt… after the kitsu→imdb hop — save the door we entered by.
    function collectionEntry() {
        return { "id": String((itemData && itemData.id) ? itemData.id : resolvedId),
                 "type": mediaType, "title": title, "cover": cover,
                 "payload": { "art": banner } }
    }
```

- [ ] **Step 2: The button** — inside the actions `Row` (L656–698), AFTER the Watch Rectangle (note the Row is currently `visible: page.mediaType !== "series"` — MOVE that visibility onto the Watch Rectangle itself so the Row can host the always-visible Library button; Task 13 makes Watch series-capable anyway):

```qml
                            LibraryButton {
                                world: "theatre"
                                entry: page.collectionEntry()
                            }
```

- [ ] **Step 3: Contract** — append to `tests/test_collection_p0.ps1`:

```powershell
$ts = Read-File "qml/TheatreSeries.qml"
Assert-Contains $ts 'function collectionEntry()' "TheatreSeries snapshots its identity"
Assert-Contains $ts 'LibraryButton {' "TheatreSeries carries the button"
```

Run the ps1 → PASSED. Boot smoke (see gotchas) → no QML errors; eyes-on optional here (Task 11 gates it).

- [ ] **Step 4: Commit + push**

```bash
git add qml/TheatreSeries.qml tests/test_collection_p0.ps1
git commit qml/TheatreSeries.qml tests/test_collection_p0.ps1 -m "[Agent 4 (Claude), player] + Library on Theatre detail (movie + series)"
git pull --rebase --autostash && git push origin master
```

---

### Task 5: Theatre world row

**Files:**
- Modify: `qml/TheatreWorld.qml` (root signal near top; row after Continue block L154–160)
- Modify: `tests/test_collection_p0.ps1`

- [ ] **Step 1: Root signal** — beside the existing properties (~L26): `signal collectionOpenRequested(var entry)`

- [ ] **Step 2: The row** — directly AFTER the Continue Watching ContinueRow (L154–160), so the board reads Next Up → Continue → Your Collection (ruling: Collection last of the personal shelves):

```qml
    ContinueRow {
        title: "Your Collection"
        items: (Collection.revision, Collection.items("theatre"))
        forgetHandler: function(e) { Collection.remove("theatre", String(e.id)) }
        onDetailRequested: function(item) { theatre.collectionOpenRequested(item) }
        onResumeRequested: function(item) { theatre.collectionOpenRequested(item) }   // saved ≠ started: both open detail
    }
```

(`theatre` = the root id of TheatreWorld.qml — verify and match. ContinueRow hides itself when empty.)

- [ ] **Step 3: Contract** — append:

```powershell
$tw = Read-File "qml/TheatreWorld.qml"
Assert-Contains $tw 'Collection.items("theatre")' "Theatre world carries the Collection row"
Assert-Contains $tw 'signal collectionOpenRequested' "Theatre world bubbles collection clicks"
```

Run ps1 → PASSED.

- [ ] **Step 4: Commit + push**

```bash
git add qml/TheatreWorld.qml tests/test_collection_p0.ps1
git commit qml/TheatreWorld.qml tests/test_collection_p0.ps1 -m "[Agent 4 (Claude), player] Your Collection row on the Theatre world"
git pull --rebase --autostash && git push origin master
```

---

### Task 6: Biblio — wire the dead button + world row

**Files:**
- Modify: `qml/BiblioBook.qml` (replace the inert Rectangle L324–333)
- Modify: `qml/BiblioWorld.qml` (row after Continue L45–51 + root signal)
- Modify: `tests/test_collection_p0.ps1`

- [ ] **Step 1: Replace the dead button** — swap the whole inert Rectangle (L324–333) for (keeps its block shape; `detail` = BiblioBook's root id — verify and match):

```qml
                LibraryButton {
                    width: parent.width
                    height: 50
                    radius: 13
                    world: "biblio"
                    entry: ({ "id": detail.pairKey, "type": "book",
                              "title": detail.book.title || "", "cover": detail.book.cover || "",
                              "payload": { "book": detail.book } })
                }
```

- [ ] **Step 2: World row** — in `qml/BiblioWorld.qml`, root signal `signal collectionOpenRequested(var entry)`, then after the Continue Reading row (L45–51):

```qml
    ContinueRow {
        title: "Your Collection"
        items: (Collection.revision, Collection.items("biblio"))
        forgetHandler: function(e) { Collection.remove("biblio", String(e.id)) }
        onDetailRequested: function(item) { biblio.collectionOpenRequested(item) }
        onResumeRequested: function(item) { biblio.collectionOpenRequested(item) }
    }
```

(`biblio` = BiblioWorld's root id — verify and match.)

- [ ] **Step 3: Contract** — append:

```powershell
$bb = Read-File "qml/BiblioBook.qml"
Assert-Contains $bb 'world: "biblio"' "BiblioBook button is live"
$bw = Read-File "qml/BiblioWorld.qml"
Assert-Contains $bw 'Collection.items("biblio")' "Biblio world carries the Collection row"
```

Run ps1 → PASSED.

- [ ] **Step 4: Commit + push**

```bash
git add qml/BiblioBook.qml qml/BiblioWorld.qml tests/test_collection_p0.ps1
git commit qml/BiblioBook.qml qml/BiblioWorld.qml tests/test_collection_p0.ps1 -m "[Agent 4 (Claude), player] Biblio: the dead + Library button finally does something + world row"
git pull --rebase --autostash && git push origin master
```

---

### Task 7: Manga + Comics buttons (three surfaces)

**Files:**
- Modify: `qml/MangaSeries.qml` (hero Row L392–447)
- Modify: `qml/ComicSeries.qml` (hero L286–337 — this hero has NO CTA today; the button is its first)
- Modify: `qml/ComicSeriesPage.qml` (header/actions area — locate the title/meta block and place beside it)
- Modify: `tests/test_collection_p0.ps1`

- [ ] **Step 1: MangaSeries** — inside the hero `Row` (L392–447), after the Tankoban toggle:

```qml
                LibraryButton {
                    world: "tankoban"
                    entry: ({ "id": page.seriesTitle, "type": "manga",
                              "title": page.seriesTitle, "cover": page.cover, "payload": ({}) })
                }
```

(`page` = MangaSeries' root id — verify and match; same below.)

- [ ] **Step 2: ComicSeries** — in the hero metadata block (L286–337), beneath the title/meta lines:

```qml
                LibraryButton {
                    world: "tankoban"
                    entry: ({ "id": page.seriesId, "type": "comic",
                              "title": page.seriesTitle, "cover": page.poster,
                              "payload": ({ "tag": page.tagSlug, "tagId": page.tagId }) })
                }
```

- [ ] **Step 3: ComicSeriesPage** — in its header area beside the title:

```qml
                LibraryButton {
                    world: "tankoban"
                    entry: ({ "id": page.locgId, "type": "comic",
                              "title": page.seriesTitle, "cover": page.cover,
                              "payload": ({ "locgMeta": page.locgMeta }) })
                }
```

- [ ] **Step 4: Contract** — append:

```powershell
foreach ($f in @("qml/MangaSeries.qml", "qml/ComicSeries.qml", "qml/ComicSeriesPage.qml")) {
    Assert-Contains (Read-File $f) 'world: "tankoban"' "$f carries the button"
}
```

Run ps1 → PASSED. Boot smoke → clean.

- [ ] **Step 5: Commit + push**

```bash
git add qml/MangaSeries.qml qml/ComicSeries.qml qml/ComicSeriesPage.qml tests/test_collection_p0.ps1
git commit qml/MangaSeries.qml qml/ComicSeries.qml qml/ComicSeriesPage.qml tests/test_collection_p0.ps1 -m "[Agent 4 (Claude), player] + Library on manga + both comics detail surfaces"
git pull --rebase --autostash && git push origin master
```

---

### Task 8: Tankoban per-tab rows

**Files:**
- Modify: `qml/TankobanWorld.qml` (root signal + tab-Loader onLoaded connect, Loader at L195–216)
- Modify: `qml/TankobanMangaTab.qml`, `qml/TankobanComicsTab.qml` (row at the TOP of each tab's content column, above browse rows)
- Modify: `tests/test_collection_p0.ps1`

Ruling: Collection filters PER TAB by entry type. Blended Continue/Next Up above the tab bar stay untouched.

- [ ] **Step 1: Signal bubble** — `TankobanWorld.qml` root: `signal collectionOpenRequested(var entry)`. In the tab Loader's `onLoaded` (L195–216):

```qml
            if (item.collectionOpenRequested) item.collectionOpenRequested.connect(tanko.collectionOpenRequested)
```

(`tanko` = TankobanWorld's root id — verify and match.)

- [ ] **Step 2: Manga tab row** — top of `TankobanMangaTab.qml`'s content column; root gains `signal collectionOpenRequested(var entry)`:

```qml
    ContinueRow {
        title: "Your Collection"
        items: (Collection.revision, Collection.items("tankoban").filter(function(e) { return e.type === "manga" }))
        forgetHandler: function(e) { Collection.remove("tankoban", String(e.id)) }
        onDetailRequested: function(item) { collectionOpenRequested(item) }
        onResumeRequested: function(item) { collectionOpenRequested(item) }
    }
```

- [ ] **Step 3: Comics tab row** — same in `TankobanComicsTab.qml` with `e.type === "comic"`.

- [ ] **Step 4: Contract** — append:

```powershell
Assert-Contains (Read-File "qml/TankobanMangaTab.qml") 'e.type === "manga"' "Manga tab filters its Collection"
Assert-Contains (Read-File "qml/TankobanComicsTab.qml") 'e.type === "comic"' "Comics tab filters its Collection"
Assert-Contains (Read-File "qml/TankobanWorld.qml") 'collectionOpenRequested.connect(tanko.collectionOpenRequested)' "tab clicks bubble to the world"
```

Run ps1 → PASSED. Boot smoke → clean.

- [ ] **Step 5: Commit + push**

```bash
git add qml/TankobanWorld.qml qml/TankobanMangaTab.qml qml/TankobanComicsTab.qml tests/test_collection_p0.ps1
git commit qml/TankobanWorld.qml qml/TankobanMangaTab.qml qml/TankobanComicsTab.qml tests/test_collection_p0.ps1 -m "[Agent 4 (Claude), player] Tankoban Collection rows, filtered per Manga|Comics tab"
git pull --rebase --autostash && git push origin master
```

---

### Task 9: Sub-arc A gate — boot smoke + chat wire announce

- [ ] **Step 1:** Full contract sweep: `powershell -ExecutionPolicy Bypass -File tests/test_collection_p0.ps1` → PASSED. Also re-run `tests/test_continue_tile_p0.ps1` (we touched ContinueRow) → PASSED.
- [ ] **Step 2:** Boot smoke per gotchas → no QML errors in the log.
- [ ] **Step 3:** Announce on the haven chat wire — append to `c:/Users/Suprabha/Desktop/Brotherhood/agents/chat.md` (the BROTHERHOOD repo, not Colosseum) an entry titled `## [Agent 4 (Claude), player] 2026-07-19 — A1/A2: your detail pages grew a + Library button (Collection arc)` naming: the store API (`Collection.add/remove/has/items/revision`), which of their files gained a LibraryButton (BiblioBook.qml, MangaSeries.qml, ComicSeries.qml, ComicSeriesPage.qml) and the per-tab rows, per Hemanth's all-world ruling. Commit that in the Brotherhood repo (its own git root) with pathspec `agents/chat.md`.
- [ ] **Step 4:** Hemanth eyes-on gate (the arc's real acceptance): save from three worlds, watch each shelf fill, toggle off, watch it leave.

---

# Sub-arc B — AF2 Theatre detail (episodes UNTOUCHED throughout)

### Task 10: TheatreFacts.js (pure logic, TDD via Node)

**Files:**
- Create: `tests/test_theatre_facts.js`
- Create: `qml/TheatreFacts.js`

- [ ] **Step 1: Failing test** — `tests/test_theatre_facts.js`:

```js
// TheatreFacts.factRows — pure-logic contract. AF2 discipline: a row with a blank
// value is OMITTED (hide-when-blank), never rendered empty.
const fs = require("fs"), path = require("path")
const src = fs.readFileSync(path.join(__dirname, "..", "qml", "TheatreFacts.js"), "utf8")
    .replace(/^\.pragma library\s*/m, "").replace(/^\.import .*$/gm, "")
const lib = {}
new Function("exports", src + "\nexports.factRows = factRows;")(lib)

function assert(c, m) { if (!c) { console.error("FAIL: " + m); process.exit(1) } }

const meta = {
    director: ["Tetsuro Araki"], writer: ["Hajime Isayama", "Yasuko Kobayashi"],
    country: "Japan", releaseInfo: "2013-", status: ""
}
const rows = lib.factRows(meta, { studio: "WIT Studio", source: "Manga" })
const keys = rows.map(r => r.k)
assert(keys.indexOf("Director") >= 0, "director row present")
assert(keys.indexOf("Studio") >= 0, "anime extras merge in (studio)")
assert(keys.indexOf("Source") >= 0, "anime extras merge in (source)")
assert(keys.indexOf("Network") < 0, "blank network row OMITTED")
assert(keys.indexOf("Status") < 0, "blank status row OMITTED")
assert(rows.filter(r => r.k === "Writers")[0].v === "Hajime Isayama, Yasuko Kobayashi", "arrays join with commas")
assert(rows.filter(r => r.k === "Aired")[0].v === "2013-", "releaseInfo becomes Aired")
assert(lib.factRows(null).length === 0, "null meta yields no rows")
assert(lib.factRows({}, null).length === 0, "empty meta yields no rows")
const many = lib.factRows({ director: ["A", "B", "C", "D", "E"] })
assert(many[0].v === "A, B, C", "name lists cap at 3")
console.log("TheatreFacts contract passed.")
```

Run: `node tests/test_theatre_facts.js` → FAIL (TheatreFacts.js missing).

- [ ] **Step 2: Implement** — `qml/TheatreFacts.js`:

```js
.pragma library
// AF2 fact block assembly — pure and headless-testable. Input: the Cinemeta meta
// object (fields we previously discarded) + optional anime extras from AniList
// ({studio, source}). Output: [{k, v}] with blank rows OMITTED (hide-when-blank).

function joinNames(v) {
    if (!v) return ""
    if (Array.isArray(v)) return v.filter(Boolean).slice(0, 3).join(", ")
    return String(v)
}

function factRows(meta, extras) {
    var rows = []
    function push(k, v) { var s = joinNames(v); if (s) rows.push({ "k": k, "v": s }) }
    if (meta) {
        push("Director", meta.director)
        push("Writers", meta.writer)
    }
    if (extras) push("Studio", extras.studio)
    if (meta) {
        push("Network", meta.network)
        push("Country", meta.country)
        push("Aired", meta.releaseInfo || (meta.released ? String(meta.released).slice(0, 10) : ""))
        push("Status", meta.status)
    }
    if (extras) push("Source", extras.source)
    return rows
}
```

- [ ] **Step 3: Run** — `node tests/test_theatre_facts.js` → `TheatreFacts contract passed.`

- [ ] **Step 4: Commit + push**

```bash
git add qml/TheatreFacts.js tests/test_theatre_facts.js
git commit qml/TheatreFacts.js tests/test_theatre_facts.js -m "[Agent 4 (Claude), player] T1: fact-row assembly, hide-when-blank (node contract green)"
git pull --rebase --autostash && git push origin master
```

---

### Task 11: T1 — fact block UI

**Files:**
- Modify: `qml/TheatreSeries.qml` (import; resolve() callback L422–445; synopsis block L702–714)
- Create: `tests/test_theatre_af2_p0.ps1`

- [ ] **Step 1:** Add `import "TheatreFacts.js" as TheatreFacts` beside the existing JS imports at the top of TheatreSeries.qml, and a property near the other meta properties (~L27): `property var factRows: []` plus `property var castNames: []` (T2 uses it; capture both in one resolve edit).

- [ ] **Step 2:** In the `resolve()` meta callback (after L439 `synopsis = ...`):

```qml
                page.factRows = TheatreFacts.factRows(meta, null)
                page.castNames = meta.cast || []
```

- [ ] **Step 3:** Wrap the synopsis (L702–714) and the new facts column in a `Row` (preserve the synopsis Text's current margins on the Row; cap synopsis width at 580 per the mock instead of 880):

```qml
                Row {
                    spacing: 56
                    // (existing synopsis Text moves here unchanged, width: 580)
                    Column {
                        spacing: 10
                        visible: page.factRows.length > 0
                        Repeater {
                            model: page.factRows
                            Row {
                                spacing: 18
                                Text { text: modelData.k; color: theme.inkFaint; width: 90
                                       font.family: theme.ui; font.pixelSize: 13 }
                                Text { text: modelData.v; color: theme.ink
                                       font.family: theme.ui; font.pixelSize: 13 }
                            }
                        }
                    }
                }
```

(If `theme.inkFaint` doesn't exist, use the page's existing dim ink token — match whatever the meta line at L601–655 uses for its dot separators.)

- [ ] **Step 4: New contract** — `tests/test_theatre_af2_p0.ps1`:

```powershell
# AF2 Theatre detail — wiring contracts. Guards the episode machinery too.
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
function Read-File($p) { Get-Content -Raw (Join-Path $root $p) }
function Assert-Contains($hay, $needle, $msg) {
    if ($hay -notlike "*$needle*") { Write-Host "FAIL: $msg"; exit 1 }
}
$ts = Read-File "qml/TheatreSeries.qml"
Assert-Contains $ts 'TheatreFacts.factRows(meta' "resolve() assembles fact rows"
Assert-Contains $ts 'meta.cast || []' "resolve() captures cast names"
# The episode list is LAW — these needles must survive every AF2 task.
Assert-Contains $ts 'id: episodesSection' "episode section intact"
Assert-Contains $ts 'sources.show("series", page.episodeStreamId(ep.modelData)' "episode-row playback intact"
Write-Host "test_theatre_af2_p0 PASSED"
```

Run node test + ps1 → both green. Boot smoke → clean.

- [ ] **Step 5: Commit + push**

```bash
git add qml/TheatreSeries.qml tests/test_theatre_af2_p0.ps1
git commit qml/TheatreSeries.qml tests/test_theatre_af2_p0.ps1 -m "[Agent 4 (Claude), player] T1: AF2 fact block beside the plot, hide-when-blank"
git pull --rebase --autostash && git push origin master
```

---

### Task 12: T1 — series Watch button

**Files:**
- Modify: `qml/TheatreSeries.qml` (actions Row L656–698 — Task 4 already moved `visible` onto the Watch Rectangle)
- Modify: `tests/test_theatre_af2_p0.ps1`

- [ ] **Step 1: Helper** — near `episodeStreamId()` (~L233):

```qml
    // The hero Watch target for a series: the first visible episode of the default
    // season (the Continue row owns resume; this is the front door). Null-safe.
    function heroEpisode() {
        return (mediaType === "series" && episodes && episodes.length) ? episodes[0] : null
    }
```

- [ ] **Step 2:** On the Watch Rectangle: change `visible` to `page.mediaType !== "series" || page.heroEpisode() !== null`; label text to:

```qml
                                    text: page.mediaType === "series" && page.heroEpisode()
                                        ? "Watch  S" + page.episodeSeason(page.heroEpisode()) + " · E" + page.episodeDisplayNumber(page.heroEpisode())
                                        : "Watch"
```

and the `onClicked` (L690) to branch — series reuses the EXACT episode-row path (L1385's call, verbatim shape):

```qml
                                onClicked: {
                                    if (page.mediaType === "series") {
                                        var ep = page.heroEpisode()
                                        if (!ep) return
                                        sources.show("series", page.episodeStreamId(ep),
                                                     page.title + " - S" + page.episodeSeason(ep) + "E" + page.episodeNumber(ep),
                                                     Object.assign({
                                                         "title": page.title,
                                                         "metaLine": page.episodeSourceLine(ep),
                                                         "backdrop": page.sourceBackdrop()
                                                     }, page.adjacentEpisodeContext(ep)))
                                    } else {
                                        sources.show("movie", page.currentId(), page.title, {
                                            "title": page.title,
                                            "year": page.year,
                                            "metaLine": page.sourceMetaLine(),
                                            "backdrop": page.sourceBackdrop()
                                        })
                                    }
                                }
```

- [ ] **Step 3: Contract** — append to `tests/test_theatre_af2_p0.ps1` before the final line:

```powershell
Assert-Contains $ts 'function heroEpisode()' "series Watch has a target"
Assert-Contains $ts 'page.episodeStreamId(ep)' "series Watch reuses the episode-row pipeline"
```

Run ps1 → PASSED. Boot smoke → clean; open a series eyes-on later (Task 15 gate).

- [ ] **Step 4: Commit + push**

```bash
git add qml/TheatreSeries.qml tests/test_theatre_af2_p0.ps1
git commit qml/TheatreSeries.qml tests/test_theatre_af2_p0.ps1 -m "[Agent 4 (Claude), player] T1: the series hero finally has a Watch button (AF2 primary action)"
git pull --rebase --autostash && git push origin master
```

---

### Task 13: T2 — cast plumbing (TDD on the source pick)

**Files:**
- Create: `tests/test_theatre_cast_pick.js`
- Modify: `qml/TheatreApi.js` (add `postJson`, `animeIdFor`, `loadAnimeCast`)

- [ ] **Step 1: Failing test** — `tests/test_theatre_cast_pick.js`:

```js
// Cast-source discriminator: anime (mal:/anilist: ORIGINAL requested id) → AniList
// faces; kitsu:/anidb: have no AniList key → fall to Cinemeta names; tt… → names.
// Keyed off the door we ENTERED by, never resolvedId (anime pivots to tt… post-load).
const fs = require("fs"), path = require("path")
const src = fs.readFileSync(path.join(__dirname, "..", "qml", "TheatreApi.js"), "utf8")
    .replace(/^\.pragma library\s*/m, "").replace(/^\.import .*$/gm, "")
const lib = {}
new Function("exports", src + "\nexports.animeIdFor = animeIdFor;")(lib)
function assert(c, m) { if (!c) { console.error("FAIL: " + m); process.exit(1) } }
assert(JSON.stringify(lib.animeIdFor("mal:16498")) === JSON.stringify({ site: "mal", id: 16498 }), "mal id parses")
assert(JSON.stringify(lib.animeIdFor("anilist:16498")) === JSON.stringify({ site: "anilist", id: 16498 }), "anilist id parses")
assert(lib.animeIdFor("kitsu:7442") === null, "kitsu has no AniList key -> names fallback")
assert(lib.animeIdFor("anidb:9541") === null, "anidb -> names fallback")
assert(lib.animeIdFor("tt0388629") === null, "live-action -> names")
assert(lib.animeIdFor("") === null && lib.animeIdFor(null) === null, "blank-safe")
console.log("Cast-pick contract passed.")
```

Run: `node tests/test_theatre_cast_pick.js` → FAIL (`animeIdFor` not defined).

- [ ] **Step 2: Implement** — append to `qml/TheatreApi.js` (before any final export comment):

```js
// ---- AF2 cast lane -------------------------------------------------------
// Anime cast comes from AniList (face art + VAs); everything else uses the
// name-only `cast` field already in the Cinemeta meta. The discriminator is
// the ORIGINAL requested id (anime ids pivot to tt… after the kitsu→imdb hop).

function postJson(url, body, done) {
    var xhr = new XMLHttpRequest()
    xhr.open("POST", url)
    xhr.setRequestHeader("Content-Type", "application/json")
    xhr.timeout = 9000
    xhr.onreadystatechange = function() {
        if (xhr.readyState !== XMLHttpRequest.DONE) return
        if (xhr.status < 200 || xhr.status >= 300) { done(null); return }
        try { done(JSON.parse(xhr.responseText)) } catch (e) { done(null) }
    }
    xhr.ontimeout = function() { done(null) }
    xhr.onerror = function() { done(null) }
    xhr.send(JSON.stringify(body))
}

function animeIdFor(requestedId) {
    var m = String(requestedId || "").match(/^(mal|anilist):(\d+)$/)
    return m ? { "site": m[1], "id": parseInt(m[2]) } : null
}

// done({cast: [{name, role, image}], studio, source}) — or done(null) to fall
// back to Cinemeta names. `name` = the voice actor (mock anatomy), `role` = the
// character; image = character art.
function loadAnimeCast(requestedId, done) {
    var ref = animeIdFor(requestedId)
    if (!ref) { done(null); return }
    var filter = ref.site === "mal" ? ("idMal:" + ref.id) : ("id:" + ref.id)
    var q = "query{Media(" + filter + ",type:ANIME){source(version:3)"
        + " studios(isMain:true){nodes{name}}"
        + " characters(sort:ROLE,perPage:12){edges{node{name{full} image{large}}"
        + " voiceActors(language:JAPANESE){name{full}}}}}}"
    postJson("https://graphql.anilist.co", { "query": q }, function(json) {
        var media = json && json.data && json.data.Media
        if (!media) { done(null); return }
        var cast = []
        var edges = (media.characters && media.characters.edges) || []
        for (var i = 0; i < edges.length; i++) {
            var ch = edges[i].node || {}
            var va = (edges[i].voiceActors && edges[i].voiceActors[0]) || null
            cast.push({ "name": va && va.name ? va.name.full : (ch.name ? ch.name.full : ""),
                        "role": ch.name ? ch.name.full : "",
                        "image": ch.image ? (ch.image.large || "") : "" })
        }
        var studios = (media.studios && media.studios.nodes) || []
        var src = media.source ? String(media.source).replace(/_/g, " ").toLowerCase() : ""
        done({ "cast": cast,
               "studio": studios.length ? studios[0].name : "",
               "source": src ? src.charAt(0).toUpperCase() + src.slice(1) : "" })
    })
}
```

- [ ] **Step 3: Run** — `node tests/test_theatre_cast_pick.js` → `Cast-pick contract passed.` And re-run `node tests/test_theatre_facts.js` (still green).

- [ ] **Step 4: Commit + push**

```bash
git add qml/TheatreApi.js tests/test_theatre_cast_pick.js
git commit qml/TheatreApi.js tests/test_theatre_cast_pick.js -m "[Agent 4 (Claude), player] T2: cast plumbing — AniList faces for anime, Cinemeta names otherwise"
git pull --rebase --autostash && git push origin master
```

---

### Task 14: T2 — CastRow below the episodes

**Files:**
- Create: `qml/CastRow.qml`
- Modify: `qml/TheatreSeries.qml` (anime-door capture in `resolve()`; fetch after meta lands; row after `episodesSection`)
- Modify: `tests/test_theatre_af2_p0.ps1`

- [ ] **Step 1: CastRow.qml**:

```qml
import QtQuick

// AF2 Cast row — faces where the source gives them (AniList character art),
// initialed monograms where it doesn't (Cinemeta is name-only). Hides when empty.
// people: [{name, role, image}]. Slide-in per house Behavior convention.
Column {
    id: castRow
    property var people: []
    property bool expanded: false
    visible: people.length > 0
    spacing: 16
    opacity: people.length > 0 ? 1.0 : 0.0
    Behavior on opacity { NumberAnimation { duration: 420; easing.type: Easing.OutCubic } }
    transform: Translate {
        y: castRow.people.length > 0 ? 0 : 24   // AF2 slide-in: rows glide up as data lands
        Behavior on y { NumberAnimation { duration: 620; easing.type: Easing.OutCubic } }
    }

    function initials(name) {
        var p = String(name || "").trim().split(/\s+/)
        return ((p[0] ? p[0][0] : "") + (p.length > 1 ? p[p.length - 1][0] : "")).toUpperCase()
    }

    Text {
        text: "CAST"
        color: theme.inkDim
        font.family: theme.ui; font.pixelSize: 12; font.letterSpacing: 2.5
    }
    Flow {
        width: parent.width
        spacing: 28
        Repeater {
            // Collapsed: first 8 + the expander tail. Expanded: everyone.
            model: castRow.expanded ? castRow.people : castRow.people.slice(0, 8)
            Column {
                width: 96
                spacing: 9
                Rectangle {
                    width: 78; height: 78; radius: 39
                    anchors.horizontalCenter: parent.horizontalCenter
                    color: Qt.rgba(1, 1, 1, 0.06)
                    border.width: 1; border.color: Qt.rgba(1, 1, 1, 0.13)
                    clip: true
                    Image {
                        anchors.fill: parent
                        source: modelData.image || ""
                        visible: (modelData.image || "") !== ""
                        fillMode: Image.PreserveAspectCrop
                    }
                    Text {   // monogram under/instead of art
                        anchors.centerIn: parent
                        visible: (modelData.image || "") === ""
                        text: castRow.initials(modelData.name)
                        color: theme.inkDim
                        font.family: theme.ui; font.pixelSize: 22; font.weight: Font.DemiBold
                    }
                }
                Text {
                    width: parent.width; horizontalAlignment: Text.AlignHCenter
                    text: modelData.name || ""; elide: Text.ElideRight
                    color: theme.ink; font.family: theme.ui; font.pixelSize: 12
                }
                Text {
                    width: parent.width; horizontalAlignment: Text.AlignHCenter
                    text: modelData.role || ""; elide: Text.ElideRight
                    visible: (modelData.role || "") !== ""
                    color: theme.inkDim; font.family: theme.ui; font.pixelSize: 11
                }
            }
        }
        // "All cast ›" expander tail (mock anatomy), only when there's more.
        Column {
            width: 96
            spacing: 9
            visible: !castRow.expanded && castRow.people.length > 8
            Rectangle {
                width: 78; height: 78; radius: 39
                anchors.horizontalCenter: parent.horizontalCenter
                color: Qt.rgba(1, 1, 1, 0.06)
                border.width: 1; border.color: Qt.rgba(1, 1, 1, 0.13)
                Text { anchors.centerIn: parent; text: "›"; color: theme.inkDim
                       font.family: theme.ui; font.pixelSize: 22 }
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                            onClicked: castRow.expanded = true }
            }
            Text {
                width: parent.width; horizontalAlignment: Text.AlignHCenter
                text: "All cast"; color: theme.inkDim
                font.family: theme.ui; font.pixelSize: 12
            }
        }
    }
}
```

- [ ] **Step 2: Wire in TheatreSeries.qml** — new properties near `factRows`: `property var castPeople: []` and `property string animeDoor: ""`. In `resolve()`, BEFORE the `loadMeta` call (~L418), capture the door: `page.animeDoor = String((itemData && itemData.id) || "")`. In the meta callback, after the Task 11 lines:

```qml
                var doorForCast = page.animeDoor
                TheatreApi.loadAnimeCast(doorForCast, function(anime) {
                    if (page.animeDoor !== doorForCast) return   // stale response, page moved on
                    if (anime) {
                        page.castPeople = anime.cast
                        page.factRows = TheatreFacts.factRows(meta, anime)   // Studio + Source rows join
                    } else {
                        page.castPeople = (meta.cast || []).map(function(n) {
                            return { "name": n, "role": "", "image": "" }
                        })
                    }
                })
```

Then AFTER the `episodesSection` Item (L716's closing brace — locate where the section ends, before the sheets/overlays), in the same content container:

```qml
                CastRow {
                    width: parent.width
                    people: page.castPeople
                }
```

(Match the container's horizontal padding to the episode section's own margins.)

- [ ] **Step 3: Contract** — append:

```powershell
Assert-Contains $ts 'TheatreApi.loadAnimeCast' "cast fetch wired"
Assert-Contains $ts 'CastRow {' "cast row present"
Assert-Contains (Read-File "qml/CastRow.qml") 'initials(' "monogram fallback exists"
```

Run ps1 + both node tests → green. Boot smoke → clean.

- [ ] **Step 4: Commit + push**

```bash
git add qml/CastRow.qml qml/TheatreSeries.qml tests/test_theatre_af2_p0.ps1
git commit qml/CastRow.qml qml/TheatreSeries.qml tests/test_theatre_af2_p0.ps1 -m "[Agent 4 (Claude), player] T2: Cast row below the episodes — AniList faces, monogram fallback"
git pull --rebase --autostash && git push origin master
```

---

### Task 15: T3 — More Like This + final gate

**Files:**
- Create: `qml/MoreLikeThisRow.qml`
- Modify: `qml/TheatreApi.js` (add `moreLikeThis`)
- Modify: `qml/TheatreSeries.qml` (fetch + row after CastRow; `openItemRequested` signal)
- Modify: `qml/Main.qml` (connect in theatreSeriesLayer `onLoaded`, ~L1685)
- Modify: `tests/test_theatre_af2_p0.ps1`

- [ ] **Step 1: The fetch** — append to `qml/TheatreApi.js`:

```js
// ---- AF2 More Like This --------------------------------------------------
// Same-genre from OUR catalogs, never a recommendations API. Live-action →
// Cinemeta catalog; anime → the baked MAL DB (malCatalog is PASSED IN by the
// page — .pragma libraries can't see context properties). Excludes self.
// done([{id, type, title, cover}]) — at most 12.
function moreLikeThis(mediaType, requestedId, resolvedId, firstGenre, malCatalog, done) {
    if (!firstGenre) { done([]); return }
    var selfIds = {}
    selfIds[String(requestedId || "")] = true
    selfIds[String(resolvedId || "")] = true
    if (animeIdFor(requestedId) || String(requestedId || "").match(/^(kitsu|anidb):/)) {
        if (!malCatalog || !malCatalog.ready()) { done([]); return }
        var rows = malCatalog.genreEntries("anime", firstGenre, "members", 13) || []
        var out = []
        for (var i = 0; i < rows.length && out.length < 12; i++) {
            var r = rows[i]
            var rid = "mal:" + r.mal_id
            if (selfIds[rid]) continue
            out.push({ "id": rid, "type": "series",
                       "title": r.title_english || r.title || "",
                       "cover": (r.images && r.images.jpg && r.images.jpg.large_image_url) || "" })
        }
        done(out)
        return
    }
    catalogFetch(mediaType, firstGenre, 13, function(cards) {
        var out = []
        for (var i = 0; i < (cards || []).length && out.length < 12; i++) {
            var c = cards[i]
            if (selfIds[String(c.id)]) continue
            out.push({ "id": c.id, "type": c.type, "title": c.title || c.caption || "", "cover": c.cover || "" })
        }
        done(out)
    })
}
```

- [ ] **Step 2: MoreLikeThisRow.qml**:

```qml
import QtQuick

// AF2 "More like this" — poster cards from our own catalogs. Tap opens that
// title's detail. Hides when empty; slides in per house Behavior convention.
Column {
    id: mlt
    property var cards: []   // [{id, type, title, cover}]
    signal openRequested(var item)
    visible: cards.length > 0
    spacing: 16
    opacity: cards.length > 0 ? 1.0 : 0.0
    Behavior on opacity { NumberAnimation { duration: 420; easing.type: Easing.OutCubic } }
    transform: Translate {
        y: mlt.cards.length > 0 ? 0 : 24   // AF2 slide-in: rows glide up as data lands
        Behavior on y { NumberAnimation { duration: 620; easing.type: Easing.OutCubic } }
    }

    Text {
        text: "MORE LIKE THIS"
        color: theme.inkDim
        font.family: theme.ui; font.pixelSize: 12; font.letterSpacing: 2.5
    }
    ListView {
        width: parent.width
        height: 216
        orientation: ListView.Horizontal
        spacing: 18
        clip: true
        model: mlt.cards
        delegate: Column {
            width: 120
            spacing: 9
            Rectangle {
                width: 120; height: 172; radius: 8
                color: Qt.rgba(1, 1, 1, 0.05)
                border.width: 1; border.color: Qt.rgba(1, 1, 1, 0.08)
                clip: true
                Image {
                    anchors.fill: parent
                    source: modelData.cover || ""
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: mlt.openRequested({ "id": modelData.id, "type": modelData.type,
                                                   "title": modelData.title, "cover": modelData.cover })
                }
            }
            Text {
                width: parent.width
                text: modelData.title || ""; elide: Text.ElideRight
                color: theme.ink; font.family: theme.ui; font.pixelSize: 12
            }
        }
    }
}
```

- [ ] **Step 3: Wire in TheatreSeries.qml** — root gains `signal openItemRequested(var item)` (beside `playRequested`, ~L17) and `property var moreLikeCards: []`. In the meta callback (after the cast fetch block):

```qml
                var mltGenre = (meta.genres && meta.genres.length) ? meta.genres[0] : ""
                TheatreApi.moreLikeThis(page.mediaType, page.animeDoor, page.resolvedId, mltGenre,
                                        (typeof MalCatalog !== "undefined") ? MalCatalog : null,
                                        function(cards) { page.moreLikeCards = cards || [] })
```

After the CastRow:

```qml
                MoreLikeThisRow {
                    width: parent.width
                    cards: page.moreLikeCards
                    onOpenRequested: function(item) { page.openItemRequested(item) }
                }
```

In `qml/Main.qml`, inside theatreSeriesLayer's `onLoaded` (~L1685, beside `item.playRequested.connect`):

```qml
            item.openItemRequested.connect(win.openTheatreSeries)
```

(`openTheatreSeries` already handles an active layer: it swaps `itemData` in place — L527–531.)

- [ ] **Step 4: Contract** — append:

```powershell
Assert-Contains $ts 'TheatreApi.moreLikeThis' "MLT fetch wired"
Assert-Contains $ts 'signal openItemRequested' "detail can open a sibling title"
Assert-Contains (Read-File "qml/Main.qml") 'openItemRequested.connect(win.openTheatreSeries)' "Main routes sibling opens"
```

Run ps1 + node tests → green.

- [ ] **Step 5: FINAL GATE** — full sweep: `test_collection_p0.ps1`, `test_theatre_af2_p0.ps1`, `test_continue_tile_p0.ps1`, both node tests, `collection_store_harness.exe`, `progress_store_harness.exe` — ALL green. Boot smoke → clean. Then Hemanth's eyes-on: (a) Collection loop across three worlds; (b) AF2 page vs the mock — fact block, series Watch, Cast, More Like This, episodes UNTOUCHED; (c) an anime title (faces) vs a live-action title (monograms).

- [ ] **Step 6: Commit + push**

```bash
git add qml/MoreLikeThisRow.qml qml/TheatreApi.js qml/TheatreSeries.qml qml/Main.qml tests/test_theatre_af2_p0.ps1
git commit qml/MoreLikeThisRow.qml qml/TheatreApi.js qml/TheatreSeries.qml qml/Main.qml tests/test_theatre_af2_p0.ps1 -m "[Agent 4 (Claude), player] T3: More Like This from our own catalogs + slide-in — the detail page becomes a hub"
git pull --rebase --autostash && git push origin master
```

---

## Execution notes

- Tasks 1→9 (Sub-arc A) strictly before 10→15 (Sub-arc B) — the store must exist before the redesign's button means anything. Within each sub-arc, tasks are sequential (later contracts extend earlier ps1 files).
- Every task leaves the app whole; if the wake is cut short, ship what's green.
- The episode-list needles in `test_theatre_af2_p0.ps1` are the tripwire for the arc's #1 ruling: episodes untouched. If they ever fail, STOP and restore before proceeding.
- MEMORY gotchas that WILL bite: `font.pixelSize` int-only; `.pragma library` files can't see context properties (pass `MalCatalog` in); commit with pathspec; kill colosseum.exe by PID before relink; AniList same-title novels trap doesn't apply here (we query by id, never title).
