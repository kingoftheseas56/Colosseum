# Fresh Book Reader Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build Colosseum's fresh book reader — native QML chrome over a minimal web "paper" (Anx Reader's foliate-js fork) — as a standalone harness exe, then swap it in for the TB2-imported foliate web app.

**Architecture:** One `WebEngineView` ("the paper") renders book pages and nothing else, driven by a small command/event protocol over QWebChannel. All visible UI is QML; all persistent state and networking is C++ (`Reader2Bridge` + extracted `BookStores` helpers writing the SAME AppData JSON files the old reader uses — zero migration). Spec: `docs/superpowers/specs/2026-07-16-colosseum-fresh-book-reader-design.md`. Visual contract: `~/Desktop/Brotherhood/agents/colosseum-book-reader-chrome-mock.html` (open it in a browser; all colors/sizes/behaviors come from there).

**Tech Stack:** Qt 6 QML + QtWebEngine/QWebChannel (MSVC build, `native/build-target.bat`), Anx Reader's foliate-js fork (MIT, vendored), existing JSON stores, existing `AudiobookSession`/`AudiobookStrip` QML for read-along.

**House rules that bind every task:**
- Build with `native/build-target.bat` from repo root. If link fails LNK1104, a leftover exe is running — kill it by PID first.
- One build at a time per out-dir. Commit + push (`git push origin master`) after every green task — this repo is `kingoftheseas56/Colosseum`, `cd` into `Colosseum/` before git.
- `font.pixelSize` must be an int in QML. `.js` imported from QML JS needs `.import`, `.pragma library` files can't use plain `import`.
- QWebChannel corrupts binary `QByteArray` → all book bytes cross the bridge as **base64**.
- Never claim a step done without running its verify command. Pixels are Hemanth's: every "Eyes-on" step means STOP and ask him to look.
- New code ONLY in: `resources/reader2/`, `native/reader2/`, `qml/reader2/`, `tests/reader2_*`. The shipping reader (`resources/book_reader/`, `qml/BookReader.qml`) is untouched until Task 16.

---

### Task 1: Vendor the paper (Anx foliate fork) + browser proof

**Files:**
- Create: `resources/reader2/vendor/foliate-anx/` (vendored tree)
- Create: `docs/superpowers/specs/2026-07-16-reader2-paper-api-notes.md` (recorded API facts)

- [ ] **Step 1: Fetch the donor at a pinned commit**

```bash
cd /c/Users/Suprabha/Desktop/Brotherhood/Colosseum
git clone --depth 1 https://github.com/anxcye/anx-reader /tmp/anx-reader
cd /tmp/anx-reader && git rev-parse HEAD   # record this SHA in the notes doc
```

- [ ] **Step 2: Copy the fork + license attribution**

```bash
mkdir -p /c/Users/Suprabha/Desktop/Brotherhood/Colosseum/resources/reader2/vendor
cp -r /tmp/anx-reader/assets/foliate-js /c/Users/Suprabha/Desktop/Brotherhood/Colosseum/resources/reader2/vendor/foliate-anx
```
Then check what came over: `ls resources/reader2/vendor/foliate-anx/` — expect `src/`, possibly `dist/`, `index.html`, `debug.html`, `README.md`, `package.json`. If `dist/` is absent (gitignored), we use `src/` directly as ES modules — foliate-js is dependency-free ESM; webpack is only their packaging convenience. Delete `node_modules` if it copied. Keep `README.md` (it carries the MIT attribution chain).

- [ ] **Step 3: Boot their debug page in a plain browser with a real EPUB**

Open `resources/reader2/vendor/foliate-anx/debug.html` (or `index.html`) directly in Chrome/Edge via a throwaway local server (file:// blocks ESM):

```bash
cd /c/Users/Suprabha/Desktop/Brotherhood/Colosseum/resources/reader2/vendor/foliate-anx
python -m http.server 8971
# browser → http://localhost:8971/debug.html — open any .epub from the downloads folder
```
Expected: the book renders and pages turn. If `debug.html` needs their Flutter channel shims, note which globals it expects — that IS the bridge vocabulary we mimic.

- [ ] **Step 4: Record the API facts (this resolves spec open-items 1 and 2)**

Write `docs/superpowers/specs/2026-07-16-reader2-paper-api-notes.md` with, verified from their `src/`:
- the entry module and how a view is created (upstream shape: `<foliate-view>` element, `view.open(file)`, `view.goTo()`, `view.prev()/next()`, `relocate` + `load` events, `view.addAnnotation()/deleteAnnotation()` over `overlayer`, `view.search()`);
- the exact event names/payloads their fork emits (grep `src/` for `dispatchEvent|postMessage|channel`);
- whether TXT and PDF open (try one of each in the debug page) — record VERDICT per format;
- the pinned donor SHA.
This file is the reference every later task's glue code is adjusted against — where a name below differs from the recorded fact, **the notes file wins**.

- [ ] **Step 5: Commit**

```bash
cd /c/Users/Suprabha/Desktop/Brotherhood/Colosseum
git add resources/reader2/vendor/foliate-anx docs/superpowers/specs/2026-07-16-reader2-paper-api-notes.md
git commit -m "feat(reader2): vendor Anx foliate-js fork (MIT) + paper API notes" && git push origin master
```

---

### Task 2: The paper page + glue protocol (browser-first)

**Files:**
- Create: `resources/reader2/paper.html`
- Create: `resources/reader2/paper_glue.js`
- Create: `resources/reader2/mock_bridge.js` (browser-only test double)
- Copy: `resources/book_reader/qwebchannel.js` → `resources/reader2/qwebchannel.js`

The paper speaks exactly this protocol — **commands down** as `window.paper.*` calls, **events up** as `bridge.paperEvent(name, jsonString)`:

| Command (QML → paper) | Event (paper → native) |
|---|---|
| `paper.open(path, cfi)` | `ready {toc, metadata, sections}` |
| `paper.next()` / `paper.prev()` | `relocated {cfi, fraction, tocIndex, chapterTitle, pageInChapter, pagesInChapter, percent}` |
| `paper.goTo(target)` (cfi/href/fraction) | `selection {text, cfi, rect:{x,y,w,h}}` |
| `paper.setAppearance(json)` | `searchResults {query, results:[{cfi, excerpt, chapterTitle}], done}` |
| `paper.search(query)` / `paper.clearSearch()` | `highlightTapped {id, rect}` |
| `paper.addHighlight({id, cfi, color})` / `paper.removeHighlight(id)` | `footnote {html, rect}` |
| `paper.clearSelection()` | `error {message}` |

- [ ] **Step 1: Write `paper.html`** — black page, zero UI:

```html
<!DOCTYPE html>
<html><head><meta charset="UTF-8">
<style>html,body{margin:0;height:100%;background:#000;overflow:hidden}</style>
<!-- qwebchannel.js is injected by QML at DocumentCreation in the real app; in the
     browser, mock_bridge.js (added by hand while debugging) plays the bridge. -->
</head><body>
<script type="module" src="paper_glue.js"></script>
</body></html>
```

- [ ] **Step 2: Write `paper_glue.js`** — the only custom JS on the paper:

```js
// paper_glue.js — command/event seam between Colosseum chrome and the foliate paper.
// Commands: window.paper.*   Events: bridge.paperEvent(name, JSON.stringify(payload))
// API names verified against docs/superpowers/specs/2026-07-16-reader2-paper-api-notes.md
import './vendor/foliate-anx/src/view.js'   // defines <foliate-view> (adjust per notes)

const send = (name, payload) => {
  const line = JSON.stringify(payload ?? {})
  if (window.bridge?.paperEvent) window.bridge.paperEvent(name, line)
  else console.log(`[paper-event] ${name} ${line}`)      // browser fallback
}

let view = null

const b64ToFile = (b64, name) => {
  const bin = atob(b64)
  const bytes = new Uint8Array(bin.length)
  for (let i = 0; i < bin.length; i++) bytes[i] = bin.charCodeAt(i)
  return new File([bytes], name)
}

async function open(path, cfi) {
  try {
    if (view) { view.remove(); view = null }
    view = document.createElement('foliate-view')
    document.body.append(view)
    view.addEventListener('relocate', e => {
      const d = e.detail
      send('relocated', {
        cfi: d.cfi, fraction: d.fraction,
        tocIndex: d.tocItem?.index ?? -1, chapterTitle: d.tocItem?.label ?? '',
        pageInChapter: (d.location?.current ?? 0) + 1,
        pagesInChapter: d.location?.total ?? 0,
        percent: Math.round((d.fraction ?? 0) * 100),
      })
    })
    view.addEventListener('load', () => {
      document.addEventListener('selectionchange', reportSelection)
    })
    // Book bytes cross the bridge as BASE64 (QWebChannel corrupts binary QByteArray).
    const b64 = await new Promise(res => window.bridge.filesRead
      ? window.bridge.filesRead(path, res) : res(window.__mockBook))
    await view.open(b64ToFile(b64, path.split(/[\\/]/).pop()))
    const toc = (view.book.toc ?? []).map((t, i) =>
      ({ index: i, label: t.label, href: t.href }))
    send('ready', { toc, metadata: view.book.metadata ?? {},
                    sections: view.book.sections?.length ?? 0 })
    if (cfi) view.goTo(cfi); else view.renderer.next()
  } catch (err) { send('error', { message: String(err) }) }
}

function reportSelection() {
  const sel = view?.getSelection?.() ?? window.getSelection()
  const text = sel?.toString?.() ?? ''
  if (!text.trim()) return
  const r = sel.getRangeAt(0).getBoundingClientRect()
  send('selection', { text, cfi: view.getCFI?.(sel) ?? '',
                      rect: { x: r.x, y: r.y, w: r.width, h: r.height } })
}

window.paper = {
  open,
  next: () => view?.renderer.next(),
  prev: () => view?.renderer.prev(),
  goTo: t => view?.goTo(t),
  clearSelection: () => window.getSelection()?.removeAllRanges(),
  setAppearance: json => {
    const a = JSON.parse(json)   // {theme:{bg,fg}, font, sizePx, lineHeight, marginPx, justify}
    document.body.style.background = a.theme.bg
    view?.renderer.setStyles?.(`
      html { color: ${a.theme.fg}; background: ${a.theme.bg};
             font-family: '${a.font}', serif; font-size: ${a.sizePx}px;
             line-height: ${a.lineHeight}; }
      p { text-align: ${a.justify ? 'justify' : 'start'}; }`)
    view?.renderer.setAttribute('margin', `${a.marginPx}px`)
  },
  addHighlight: json => {
    const h = JSON.parse(json)
    view?.addAnnotation({ value: h.cfi, color: h.color, id: h.id })
  },
  removeHighlight: id => view?.deleteAnnotation({ id }),
  search: async q => {
    const results = []
    for await (const r of view.search({ query: q })) {
      if (r === 'done') break
      if (r.subitems) for (const s of r.subitems)
        results.push({ cfi: s.cfi, excerpt: s.excerpt?.text ?? '',
                       chapterTitle: r.label ?? '' })
    }
    send('searchResults', { query: q, results, done: true })
  },
  clearSearch: () => view?.clearSearch?.(),
}
send('glueLoaded', {})
```

- [ ] **Step 3: Write `mock_bridge.js`** so the paper is testable in a plain browser:

```js
// mock_bridge.js — browser-only stand-in for the QWebChannel bridge.
// Load AFTER picking a book: window.__mockBook is set by the file input below.
window.bridge = { paperEvent: (n, j) => console.log('[event]', n, j) }
const inp = Object.assign(document.createElement('input'), { type: 'file' })
inp.style.cssText = 'position:fixed;top:8px;left:8px;z-index:99;color:#888'
document.body.append(inp)
inp.onchange = () => {
  const rd = new FileReader()
  rd.onload = () => {
    window.__mockBook = rd.result.split(',')[1]           // strip data: prefix
    window.paper.open(inp.files[0].name, '')
  }
  rd.readAsDataURL(inp.files[0])
}
```
Temporarily add `<script type="module" src="mock_bridge.js"></script>` to a copy `paper_debug.html` (commit that too — it's the permanent browser bench).

- [ ] **Step 4: Verify in the browser**

```bash
cd /c/Users/Suprabha/Desktop/Brotherhood/Colosseum/resources/reader2 && python -m http.server 8971
# browser → http://localhost:8971/paper_debug.html
```
Expected in console: `[paper-event] glueLoaded`, then after picking an EPUB: `ready` with a real TOC, `relocated` on every arrow-key… no wait — keys are QML's job; turn pages via console: `paper.next()` → a `relocated` event logs with sane `percent`/`chapterTitle`. Also verify `paper.setAppearance(...)`, `paper.search('the')`, and a selection logging a `selection` event. Fix glue names against the notes doc until all pass.

- [ ] **Step 5: Commit**

```bash
git add resources/reader2 && git commit -m "feat(reader2): paper page + glue protocol, browser-proven" && git push origin master
```

---

### Task 3: Extract shared store helpers (`BookStores`)

**Files:**
- Create: `native/reader/BookStores.h`, `native/reader/BookStores.cpp`
- Modify: `native/reader/BookBridge.cpp` (delegate to BookStores; behavior identical)
- Create: `tests/reader2_stores_harness.cpp`
- Modify: `native/CMakeLists.txt` (harness target + BookStores in colosseum sources)

- [ ] **Step 1: Write the failing harness test** (pattern: existing `tests/*_harness.cpp`, exit code = verdict):

```cpp
// tests/reader2_stores_harness.cpp — BookStores roundtrip under sandboxed AppData.
#include "reader/BookStores.h"
#include <QCoreApplication>
#include <QStandardPaths>
#include <QJsonObject>
#include <cstdio>

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QStandardPaths::setTestModeEnabled(true);   // sandbox — never touches real stores
    int fails = 0;
    auto check = [&](bool ok, const char* what) {
        if (!ok) { std::printf("FAIL %s\n", what); ++fails; }
        else       std::printf("ok   %s\n", what);
    };
    QJsonObject p{{"cfi", "epubcfi(/6/4!/4/2)"}, {"percent", 42}};
    BookStores::save(QStringLiteral("progress.json"), QStringLiteral("bk1"), p);
    check(BookStores::get(QStringLiteral("progress.json"),
          QStringLiteral("bk1")).value("percent").toInt() == 42, "progress roundtrip");
    QJsonObject bm{{"id", "b1"}, {"cfi", "epubcfi(/6/4!/4/8)"}, {"snippet", "damp, drizzly"}};
    BookStores::listSave(QStringLiteral("bookmarks.json"), QStringLiteral("bk1"), bm);
    check(BookStores::listGet(QStringLiteral("bookmarks.json"),
          QStringLiteral("bk1")).size() == 1, "bookmark listSave/listGet");
    BookStores::listDelete(QStringLiteral("bookmarks.json"),
                           QStringLiteral("bk1"), QStringLiteral("b1"));
    check(BookStores::listGet(QStringLiteral("bookmarks.json"),
          QStringLiteral("bk1")).isEmpty(), "bookmark listDelete");
    std::printf(fails ? "VERDICT: FAIL\n" : "VERDICT: PASS\n");
    return fails ? 1 : 0;
}
```

- [ ] **Step 2: Declare the API** in `native/reader/BookStores.h` by lifting the file-local helpers already inside `BookBridge.cpp` (its `readStore`/`writeStore`/`listGet`/`listSave`/`listDelete`/`listClear` around lines 85–200) into a namespace:

```cpp
#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QString>

// Shared JSON stores under AppData/Colosseum (or the sandbox when
// QStandardPaths::setTestModeEnabled(true)). Same files the old reader wrote:
// progress.json, settings.json, bookmarks.json, annotations.json.
namespace BookStores {
QJsonObject readStore(const QString& fileName);
void        writeStore(const QString& fileName, const QJsonObject& all);
QJsonObject get(const QString& fileName, const QString& bookId);
void        save(const QString& fileName, const QString& bookId, const QJsonObject& data);
QJsonArray  listGet(const QString& fileName, const QString& bookId);
QJsonObject listSave(const QString& fileName, const QString& bookId, QJsonObject item);
QJsonObject listDelete(const QString& fileName, const QString& bookId, const QString& itemId);
void        listClear(const QString& fileName, const QString& bookId);
}
```
`BookStores.cpp` = move the bodies verbatim from `BookBridge.cpp` (they resolve the dir via `QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)` — keep that; test mode redirects it). Then `BookBridge.cpp`'s methods become one-line delegates. **Do not change any store filename or JSON shape** — the whole zero-migration promise rides on this.

- [ ] **Step 3: Add the harness target** to `native/CMakeLists.txt` next to the other harness blocks, and add `reader/BookStores.cpp` + `.h` to the `colosseum` target's source list:

```cmake
add_executable(reader2_stores_harness
    ../tests/reader2_stores_harness.cpp
    reader/BookStores.cpp
    reader/BookStores.h
)
target_include_directories(reader2_stores_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(reader2_stores_harness PRIVATE Qt6::Core)
```
Grep-verify your CMake edit afterwards (multi-agent collision rule): `grep -n "reader2_stores_harness\|BookStores" native/CMakeLists.txt`.

- [ ] **Step 4: Build + run, expect PASS; old reader still boots**

```bash
cd /c/Users/Suprabha/Desktop/Brotherhood/Colosseum && native/build-target.bat
native/build-msvc/reader2_stores_harness.exe    # expect "VERDICT: PASS", exit 0
```
Then boot `colosseum.exe`, open any book in the OLD reader, confirm position/bookmarks still behave (the delegation must be invisible).

- [ ] **Step 5: Commit**

```bash
git add native/reader/BookStores.h native/reader/BookStores.cpp native/reader/BookBridge.cpp native/CMakeLists.txt tests/reader2_stores_harness.cpp
git commit -m "refactor(reader): extract BookStores helpers shared by old bridge and reader2" && git push origin master
```

---

### Task 4: `Reader2Bridge` (the spine)

**Files:**
- Create: `native/reader2/Reader2Bridge.h`, `native/reader2/Reader2Bridge.cpp`
- Create: `tests/reader2_bridge_harness.cpp`
- Modify: `native/CMakeLists.txt`

- [ ] **Step 1: Write the header** — slim, no audiobook/window baggage (that stays on BookBridge until swap):

```cpp
#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>

class QNetworkAccessManager;

// Reader2Bridge — the fresh reader's whole native seam. Registered on the paper's
// QWebChannel as "bridge" AND exposed to QML as context property "Reader2Bridge".
// Paper pulls book bytes (base64) and pushes events; QML reads/writes the stores
// and receives the same events. Networking (dictionary) lives here, never in JS.
class Reader2Bridge : public QObject {
    Q_OBJECT
public:
    explicit Reader2Bridge(QObject* parent = nullptr);

    // paper-facing
    Q_INVOKABLE QString filesRead(const QString& filePath);      // base64, "" on error
    Q_INVOKABLE void paperEvent(const QString& name, const QString& json);

    // QML-facing stores (same files as the old reader — BookStores)
    Q_INVOKABLE QJsonObject progressGet(const QString& bookId);
    Q_INVOKABLE void progressSave(const QString& bookId, const QJsonObject& data);
    Q_INVOKABLE QJsonObject settingsGet();
    Q_INVOKABLE void settingsSave(const QJsonObject& data);
    Q_INVOKABLE QJsonArray bookmarksGet(const QString& bookId);
    Q_INVOKABLE QJsonObject bookmarksSave(const QString& bookId, const QJsonObject& bm);
    Q_INVOKABLE void bookmarksDelete(const QString& bookId, const QString& id);
    Q_INVOKABLE QJsonArray annotationsGet(const QString& bookId);
    Q_INVOKABLE QJsonObject annotationsSave(const QString& bookId, const QJsonObject& an);
    Q_INVOKABLE void annotationsDelete(const QString& bookId, const QString& id);

    // dictionary — Wiktionary REST, C++ side (house rule: no fetch on the paper)
    Q_INVOKABLE void dictLookup(const QString& word);

signals:
    void paperEventReceived(const QString& name, const QString& json);
    void dictResult(const QString& word, const QString& json, bool ok);

private:
    QNetworkAccessManager* m_nam;
};
```

- [ ] **Step 2: Write the implementation.** `filesRead`: `QFile::readAll().toBase64()` (returns `QString` — never raw QByteArray over the channel). `paperEvent`: `emit paperEventReceived(name, json);`. Stores: one-line delegates to `BookStores` with the same filenames the old bridge used (`progress.json`, `settings.json`, `bookmarks.json`, `annotations.json`). `dictLookup`:

```cpp
void Reader2Bridge::dictLookup(const QString& word) {
    const QUrl url(QStringLiteral(
        "https://en.wiktionary.org/api/rest_v1/page/definition/") + QUrl::toPercentEncoding(word));
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Colosseum/1.0"));
    QNetworkReply* rep = m_nam->get(req);
    connect(rep, &QNetworkReply::finished, this, [this, rep, word] {
        rep->deleteLater();
        const bool ok = rep->error() == QNetworkReply::NoError;
        emit dictResult(word, ok ? QString::fromUtf8(rep->readAll()) : QString(), ok);
    });
}
```
(If lookups stall ~21s on this machine, apply the house IPv4-pin NAM factory fix from the memory doctrine — known Windows IPv6 black hole.)

- [ ] **Step 3: Harness test** `tests/reader2_bridge_harness.cpp`: construct the bridge under `QStandardPaths::setTestModeEnabled(true)`; write a temp file, check `filesRead` returns its exact base64; `progressSave`/`progressGet` roundtrip; connect `paperEventReceived`, call `paperEvent("relocated", "{\"percent\":7}")`, assert the signal fired with the same payload. Same PASS/FAIL/exit-code shape as Task 3. Add `reader2_bridge_harness` to CMake (links `Qt6::Core Qt6::Network`, sources: harness + `reader2/Reader2Bridge.cpp` + `reader/BookStores.cpp`).

- [ ] **Step 4: Build + run, expect `VERDICT: PASS`.** Then commit:

```bash
git add native/reader2 tests/reader2_bridge_harness.cpp native/CMakeLists.txt
git commit -m "feat(reader2): Reader2Bridge — stores, base64 filesRead, event relay, native dict" && git push origin master
```

---

### Task 5: The harness exe (first pixels)

**Files:**
- Create: `native/reader2/reader2_harness_main.cpp`
- Create: `qml/reader2/Harness.qml`, `qml/reader2/HarnessShelf.qml`, `qml/reader2/ReaderShell.qml`, `qml/reader2/Paper.qml`
- Create: `reader2.bat` (repo root)
- Modify: `native/CMakeLists.txt`

- [ ] **Step 1: Find the real books download folder** (the shelf reads it, read-only, even in sandbox mode):

```bash
grep -rn "books" native/engine/BookDownloader.h | grep -iE "dir|path|location" | head -5
```
Expected: the method/constant naming the library dir (the folder the old reader opens from). Wire its literal into `HarnessShelf.qml`'s folder scan below.

- [ ] **Step 2: Harness main** — sandbox stores by default, `--real-stores` opts out:

```cpp
// native/reader2/reader2_harness_main.cpp — boots ONLY the fresh reader.
#include "reader2/Reader2Bridge.h"
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QStandardPaths>
#include <QtWebEngineQuick/qtwebenginequickglobal.h>

int main(int argc, char** argv) {
    qputenv("QT_FORCE_STDERR_LOGGING", "1");
    QtWebEngineQuick::initialize();
    QGuiApplication app(argc, argv);
    const bool realStores = app.arguments().contains(QStringLiteral("--real-stores"));
    if (!realStores) QStandardPaths::setTestModeEnabled(true);   // sandbox by default
    qInfo("[reader2] stores: %s", realStores ? "REAL" : "sandbox");
    Reader2Bridge bridge;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("Reader2Bridge"), &bridge);
    engine.load(QUrl::fromLocalFile(QCoreApplication::applicationDirPath()
                + QStringLiteral("/../../qml/reader2/Harness.qml")));
    return engine.rootObjects().isEmpty() ? 1 : app.exec();
}
```

CMake target (WebEngine needs the Quick module):

```cmake
add_executable(reader2_harness
    reader2/reader2_harness_main.cpp
    reader2/Reader2Bridge.cpp
    reader2/Reader2Bridge.h
    reader/BookStores.cpp
    reader/BookStores.h
)
target_include_directories(reader2_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(reader2_harness PRIVATE Qt6::Quick Qt6::Qml Qt6::Network Qt6::WebEngineQuick)
```
`reader2.bat` at repo root: `@native\build-msvc\reader2_harness.exe %*`

- [ ] **Step 3: `Paper.qml`** — the web view wrapper, whole command surface:

```qml
// qml/reader2/Paper.qml — the paper: renders book pages, obeys commands, owns NOTHING.
import QtQuick
import QtWebEngine
import QtWebChannel

Item {
    id: paper
    signal paperEvent(string name, var payload)
    property bool glueUp: false

    function open(path, cfi) { run("window.paper.open(" + JSON.stringify(path) + "," + JSON.stringify(cfi || "") + ")") }
    function next() { run("window.paper.next()") }
    function prev() { run("window.paper.prev()") }
    function goTo(t) { run("window.paper.goTo(" + JSON.stringify(t) + ")") }
    function setAppearance(a) { run("window.paper.setAppearance(" + JSON.stringify(JSON.stringify(a)) + ")") }
    function search(q) { run("window.paper.search(" + JSON.stringify(q) + ")") }
    function clearSearch() { run("window.paper.clearSearch()") }
    function addHighlight(h) { run("window.paper.addHighlight(" + JSON.stringify(JSON.stringify(h)) + ")") }
    function removeHighlight(id) { run("window.paper.removeHighlight(" + JSON.stringify(id) + ")") }
    function clearSelection() { run("window.paper.clearSelection()") }
    function run(js) { web.runJavaScript(js) }

    Component.onCompleted: channel.registerObject("bridge", Reader2Bridge)
    Connections {
        target: Reader2Bridge
        function onPaperEventReceived(name, json) {
            if (name === "glueLoaded") paper.glueUp = true
            paper.paperEvent(name, JSON.parse(json))
        }
    }
    WebEngineView {
        id: web
        anchors.fill: parent
        backgroundColor: "#000000"
        focusPolicy: Qt.NoFocus            // ROOT FIX: the paper never owns a key
        settings.localContentCanAccessFileUrls: true
        webChannel: WebChannel { id: channel }
        url: Qt.resolvedUrl("../../resources/reader2/paper.html")
        // inject qwebchannel + shim at DocumentCreation exactly as BookReader.qml does
        userScripts.collection: [
            WebEngineScript { injectionPoint: WebEngineScript.DocumentCreation
                              worldId: WebEngineScript.MainWorld
                              sourceUrl: Qt.resolvedUrl("../../resources/reader2/qwebchannel.js") },
            WebEngineScript { injectionPoint: WebEngineScript.DocumentCreation
                              worldId: WebEngineScript.MainWorld
                              sourceUrl: Qt.resolvedUrl("../../resources/reader2/bridge_boot.js") }
        ]
        onJavaScriptConsoleMessage: (lvl, msg) => console.log("[paper]", msg)
    }
}
```
Also create `resources/reader2/bridge_boot.js` (the shim, mirroring `qt_bridge_shim.js`):

```js
new QWebChannel(qt.webChannelTransport, ch => { window.bridge = ch.objects.bridge })
```
(Note: channel method returns arrive via callback — `paper_glue.js` already calls `bridge.filesRead(path, cb)` callback-style, which matches QWebChannel's JS API.)

- [ ] **Step 4: `ReaderShell.qml` (v1 = paper + temp keys) and `Harness.qml`:**

```qml
// qml/reader2/ReaderShell.qml — the component Biblio will embed on swap day.
import QtQuick

FocusScope {
    id: shell
    property string bookPath: ""
    property string bookId: bookPath        // stable id = path, same as old reader
    signal closed()
    focus: true
    Keys.onPressed: (e) => {
        if (e.key === Qt.Key_Right || e.key === Qt.Key_Space || e.key === Qt.Key_PageDown) { paper.next(); e.accepted = true }
        else if (e.key === Qt.Key_Left || e.key === Qt.Key_PageUp) { paper.prev(); e.accepted = true }
        else if (e.key === Qt.Key_Escape) { shell.closed(); e.accepted = true }
    }
    Paper { id: paper; anchors.fill: parent
        onGlueUpChanged: if (glueUp && shell.bookPath !== "") paper.open(shell.bookPath, "")
        onPaperEvent: (name, p) => console.log("[shell]", name, JSON.stringify(p).slice(0, 120))
    }
    function openBook(path) { bookPath = path; if (paper.glueUp) paper.open(path, "") }
}
```
`Harness.qml`: a `Window { width: 1280; height: 720 }` containing `HarnessShelf` (a `ListView` of book files found by a `FolderListModel` over the Step-1 folder, filters `["*.epub","*.mobi","*.azw3","*.fb2","*.txt","*.pdf"]`) that calls `shell.openBook(filePath)`; the shelf hides once a book opens, Escape returns to it.

- [ ] **Step 5: Build, run, EYES-ON GATE 1**

```bash
native/build-target.bat && ./reader2.bat
```
Expected stderr: `[reader2] stores: sandbox`, `[paper] [paper-event] glueLoaded`… then `ready` with a TOC after clicking a book, pages turn on arrows/Space. **Stop and show Hemanth: a book, rendering, turning.** Then commit (`feat(reader2): standalone harness exe — the paper stands`) + push.

---

### Task 6: The resume seam (positions survive)

**Files:**
- Modify: `qml/reader2/ReaderShell.qml`
- Create: `qml/reader2/Reader2Logic.js`, `tests/reader2_logic_harness.qml`

- [ ] **Step 1: Pure logic first** — `qml/reader2/Reader2Logic.js` (`.pragma library`) with `progressRecord(relocatedPayload)` → the exact `progress.json` value shape the OLD reader wrote (verify shape once: `cat "$APPDATA/../Local/Colosseum/progress.json" | head -20` — mirror its keys exactly; expect at least `{cfi, percent}` plus whatever else is present — copy every key) and `railState(relocated, tocLength)` → `{fillPct, label}` for Task 7.

- [ ] **Step 2: Headless test** `tests/reader2_logic_harness.qml` (house pattern — try/catch, `Qt.exit`):

```qml
import QtQml
import "../qml/reader2/Reader2Logic.js" as L
QtObject {
    Component.onCompleted: {
        try {
            var rec = L.progressRecord({ cfi: "epubcfi(/6/4!/2)", percent: 41,
                                         chapterTitle: "Loomings", fraction: 0.41 })
            if (rec.cfi !== "epubcfi(/6/4!/2)") throw "cfi lost"
            if (rec.percent !== 41) throw "percent lost"
            console.log("VERDICT: PASS"); Qt.exit(0)
        } catch (e) { console.log("VERDICT: FAIL — " + e); Qt.exit(1) }
    }
}
```
Run: `qml.exe -platform offscreen tests/reader2_logic_harness.qml` (qml.exe from the same Qt kit `build-target.bat` uses). Expect PASS, exit 0.

- [ ] **Step 3: Wire the seam in `ReaderShell.qml`:** on `paperEvent("relocated", p)` → `Reader2Bridge.progressSave(bookId, L.progressRecord(p))`; in `openBook` → `var saved = Reader2Bridge.progressGet(path); paper.open(path, saved.cfi || "")`.

- [ ] **Step 4: Verify eyes-on in the harness:** open a book, page forward five times, quit, relaunch — it opens where you left. Commit (`feat(reader2): resume seam — relocated events persist to the shared progress store`) + push.

---

### Task 7: Chrome skeleton — reveal, top bar, bottom rail

**Files:**
- Create: `qml/reader2/ReaderChrome.qml`, `qml/reader2/TopBar.qml`, `qml/reader2/BottomRail.qml`, `qml/reader2/Theme.qml` (singleton: the mock's constants)
- Modify: `qml/reader2/ReaderShell.qml`, `qml/reader2/Reader2Logic.js`, `tests/reader2_logic_harness.qml`

**The mock is the pixel contract.** `Theme.qml` singleton carries its constants: ink `#ffffff`, inkDim 62%, inkFaint 40%, inkGhost 26%, gold `#F0C24A`, bar `Qt.rgba(16/255,16/255,19/255,0.72)`, barBorder white 7%, Inter for UI, Fraunces for the title (register via the app's existing font loading; **"<Family> Variable" naming trap** applies if a variable TTF is used).

- [ ] **Step 1: Reveal logic pure + tested.** Add to `Reader2Logic.js`: `revealReducer(state, event)` — events `"move"`, `"tick"`, `"panelOpen"`, `"panelClose"`; chrome hides after 1800ms of no `"move"` unless a panel is open; keys NEVER reveal. Extend the headless harness: move→awake, 2×tick(1000)→hidden, panelOpen→pinned awake, panelClose+tick→hidden. Run offscreen, expect PASS.

- [ ] **Step 2: `TopBar.qml`** — icon-only per the amendment: back arrow (left); search / contents / appearance / bookmark icon buttons (right) as `signal`-emitting SVG `Image`/`Shape` buttons (assets under `assets/icons/reader2/`, 24px viewBox, stroke 1.7, drawn to match the mock's paths); centered `Text` title·author (Fraunces 16 + Inter 13 dim), chapter label right (Inter 13 dim). `BottomRail.qml` — 3px track white 13%, gold fill+knob, chapter tick `Repeater` positioned from `Reader2Logic.railTicks(toc, sections)`, drag-to-scrub emitting `scrubbed(fraction)`, meta row "Page N of M in chapter" / "X% of book" (Inter 12.5 faint, numbers dim), and the return ghost chip (visible after `scrubbed`, emits `returnRequested()`).

- [ ] **Step 3: `ReaderChrome.qml`** composes scrims + TopBar + BottomRail over the paper, driven by a `MouseArea` (`hoverEnabled: true`, `acceptedButtons: Qt.NoButton`) feeding `revealReducer`, plus edge click zones (11% width) calling `paper.prev()/next()` and a center click toggling chrome. Wire into `ReaderShell`: `relocated` updates the rail; `scrubbed(f)` → remember current cfi → `paper.goTo(f)`; `returnRequested` → `paper.goTo(rememberedCfi)`.

- [ ] **Step 4: EYES-ON GATE 2 — the feel test.** Rebuild, run the harness. Naked while reading; mouse wakes glass; idle sleeps it; keys turn pages without waking anything; scrub + return chip work. **This is the step that must feel like the video player — stop and get Hemanth's verdict before proceeding.** Commit (`feat(reader2): chrome skeleton — reveal, icon top bar, gold rail`) + push.

---

### Task 8: Left panel — Contents / Bookmarks / Highlights

**Files:**
- Create: `qml/reader2/LeftPanel.qml`
- Modify: `qml/reader2/ReaderShell.qml`, `qml/reader2/ReaderChrome.qml`

- [ ] **Step 1: Panel shell** — 348px glass column (bar color, 22px blur via `FastBlur`-free translucency: use the mock's flat rgba glass, matching AudiobookStrip's approach), slides in `translateX`, **click-swallower `MouseArea` on the body** (house doctrine), tab strip Contents/Bookmarks/Highlights/Audio (Audio tab placeholder emits nothing until Task 12 — render it disabled-dim with a "soon" tooltip so the strip layout is final from day one).
- [ ] **Step 2: Contents pane** — `ListView` over the `ready` event's TOC; current row gold (from `relocated.tocIndex`), rows before it ghost-dim; click → `paper.goTo(href)` + panel stays open.
- [ ] **Step 3: Bookmarks pane** — model from `Reader2Bridge.bookmarksGet(bookId)`; TopBar's bookmark icon saves `{id: Date.now().toString(), cfi, snippet, chapterTitle, page}` from the latest `relocated` + a paper text excerpt; row click → `goTo(cfi)`; row hover shows a ghost delete ×.
- [ ] **Step 4: Highlights pane** — model from `annotationsGet(bookId)`; rows: 3px color edge rule + Literata quote + indented note (mock layout); click → `goTo(cfi)`. (Creation arrives in Task 9 — pane renders existing store content now, which proves zero-migration visibly: highlights made in the OLD reader appear here when run `--real-stores`.)
- [ ] **Step 5: Eyes-on in harness** (all three panes, jumps work, Esc closes panel before closing book — Escape chain: panel → chrome → shelf). Commit (`feat(reader2): left panel — contents, bookmarks, highlights over shared stores`) + push.

---

### Task 9: The pen — selection menu, highlights, notes, dictionary, footnotes

**Files:**
- Create: `qml/reader2/SelectionMenu.qml`, `qml/reader2/DictCard.qml`, `qml/reader2/FootnoteCard.qml`
- Modify: `qml/reader2/ReaderShell.qml`, `resources/reader2/paper_glue.js`

- [ ] **Step 1:** `selection` event → `SelectionMenu.qml` pops at the reported rect (clamped inside the frame): color dots (gold/slate/moss from the mock), Note, Copy, Define. Copy uses the existing `ClipboardHelper` (grep `grep -rn "ClipboardHelper" qml/ native/ | head -3` for the exposed name and reuse it — it shipped with copy-magnet).
- [ ] **Step 2:** Color pick → `annotationsSave(bookId, {id, cfi, color, text, note:""})` → `paper.addHighlight(...)` → Highlights pane refreshes. Note → inline `TextArea` in the menu, saved into the same record. `highlightTapped` event → reopen the menu with a Delete action → `annotationsDelete` + `paper.removeHighlight(id)`.
- [ ] **Step 3:** Define → `Reader2Bridge.dictLookup(word)`; `dictResult` → `DictCard.qml` (glass card: word Fraunces, definitions Inter dim, "open in Wiktionary" ghost link via `Qt.openUrlExternally`). On open, also re-apply all stored highlights: after `ready`, loop `annotationsGet` → `paper.addHighlight` (glue side already idempotent by id).
- [ ] **Step 4:** Footnote: glue's link handler intercepts footnote links (foliate exposes footnote handling — per the Task-1 notes; upstream ships `footnotes.js`) → `footnote {html, rect}` event → `FootnoteCard.qml` renders the text (strip tags to plain text v1).
- [ ] **Step 5: Eyes-on:** select → highlight in three colors, note survives relaunch, define shows a card, footnote pops, highlights from the old reader visible under `--real-stores` (read-only look, don't write yet). Commit (`feat(reader2): selection pen — highlights, notes, native dictionary, footnotes`) + push.

---

### Task 10: Appearance panel (live-apply)

**Files:**
- Create: `qml/reader2/AppearancePanel.qml`
- Modify: `qml/reader2/ReaderShell.qml`, `qml/reader2/Reader2Logic.js`, `tests/reader2_logic_harness.qml`

- [ ] **Step 1:** `Reader2Logic.appearanceDefaults()` + `appearanceToPaper(settings)` mapping the settings record → the glue's `setAppearance` payload. Themes (from the mock): Paper `#e9e4d8/#3a362c`, Sepia `#e5d5b8/#4a3f2c`, Slate `#232830/#c6cdd8`, Night `#111013/#eee9deDB`. Headless-test the mapping (defaults roundtrip; theme key → bg/fg pair). PASS before UI.
- [ ] **Step 2:** `AppearancePanel.qml` right glass column per the mock: swatch row, typeface cards (Literata/Fraunces/Inter v1 — book fonts ship in `assets/fonts/`, add Literata TTF, MIT/OFL note in THIRD_PARTY_DATA.md), size stepper (12–26px), line-spacing slider (1.2–2.2), margin slider (24–160px), justify segment, ruler group (toggle + height + dim — controls only, overlay next task).
- [ ] **Step 3:** Every control → update settings object → `settingsSave` → `paper.setAppearance(L.appearanceToPaper(s))` — live, no Apply button. On shell start: `settingsGet` → apply after `ready`.
- [ ] **Step 4: Eyes-on** (theme flips live, font/size/margins live, settings survive relaunch). Commit (`feat(reader2): appearance panel — live-applied, shared settings store`) + push.

---

### Task 11: Search + reading ruler

**Files:**
- Create: `qml/reader2/SearchSheet.qml`, `qml/reader2/RulerOverlay.qml`
- Modify: `qml/reader2/ReaderShell.qml`

- [ ] **Step 1:** `SearchSheet.qml` — floating glass sheet under the top bar (mock geometry): `TextField`, result count, `ListView` of `searchResults.results` (chapter label ghost-caps + excerpt with the hit `<b>`-wrapped via `Text.StyledText`); Enter → `paper.search(q)`; row click → `goTo(cfi)` + sheet stays; Esc closes + `paper.clearSearch()`.
- [ ] **Step 2:** `RulerOverlay.qml` — pure QML over the paper, under the chrome: darkened top/bottom regions + tinted band (`Rectangle`s, no shaders), y-position draggable, bound to the appearance settings' ruler record. **The ruler leaves the web layer entirely** — no glue involvement.
- [ ] **Step 3: Eyes-on** (search 28 hits across chapters and jumps; ruler drags and dims and persists). Commit (`feat(reader2): search sheet + native ruler overlay`) + push.

---

### Task 12: Audio auto-attach (the amendment) — C++

**Files:**
- Modify: `native/engine/AudiobookDownloader.h`, `native/engine/AudiobookDownloader.cpp`
- Create: `tests/reader2_autoattach_harness.cpp`
- Modify: `native/CMakeLists.txt`, plus the QML call site found in Step 3

- [ ] **Step 1: Failing harness test:** construct `AudiobookDownloader` + `AudioPairingStore` (test mode), call the new `setPairing(store)` + simulate a completed download by invoking the completion path with `bookId` set (expose the internal finish handler or drive it via the smallest seam the class allows — mirror how `tests/book_torrent_*` harnesses drive their classes), assert `store->getPairing("bk1").value("pairKey") == "pk1"`. FAIL first (method absent).
- [ ] **Step 2: Implement:** add `void setPairing(AudioPairingStore* s) { m_pairing = s; }` and extend `downloadAudiobook(...)` (currently `native/engine/AudiobookDownloader.h:53`) with a trailing `const QString& bookId = QString()`; remember it per pairKey; in the code path that emits `finished(pairKey, dirPath)`, add:

```cpp
if (m_pairing && !bookIdFor(pairKey).isEmpty())
    m_pairing->savePairing(bookIdFor(pairKey), QVariantMap{
        {QStringLiteral("pairKey"), pairKey},
        {QStringLiteral("dirPath"), dirPath}});
```
(Idempotent-safe: `finished` re-emits for already-downloaded pairKeys — `savePairing` upsert handles it.)
- [ ] **Step 3: Thread `bookId` from the book page:** find the QML call site — `grep -rn "downloadAudiobook(" qml/ | head -5` — and pass the page's book id as the new trailing arg. Wire `setPairing` where the downloader is constructed (`grep -rn "AudiobookDownloader" native/*.cpp native/main*.cpp | head -5`; the pairing store is already constructed for BookBridge — reuse that instance).
- [ ] **Step 4:** Build; harness PASS; eyes-on in the full app: download an audiobook from a book page → `AudioPairingStore` gains the record (log it) with **no pairing UI anywhere**. Commit (`feat(engine): audiobook auto-attach on download completion — pairing UI concept retired`) + push.

---

### Task 13: Audio tab + read-along (increments 4–5, retargeted)

**Files:**
- Modify: `qml/reader2/LeftPanel.qml` (enable the Audio tab), `qml/reader2/ReaderShell.qml`, `qml/reader2/Reader2Logic.js`, `tests/reader2_logic_harness.qml`
- Reference (read first): `~/Desktop/Tankoban-Max/src/domains/books/reader/reader_audiobook_pairing.js` (the chapter-matching brain to port), `qml/AudiobookSession.qml`, `qml/AudiobookStrip.qml`

- [ ] **Step 1: Port the chapter-match brain** to `Reader2Logic.chapterFor(bookTocIndex, bookToc, audioChapters)` — read TB-Max's `reader_audiobook_pairing.js` and port its matching logic (title-normalized match first, ordinal fallback) as a pure function. Headless tests: exact-title match, "Chapter 3"↔"3. …" ordinal match, mismatched counts fall back proportionally. PASS first.
- [ ] **Step 2: Audio tab UI** per the mock: attached-audiobook card (from `AudioPairingStore.getPairing(bookId)` — expose the store to the harness root context alongside `Reader2Bridge` in `reader2_harness_main.cpp`, constructing both), "Follow my reading" switch, transport reusing `AudiobookSession` (grep its API: `grep -n "function \|signal \|property " qml/AudiobookSession.qml | head -20` and drive it exactly as `Main.qml` does today). No pairing controls. Unattached state: dim text "Download the audiobook from this book's page" — nothing else.
- [ ] **Step 3: The sync (increment 5):** in `ReaderShell`, on `relocated` while follow is ON and a session is loaded: `var ch = L.chapterFor(p.tocIndex, toc, session.chapters); if (ch !== session.currentChapter) session.goToChapter(ch)`. Debounce with a 400ms `Timer` restart so rapid page flips seek once.
- [ ] **Step 4: Eyes-on** with a real attached audiobook (`--real-stores`, read-only judgment): tab shows the book, follow ON, chapter jump in Contents seeks the audio. Commit (`feat(reader2): audio tab + read-along sync — increments 4-5 land native`) + push.

---

### Task 14: PDF + TXT parity

**Files:**
- Modify: `resources/reader2/paper_glue.js` (+ ported engine glue if needed)

- [ ] **Step 1:** Consult the Task-1 notes verdicts. For each of TXT and PDF: if the fork opens it, wire the same `open()` path (foliate's PDF rides pdf.js — confirm the worker asset path resolves under `file://`+WebEngine; if the notes said no, port our proven glue: `resources/book_reader/domains/books/reader/engine_txt.js` (296 lines) / `engine_pdf.js` (332 lines) behind the same `window.paper` command surface, as `paper_txt.js`/`paper_pdf.js` modules the glue dispatches to by extension).
- [ ] **Step 2:** Browser-bench both (`paper_debug.html`, one TXT, one PDF) — `ready`/`relocated` events flow, appearance no-ops gracefully on PDF (fixed layout: `setAppearance` applies theme background only).
- [ ] **Step 3:** Harness eyes-on: one MOBI, one FB2, one TXT, one PDF from the shelf. Commit (`feat(reader2): format parity — mobi/fb2/txt/pdf on the one paper`) + push.

---

### Task 15: Parity polish pass + cross-model review

- [ ] **Step 1:** Walk the spec's Section-3 parity table in the harness, old reader open beside it (`colosseum.exe`), row by row; fix every miss inline (small commits per fix).
- [ ] **Step 2:** Run the full headless suite + harnesses: `reader2_stores_harness`, `reader2_bridge_harness`, `reader2_autoattach_harness`, `qml.exe -platform offscreen tests/reader2_logic_harness.qml` — all PASS.
- [ ] **Step 3:** Package a Codex review (codex-review skill): diff range = first reader2 commit..HEAD, DoD = the spec's parity table + amendments. Fix what survives scrutiny (receiving-code-review discipline: verify before implementing).
- [ ] **Step 4: EYES-ON GATE 3 — Hemanth reads a real book in the harness for a session.** His verdict gates the swap. Commit fixes + push.

---

### Task 16: Swap day

**Files:**
- Modify: the Biblio open-book call site (found in Step 2), possibly `Main.qml` (**A1-dirty — surgical-blob commit discipline mandatory**)
- Delete: `qml/BookReader.qml`, `resources/book_reader/` (after archiving)
- Modify: `native/CMakeLists.txt`, `native/reader2/Reader2Bridge.cpp` (if BookBridge-only glue remains), `reader2_harness` stays

- [ ] **Step 1: Archive first (house pattern):** `git branch archive/foliate-reader-2026-07 HEAD && git push origin archive/foliate-reader-2026-07`.
- [ ] **Step 2: Find the pointer:** `grep -rn "BookReader" qml/ | grep -v reader2` — expected: the Biblio surface that instantiates `BookReader { }` (likely `BiblioBook.qml` or `Main.qml`). Replace with `reader2/ReaderShell`, mapping the old `open(path, book)`/`closed()` contract onto `openBook(path)`/`closed()`. If `Main.qml` must be touched: stage ONLY your hunks via the surgical-blob procedure (hash-object + update-index), never `git add Main.qml` whole.
- [ ] **Step 3:** Wire `Reader2Bridge` construction in the main app (same place BookBridge is constructed; both coexist — BookBridge still serves any remaining callers like the audiobook strip until a later cleanup).
- [ ] **Step 4:** Delete `qml/BookReader.qml` + `resources/book_reader/` from the app target/resources; grep-verify no dangling references: `grep -rn "book_reader\|BookReader" qml/ native/ --include='*.qml' --include='*.cpp' --include='*.h' --include='*.txt' | grep -v reader2 | grep -v build-msvc` → expect zero rows.
- [ ] **Step 5: Full-app acceptance, REAL stores:** build, boot `colosseum.exe`, open a book from Biblio — it resumes at the exact position the OLD reader left it; bookmarks/highlights present; read-along follows; Continue card still works. Boot-smoke clean (no QML errors in stderr).
- [ ] **Step 6: The one commit:** `feat(reader2): swap — Biblio opens the fresh reader; TB2 foliate app retired (archived: archive/foliate-reader-2026-07)` + push. Announce on the haven wire (`agents/chat.md`) — a lane-visible organ was replaced.

---

## Self-review (done at write time)

- **Spec coverage:** architecture→T1-5; anatomy §1→T7-11; split §2→T2-4; parity table §3→T6-14 (formats T1/T14, resume T6, nav/TOC/search/bookmarks/highlights/dict/footnotes/appearance/ruler T7-11, read-along T12-13); delivery §4→T5 (harness+sandbox), eyes-on gates 1-3, T15 review, T16 swap. Deliberately-gone: TTS never ported; pairing UI never built (T12 note); HTML chrome never vendored.
- **Placeholders:** none — every unknown is a bounded verify step with an exact command and a recorded-answer home (the Task-1 notes doc), never "TBD".
- **Type consistency:** protocol table (T2) matches glue (T2), `Paper.qml` (T5), and shell wiring (T6-13); store names match `BookStores` (T3) and `Reader2Bridge` (T4); `chapterFor`/`progressRecord`/`railTicks`/`revealReducer`/`appearanceToPaper` each defined before use.
