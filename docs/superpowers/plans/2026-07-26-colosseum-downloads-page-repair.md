# Colosseum Downloads Page Repair Implementation Plan

> **SUPERSEDED after Task 1 by Hemanth's minimal-scope ruling.** Do not execute
> Tasks 2–9. Continue from
> `docs/superpowers/plans/2026-07-26-colosseum-downloads-essential-fixes.md`.

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Turn Downloads into an honest, deletion-safe, keyboard-operable recovery surface for manga, comics, books, audiobooks, and video.

**Architecture:** `LocalDownloads` becomes the single capability-driven facade while each downloader remains the authority for its own mutations. Small QML primitives provide consistent actions, confirmation, context menus, and focus; `Main.qml` owns only the persisted page snapshot and final media routing. Active-job and settled-library revisions are separated so progress no longer rebuilds the vault.

**Tech Stack:** Qt 6.11, C++17, Qt Quick/QML, PowerShell contract tests, small C++ harnesses through CMake.

## Global Constraints

- Implement all three P0 and all fifteen P1 findings from `C:\Users\Suprabha\Desktop\Brotherhood\agents\audit-downloads-ux-2026-07-26.md`.
- Implement only Downloads-local P2 work; do not change shared Theme construction, shared backdrop capture, or shared ScrollGlide behavior.
- Theatre alone may expose Retry, Pause, Resume, and play-while-arriving.
- Non-Theatre failures must show their real reason and a route back to their owning world; never synthesize Retry.
- Completed audiobooks open through the paired book reader's Audio surface; do not restore a standalone audiobook player.
- A failed job is attention, not active work, and unknown length is not zero.
- A destructive action must name deletion, require confirmation, preserve the index when filesystem deletion fails, and report the result.
- Do not touch `qml/PlayerPage.qml` or `qml/MangaReader.qml`.
- Add no dependency and no new application route outside Downloads.
- `native/CMakeLists.txt` and `native/main.cpp` already contain unrelated work. Preserve it, announce the exact additive CMake/main seams in `agents/chat.md` before editing, stage those files by hunk, and verify the staged diff before committing.
- Before every commit, require `git diff --cached --name-only` to contain only the current task's files and inspect `git diff --cached`.
- Hemanth's eyes on the real app are the only closure gate.

---

## File map

### New files

- `native/engine/DownloadFileOps.h` — one header-only deletion primitive that keeps indexes intact on filesystem failure and returns bounded user-facing errors.
- `tests/download_file_ops_harness.cpp` — deterministic success, missing-path, and injected-failure coverage for the deletion primitive.
- `qml/downloads/DownloadsAction.qml` — minimum target, focus ring, tooltip, busy state, and keyboard activation for one action.
- `qml/downloads/DownloadsConfirmDialog.qml` — modal destructive confirmation with explicit safe/destructive choices.
- `qml/downloads/DownloadsContextMenu.qml` — capability-fed keyboard context menu.
- `tests/downloads_page_behavior_harness.qml` — fake facade and keyboard/action behavior harness.
- `tests/test_downloads_truth_contract.ps1` — source contract for capability vocabulary, first-class audiobooks, separated revisions, and absence of dead actions.
- `tests/test_downloads_behavior.ps1` — launches the behavior harness and requires its pass markers.
- `agents/downloads-page-eyes-on-checklist-2026-07-26.md` — real-app closure ledger for the audit findings.

### Modified files

- `native/engine/MangaDownloader.h/.cpp` — truthful delete result; cancellation emits removal rather than a fake failure.
- `native/engine/BookDownloader.h/.cpp` — truthful delete result; cancellation emits removal.
- `native/engine/ComicDownloader.h/.cpp` — truthful delete result; cancellation emits removal.
- `native/engine/MangaTankobanService.h/.cpp` — truthful volume removal result.
- `native/engine/AudiobookDownloader.h/.cpp` — retained failure metadata, persisted book route, truthful delete result, dismiss operation.
- `native/player/downloadstore.h/.cpp` — truthful video deletion result.
- `native/engine/LocalDownloads.h/.cpp` — unified schemas, audiobook composition, capability flags, split revisions, totals, progress aggregation, and mutation lifecycle.
- `native/CMakeLists.txt` — add only the deletion harness target and source.
- `native/main.cpp` — pass the existing `audiobooks` pointer into `LocalDownloads`.
- `qml/DownloadsPage.qml` — unified manager/lanes, confirmation, focus, state snapshot, virtualization, and local polish.
- `qml/Main.qml` — save/restore Downloads state and route completed audiobooks into the paired book reader.
- `qml/reader2/ReaderShell.qml` — one additive `openAudioPanel()` entry point used after Downloads opens the paired book.
- `tests/downloads_page_load_harness.qml` — inject safe fake APIs needed by the expanded page.
- `tests/test_downloads_manager_p0.ps1` — replace obsolete string assertions with capability and confirmation assertions.
- `tests/test_downloads_play_arriving_p0.ps1` — retain Theatre play-while-arriving coverage after schema normalization.

---

### Task 1: Make filesystem deletion truthful

**Files:**

- Create: `native/engine/DownloadFileOps.h`
- Create: `tests/download_file_ops_harness.cpp`
- Modify: `native/CMakeLists.txt`
- Modify: `native/engine/MangaDownloader.h`
- Modify: `native/engine/MangaDownloader.cpp`
- Modify: `native/engine/BookDownloader.h`
- Modify: `native/engine/BookDownloader.cpp`
- Modify: `native/engine/ComicDownloader.h`
- Modify: `native/engine/ComicDownloader.cpp`
- Modify: `native/engine/MangaTankobanService.h`
- Modify: `native/engine/MangaTankobanService.cpp`
- Modify: `native/engine/AudiobookDownloader.h`
- Modify: `native/engine/AudiobookDownloader.cpp`
- Modify: `native/player/downloadstore.h`
- Modify: `native/player/downloadstore.cpp`

**Interfaces:**

- Produces: `DownloadFileOps::Result { bool success; QString message; }`.
- Produces: `DownloadFileOps::removeFile(path, remover)` and `removeTree(path, remover)`.
- Produces: deletion invokables returning `QVariantMap{success,message}`.
- Produces: cancellation signals `removed(id)` for user cancellation; `failed(id, reason)` remains reserved for genuine failures.

- [ ] **Step 1: Write the failing deletion harness**

Create `tests/download_file_ops_harness.cpp` with these assertions:

```cpp
#include "engine/DownloadFileOps.h"
#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>

static void require(bool value, const char *message)
{
    if (!value) qFatal("%s", message);
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir temp;
    require(temp.isValid(), "temporary directory");

    const QString filePath = temp.filePath(QStringLiteral("landed.epub"));
    QFile file(filePath);
    require(file.open(QIODevice::WriteOnly), "fixture open");
    file.write("payload");
    file.close();

    auto denied = DownloadFileOps::removeFile(
        filePath, [](const QString &) { return false; });
    require(!denied.success, "injected failure must fail");
    require(QFile::exists(filePath), "failed deletion must preserve payload");
    require(!denied.message.isEmpty(), "failure must carry bounded copy");

    auto removed = DownloadFileOps::removeFile(filePath);
    require(removed.success, "real deletion must succeed");
    require(!QFile::exists(filePath), "successful deletion removes payload");

    auto alreadyMissing = DownloadFileOps::removeFile(filePath);
    require(alreadyMissing.success, "missing payload is already deleted");
    return 0;
}
```

- [ ] **Step 2: Add the harness target and prove it fails**

Add this exact additive block to `native/CMakeLists.txt` after the other small Qt Core harnesses:

```cmake
add_executable(download_file_ops_harness
    ../tests/download_file_ops_harness.cpp
)
target_include_directories(download_file_ops_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(download_file_ops_harness PRIVATE Qt6::Core)
```

Run:

```powershell
cmake --build native/build-msvc --target download_file_ops_harness --config Release
```

Expected: compile failure because `engine/DownloadFileOps.h` does not exist.

- [ ] **Step 3: Implement the deletion primitive**

Create header-only `native/engine/DownloadFileOps.h`:

```cpp
#pragma once

#include <QDir>
#include <QFile>
#include <QString>
#include <functional>

namespace DownloadFileOps {

struct Result {
    bool success = false;
    QString message;
};

using Remover = std::function<bool(const QString &)>;

inline Result removeFile(
    const QString &path,
    const Remover &remove = [](const QString &p) { return QFile::remove(p); })
{
    if (path.isEmpty() || !QFile::exists(path))
        return {true, QString()};
    if (!remove(path) || QFile::exists(path))
        return {false, QStringLiteral("Colosseum could not delete the local file.")};
    return {true, QString()};
}

inline Result removeTree(
    const QString &path,
    const Remover &remove = [](const QString &p) {
        return QDir(p).removeRecursively();
    })
{
    if (path.isEmpty() || !QDir(path).exists())
        return {true, QString()};
    if (!remove(path) || QDir(path).exists())
        return {false, QStringLiteral("Colosseum could not delete the local folder.")};
    return {true, QString()};
}

inline QVariantMap toMap(const Result &result)
{
    return {{QStringLiteral("success"), result.success},
            {QStringLiteral("message"), result.message}};
}

} // namespace DownloadFileOps
```

Add `#include <QVariantMap>` to the header.

- [ ] **Step 4: Route every completed-media deletion through the primitive**

Change the six deletion invokables to return `QVariantMap`. Use this exact control flow in each backend:

```cpp
const auto result = DownloadFileOps::removeFile(entry.path); // removeTree for directories
if (!result.success) {
    qWarning() << "[downloads] delete failed" << id << result.message;
    return DownloadFileOps::toMap(result);
}
m_index.erase(it);
saveIndex();
emit removed(id);
return DownloadFileOps::toMap(result);
```

For `MangaTankobanService::remove`, call `m_index->remove(volumeId)` and return
`{success:false,message:"Colosseum could not delete the local volume."}` when
the index still reports `ready`; emit `removed` only after the ready record is
gone.

For a user cancellation in Manga, Book, Comic, and Audiobook code, clean the
partial payload and emit `removed(id)`. Do not emit `failed(id,
"cancelled by user")`.

- [ ] **Step 5: Run deletion verification**

Run:

```powershell
cmake --build native/build-msvc --target download_file_ops_harness colosseum --config Release
native\build-msvc\Release\download_file_ops_harness.exe
```

Expected: both commands exit `0`; the harness prints no fatal message.

- [ ] **Step 6: Commit the deletion slice**

Announce the exact CMake block in Brotherhood `agents/chat.md`. Stage
`native/CMakeLists.txt` by hunk and the remaining listed files by explicit
path. Verify the staged diff contains no pre-existing CMake changes, then
commit:

```powershell
git commit -m "fix: preserve downloads when deletion fails"
```

---

### Task 2: Retain honest audiobook state and reader routing metadata

**Files:**

- Modify: `native/engine/AudiobookDownloader.h`
- Modify: `native/engine/AudiobookDownloader.cpp`
- Modify: `qml/BiblioBook.qml`
- Create: `tests/test_downloads_truth_contract.ps1`

**Interfaces:**

- Produces: `activeDownloads()` rows with `title`, `author`, `state`, `error`, `received`, and `total`.
- Produces: `downloadedAudiobooks()` rows with persisted `bookId` and `bookPath`.
- Produces: `Q_INVOKABLE bool dismissFailure(const QString &pairKey)`.
- Produces: `failuresChanged()` and retained in-memory failure records.

- [ ] **Step 1: Write the failing truth contract**

Create `tests/test_downloads_truth_contract.ps1` and require these exact seams:

```powershell
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$audioH = Get-Content (Join-Path $root "native/engine/AudiobookDownloader.h") -Raw
$audioC = Get-Content (Join-Path $root "native/engine/AudiobookDownloader.cpp") -Raw

foreach ($needle in @("dismissFailure", "failuresChanged", "bookPath", "bookId",
                      'QStringLiteral("error")')) {
    if ($audioH -notlike "*$needle*" -and $audioC -notlike "*$needle*") {
        throw "missing audiobook truth seam: $needle"
    }
}
if ($audioC -match 'cancelled by user') {
    throw "user cancellation must not survive as a failed audiobook row"
}
Write-Host "downloads truth contract: OK"
```

- [ ] **Step 2: Run the contract and prove it fails**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File tests/test_downloads_truth_contract.ps1
```

Expected: failure naming `dismissFailure`.

- [ ] **Step 3: Add retained failure and persisted route fields**

Extend `AudiobookDownloader::Entry` with `bookId` and `bookPath`. Save and load
both keys in `index.json`. Before deleting a failed `Job`, store:

```cpp
m_failures.insert(pk, QVariantMap{
    {QStringLiteral("id"), pk},
    {QStringLiteral("title"), job->title},
    {QStringLiteral("author"), job->author},
    {QStringLiteral("state"), QStringLiteral("failed")},
    {QStringLiteral("error"), reason},
    {QStringLiteral("received"),
        static_cast<double>(job->doneBytes + job->fileReceived)},
    {QStringLiteral("total"), static_cast<double>(job->totalBytes)}
});
emit failuresChanged();
```

Append retained failures to `activeDownloads()`. Clear the matching failure on
a new download, completion, successful cancellation, deletion, or
`dismissFailure(pairKey)`.

At finalization, copy `m_bookIdFor[pairKey]` and `m_bookPathFor[pairKey]` into
the persisted `Entry`. Return those fields from `downloadedAudiobooks()`.

Move `setPairing(AudioPairingStore*)` out of the header and make it reconcile
legacy entries against `AudioPairingStore::allPairings()`: when a pairing's
`pairKey` matches an entry, copy its `bookId` and existing `bookPath`, then save
the audiobook index. In `qml/BiblioBook.qml`, pass `detail.localPath` as the
sixth `downloadAudiobook` argument:

```qml
Audiobooks.downloadAudiobook(
    detail.pairKey, d.infoHash, detail.book.title || "",
    detail.book.author || "", bookId, detail.localPath || "")
```

This makes every future completion routable and backfills legacy entries when
their pairing already contains a book path. Legacy entries without a recoverable
book path remain visibly unavailable instead of opening a dead reader.

- [ ] **Step 4: Run the audiobook contract and engine probe**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File tests/test_downloads_truth_contract.ps1
cmake --build native/build-msvc --target audiobook_engine_probe colosseum --config Release
```

Expected: contract prints `downloads truth contract: OK`; both targets build.

- [ ] **Step 5: Commit the audiobook truth slice**

Stage only the two audiobook files and the contract, inspect the staged diff,
then commit:

```powershell
git commit -m "fix: retain truthful audiobook download state"
```

---

### Task 3: Build the capability-driven LocalDownloads facade

**Files:**

- Modify: `native/engine/LocalDownloads.h`
- Modify: `native/engine/LocalDownloads.cpp`
- Modify: `native/engine/MangaDownloader.h`
- Modify: `native/engine/MangaDownloader.cpp`
- Modify: `native/engine/BookDownloader.h`
- Modify: `native/engine/BookDownloader.cpp`
- Modify: `native/engine/ComicDownloader.h`
- Modify: `native/engine/ComicDownloader.cpp`
- Modify: `native/engine/MangaTankobanService.h`
- Modify: `native/engine/MangaTankobanService.cpp`
- Modify: `native/engine/AudiobookDownloader.h`
- Modify: `native/engine/AudiobookDownloader.cpp`
- Modify: `native/player/downloadstore.h`
- Modify: `native/player/downloadstore.cpp`
- Modify: `native/main.cpp`
- Modify: `tests/test_downloads_truth_contract.ps1`
- Modify: `tests/test_downloads_manager_p0.ps1`
- Modify: `tests/test_downloads_play_arriving_p0.ps1`

**Interfaces:**

- Consumes: truthful backend deletion maps and audiobook rows from Tasks 1–2.
- Produces: `activeRevision`, `libraryRevision`, and `summaryRevision`.
- Produces: `activeJobs()`, `series(world)`, `items(world,key)`, and `totals()` with world `audiobook`.
- Produces: `laneStatus(world)` with `available`, `degraded`, and bounded `error` fields.
- Produces: `requestCancel`, `requestDelete`, `requestRetry`, and `requestDismiss`.
- Produces: `operationFinished(requestId, operation, world, id, success, message)`.
- Produces: every row's `canPlay`, `canRetry`, `canPause`, `canResume`, `canCancel`, `canDismiss`, `canOpen`, and `canDelete`.

- [ ] **Step 1: Expand the failing contract**

Add assertions to `tests/test_downloads_truth_contract.ps1` for:

```powershell
$localH = Get-Content (Join-Path $root "native/engine/LocalDownloads.h") -Raw
$localC = Get-Content (Join-Path $root "native/engine/LocalDownloads.cpp") -Raw
foreach ($needle in @("activeRevision", "libraryRevision", "summaryRevision",
                      "requestCancel", "requestDelete", "requestRetry",
                      "requestDismiss", "operationFinished", "AudiobookDownloader",
                      'QStringLiteral("audiobook")', 'QStringLiteral("canRetry")',
                      'QStringLiteral("canDelete")', 'QStringLiteral("attention")',
                      "laneStatus", "storageStatus")) {
    if ($localH -notlike "*$needle*" -and $localC -notlike "*$needle*") {
        throw "missing LocalDownloads truth seam: $needle"
    }
}
if ($localC -match 'canRetry.+world.+theatre' -and
    $localC -match 'canRetry.+tankoban') {
    throw "Tankoban must not advertise blind retry"
}
```

Run the contract. Expected: failure naming `activeRevision`.

- [ ] **Step 2: Replace the single revision with three channels**

Add:

```cpp
Q_PROPERTY(int activeRevision READ activeRevision NOTIFY activeChanged)
Q_PROPERTY(int libraryRevision READ libraryRevision NOTIFY libraryChanged)
Q_PROPERTY(int summaryRevision READ summaryRevision NOTIFY summaryChanged)
```

Use `bumpActive()`, `bumpLibrary()`, and `bumpSummary()`; active progress must
not call `bumpLibrary()`. Keep a compatibility `changed()` signal until
`Main.qml` taskbar reveal is migrated to `activeChanged()`.

- [ ] **Step 3: Compose audiobooks and normalize rows**

Add `AudiobookDownloader *audiobooks` before the parent parameter in the
constructor and store `m_audiobooks`. Pass the existing `audiobooks` pointer
from `native/main.cpp`.

For Theatre, set capability flags from actual state and URL:

```cpp
const bool failed = state == QStringLiteral("failed");
const bool paused = state == QStringLiteral("paused");
j.insert(QStringLiteral("canPlay"), !url.isEmpty() && !failed);
j.insert(QStringLiteral("canRetry"), failed);
j.insert(QStringLiteral("canPause"), state == QStringLiteral("downloading"));
j.insert(QStringLiteral("canResume"), paused);
j.insert(QStringLiteral("canCancel"), state != QStringLiteral("done"));
```

For non-Theatre rows, set `canRetry`, `canPause`, and `canResume` to `false`.
Preserve each backend's failure reason as `error`. Append audiobooks using
world `audiobook`, owner `audiobook`, kind `audiobook`, and their title/author.

For completed audiobook rows, set:

```cpp
canOpen = !bookPath.isEmpty() && QFileInfo::exists(bookPath);
canDelete = true;
unavailableReason = canOpen ? QString()
    : QStringLiteral("The paired book is not available locally.");
```

- [ ] **Step 4: Retain non-audiobook failure rows**

Keep `m_lastActiveRows` keyed by `owner + ":" + id` whenever `activeJobs()`
normalizes a backend job. Connect Manga, Book, Comic, and volume `failed`
signals directly to `rememberFailure(owner, id, reason)`. The retained map uses
the last title/subtitle/creator identity, state `failed`, `canDismiss:true`, all
transport capabilities false, and the bounded real reason. A later progress or
finished signal for that identity clears the failure. `requestDismiss` removes
the retained row and bumps active/summary revisions.

- [ ] **Step 5: Distinguish storage failure from a genuinely empty lane**

Add `Q_INVOKABLE QVariantMap storageStatus() const` to Manga, Book, Comic,
Audiobook, DownloadStore, and MangaTankobanService. Their index loaders set:

```cpp
{available:true, error:""}                 // missing index means a valid empty store
{available:false, error:"The local download index could not be read."}
```

for open/parse failures. Do not expose an absolute path. For Tankoban,
`laneStatus("tankoban")` returns `degraded:true` when at least one of Manga,
Comic, or Volume storage failed but another remains readable; it returns
`available:false` only when every owner is unavailable. Biblio, Theatre, and
Audiobook reflect their single owner's status. Add an unavailable fake lane to
the QML behavior harness in Task 5 and require that it renders the bounded
error instead of the empty-lane CTA.

- [ ] **Step 6: Implement truthful totals and weighted groups**

Totals must include `audiobook`, exclude `failed` from `active`, and include
failed/unavailable rows in `attention`.

Add a pure helper:

```cpp
QVariantMap LocalDownloads::aggregateProgress(const QVariantList &rows)
{
    double received = 0.0;
    double total = 0.0;
    int known = 0;
    for (const QVariant &value : rows) {
        const QVariantMap row = value.toMap();
        if (!row.value(QStringLiteral("hasKnownTotal")).toBool())
            continue;
        received += row.value(QStringLiteral("received")).toDouble();
        total += row.value(QStringLiteral("total")).toDouble();
        ++known;
    }
    return {{QStringLiteral("hasKnownTotal"), known > 0 && total > 0.0},
            {QStringLiteral("received"), received},
            {QStringLiteral("total"), total},
            {QStringLiteral("ratio"), total > 0.0 ? received / total : 0.0}};
}
```

Expose it as `Q_INVOKABLE` for the QML harness. Page/count jobs remain labeled
in pages and are not folded into byte totals.

- [ ] **Step 7: Implement mutation lifecycle**

Each request returns a monotonically unique string such as
`delete:theatre:tt123:42`. Store pending metadata by request id.

- `requestRetry` rejects non-Theatre immediately through
  `operationFinished(..., false, "Retry is not available for this download.")`.
- `requestDelete` calls the owning backend's truthful deletion result and emits
  the exact result.
- `requestCancel` accepts only rows with `canCancel`; complete on the owning
  backend's `removed` signal.
- `requestDismiss` supports retained failed audiobooks and Theatre failed jobs.
- A five-second `QTimer::singleShot` fails any request still pending with
  `The download operation did not finish.`

Sanitize user-facing messages by removing control characters and limiting to
240 characters; retain full backend text in `qWarning`.

- [ ] **Step 8: Update legacy contracts**

Change `tests/test_downloads_manager_p0.ps1` to require request methods,
capability flags, confirmation vocabulary, and exact-row ids. Remove assertions
that force the old direct `LocalDownloads.cancel(...)` calls.

Keep `tests/test_downloads_play_arriving_p0.ps1` asserting URL/art pass-through
and `playArrivingRequested`, but update map formatting expectations to the new
normalization helper.

- [ ] **Step 9: Run facade verification**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File tests/test_downloads_truth_contract.ps1
powershell -ExecutionPolicy Bypass -File tests/test_downloads_manager_p0.ps1
powershell -ExecutionPolicy Bypass -File tests/test_downloads_play_arriving_p0.ps1
cmake --build native/build-msvc --target colosseum --config Release
```

Expected: all three scripts print their success line; build exits `0`.

- [ ] **Step 10: Commit the facade slice**

Stage LocalDownloads and tests by path. Stage only the constructor hunk from
`native/main.cpp`, confirm no Player 2 or unrelated main changes are staged,
then commit:

```powershell
git commit -m "feat: unify truthful download capabilities"
```

---

### Task 4: Add reusable Downloads interaction primitives

**Files:**

- Create: `qml/downloads/DownloadsAction.qml`
- Create: `qml/downloads/DownloadsConfirmDialog.qml`
- Create: `qml/downloads/DownloadsContextMenu.qml`
- Create: `tests/downloads_page_behavior_harness.qml`
- Create: `tests/test_downloads_behavior.ps1`

**Interfaces:**

- Produces: `DownloadsAction { text, iconSource, busy, destructive, triggered() }`.
- Produces: `DownloadsConfirmDialog.confirm(title, body, destructiveLabel, callback)`.
- Produces: `DownloadsContextMenu.openFor(anchorItem, actions)`.
- Produces: harness markers `ACTION PASS`, `CONFIRM PASS`, and `KEYBOARD PASS`.

- [ ] **Step 1: Write the failing behavior runner and harness shell**

`tests/test_downloads_behavior.ps1` launches the Qt QML executable exactly as
the load test does and requires all three markers:

```powershell
$ErrorActionPreference = "Stop"
$qml = "C:\Qt\6.11.1\msvc2022_64\bin\qml.exe"
$harness = Join-Path $PSScriptRoot "downloads_page_behavior_harness.qml"
$env:QT_FORCE_STDERR_LOGGING = "1"
$out = cmd /c "`"$qml`" `"$harness`" 2>&1" | Out-String
foreach ($marker in @("ACTION PASS", "CONFIRM PASS", "KEYBOARD PASS")) {
    if ($out -notlike "*$marker*") { throw "missing $marker`n$out" }
}
Write-Host "downloads behavior: OK"
```

The initial harness imports `../qml/downloads` and instantiates
`DownloadsAction`, `DownloadsConfirmDialog`, and `DownloadsContextMenu`.

Run the script. Expected: component-not-found failure.

- [ ] **Step 2: Implement DownloadsAction**

Use `QtQuick.Controls.Button`, not a raw `MouseArea`. Enforce:

```qml
implicitHeight: 44
implicitWidth: Math.max(44, contentItem.implicitWidth + 24)
enabled: !busy && actionEnabled
Accessible.name: text
Keys.onReturnPressed: clicked()
Keys.onEnterPressed: clicked()
Keys.onSpacePressed: clicked()
```

Render the existing Lucide SVG in a 17×17 `Image`, show `Working…` while busy,
and draw a two-pixel accent focus ring when `activeFocus`.

- [ ] **Step 3: Implement confirmation and context menu**

`DownloadsConfirmDialog` is modal, closes on Escape, traps focus, and exposes
only `Cancel` plus the passed destructive label. Clicking the dim backdrop does
not confirm or close it.

`DownloadsContextMenu` accepts action maps:

```qml
{ key: "delete", label: "Delete local copy", icon: "../icons/trash-2.svg",
  enabled: true, destructive: true, invoke: function() {} }
```

It omits disabled/inapplicable entries, focuses the first action, supports
Up/Down, Enter/Space, and Escape, and restores focus to its anchor on close.

- [ ] **Step 4: Complete and run the primitive harness**

The harness must:

- focus an action and send Return, then print `ACTION PASS`;
- open the dialog, prove Cancel does not invoke deletion, invoke the destructive
  button once, then print `CONFIRM PASS`;
- open the context menu, move with Down, activate with Space, close with Escape,
  then print `KEYBOARD PASS`.

Run:

```powershell
powershell -ExecutionPolicy Bypass -File tests/test_downloads_behavior.ps1
```

Expected: `downloads behavior: OK`.

- [ ] **Step 5: Commit the primitive slice**

Stage only the three components and two harness files, inspect, then commit:

```powershell
git commit -m "feat: add safe Downloads interaction controls"
```

---

### Task 5: Rebuild the manager and five settled lanes around capabilities

**Files:**

- Modify: `qml/DownloadsPage.qml`
- Modify: `tests/downloads_page_load_harness.qml`
- Modify: `tests/downloads_page_behavior_harness.qml`
- Modify: `tests/test_downloads_truth_contract.ps1`

**Interfaces:**

- Consumes: LocalDownloads schemas and request methods from Task 3.
- Consumes: interaction primitives from Task 4.
- Produces: `property var downloadsApi`, `captureState()`, and `restoreState(snapshot)`.
- Produces: signal `openAudiobookRequested(var item)`.

- [ ] **Step 1: Add failing page-level contract assertions**

Require:

```powershell
$page = Get-Content (Join-Path $root "qml/DownloadsPage.qml") -Raw
foreach ($needle in @("downloadsApi", "requestDelete", "requestCancel",
                      "requestRetry", "requestDismiss", "Delete local copy",
                      "openAudiobookRequested", "needs attention",
                      "hasKnownTotal", "unavailableReason")) {
    if ($page -notlike "*$needle*") { throw "missing page seam: $needle" }
}
if ($page -match 'LocalDownloads\.retry\(') {
    throw "page must not bypass capability-driven retry"
}
if ($page -match 'text:\s*\"Remove\"') {
    throw "destructive Remove wording is forbidden"
}
```

Run the truth contract. Expected: failure naming `downloadsApi`.

- [ ] **Step 2: Add injectable API and split refreshes**

At the page root:

```qml
property var downloadsApi:
    (typeof LocalDownloads !== "undefined") ? LocalDownloads : null
property int activeRev: downloadsApi ? downloadsApi.activeRevision : 0
property int libraryRev: downloadsApi ? downloadsApi.libraryRevision : 0
property int summaryRev: downloadsApi ? downloadsApi.summaryRevision : 0
```

Implement `refreshActive()` to rebuild only active jobs/groups and
`refreshLibrary()` to rebuild series/ledger/totals. The 400 ms timer may call
`refreshActive()` only while non-failed jobs exist.

- [ ] **Step 3: Render the manager from capability maps**

Build each inline action through one `actionsForJob(job)` function. It returns
Play, Retry, Pause, Resume, Cancel, Dismiss, or Return to World only when the
matching capability/state requires it. Group actions are the intersection or
eligible subset of child capabilities, never assumptions from group state.

Use `downloadsApi.aggregateProgress(rows)` for group bytes. When
`hasKnownTotal` is false, show an indeterminate strip and no percent.

Summary copy uses:

```qml
active + " arriving · " + attention + " need attention · "
    + items + " local items · " + fmtBytes(bytes)
```

Omit zero-valued clauses rather than promising failed jobs will land.

- [ ] **Step 4: Render unavailable and degraded lanes honestly**

Read `downloadsApi.laneStatus(world)` before choosing the lane's empty state.
When `available:false`, render its bounded error and no browse CTA. When
`degraded:true`, keep readable items visible and show the warning above them.
Only `{available:true,degraded:false}` with zero items may render the genuine
empty-lane CTA.

- [ ] **Step 5: Replace the detached audiobook appendix with a normal lane**

Add world descriptor:

```qml
{ key: "audiobook", title: "AUDIOBOOKS", empty: "Find an audiobook in Biblio" }
```

Audiobook cards show title, author, size, and unavailable state. `Open book`
emits `openAudiobookRequested(item)` only when `canOpen`. The empty CTA emits
`openWorldRequested("audiobook")`. Deletion uses the shared confirmation with
`Delete local copy`.

Remove `abActive`, `abDone`, `abRefresh`, `abTitleOf`, and direct
`Audiobooks.cancelDownload/deleteAudiobook` calls.

- [ ] **Step 6: Wire confirmation and mutation feedback**

All destructive methods call:

```qml
confirmDialog.confirm(
    row.title,
    "Delete the local files for “" + row.title + "”? Reading and watch progress stays.",
    "Delete local copy",
    function() { startRequest("delete", row,
        downloadsApi.requestDelete(row.world, row.id)) })
```

Group cancellation names the eligible count and says partial files may be
discarded. Track request ids in `pendingRequests`; disable duplicate actions.
Consume `operationFinished`, clear pending, refresh the correct model, and show
the bounded success/failure announcement.

- [ ] **Step 7: Update load and behavior harnesses**

Give each harness a fake `downloadsApi` with all revision properties, empty
lists, totals, request methods, and `aggregateProgress`. The behavior harness
must inject:

- a failed Theatre row with `canRetry:true`;
- a failed manga row with `canRetry:false` and an error;
- a completed audiobook with `canOpen:true`;
- an unknown-total active row.

Assert Theatre has Retry, manga does not, the audiobook emits open, and
unknown-total copy contains no percent. Print the existing pass markers only
after these assertions.

- [ ] **Step 8: Run page verification**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File tests/test_downloads_truth_contract.ps1
powershell -ExecutionPolicy Bypass -File tests/test_downloads_behavior.ps1
powershell -ExecutionPolicy Bypass -File tests/test_downloads_page_loads.ps1
```

Expected: all scripts exit `0`.

- [ ] **Step 9: Commit the page truth slice**

Stage only the page and harness changes, inspect, then commit:

```powershell
git commit -m "feat: make all Downloads lanes truthful"
```

---

### Task 6: Add complete keyboard, remote, context-menu, and snapshot behavior

**Files:**

- Modify: `qml/DownloadsPage.qml`
- Modify: `tests/downloads_page_behavior_harness.qml`
- Modify: `tests/test_downloads_truth_contract.ps1`

**Interfaces:**

- Produces: semantic focus keys `chrome:<name>`, `job:<world>:<id>`,
  `lane:<world>:<seriesKey>`, and `item:<world>:<id>`.
- Produces: `captureState()` map and `restoreState(snapshot)`.

- [ ] **Step 1: Add failing focus assertions**

The contract must require `FocusScope`, `activeFocus`, `Accessible.name`,
`Keys.onEscapePressed`, `captureState`, `restoreState`, and
`DownloadsContextMenu`. It must reject page action implementations consisting
only of a raw `MouseArea`.

Run the contract. Expected: failure naming `FocusScope`.

- [ ] **Step 2: Implement semantic focus topology**

Make the root a `FocusScope`. Assign every delegate a semantic key and
register/unregister it in `focusTargets`. Implement:

```qml
function focusSemantic(key) {
    var target = focusTargets[key]
    if (target && target.visible && target.enabled) {
        target.forceActiveFocus()
        lastFocusKey = key
        return true
    }
    return false
}
```

Tab order follows chrome → manager → lanes → ledger. Left/Right remains inside
an action cluster or shelf; Up/Down chooses the nearest actionable delegate in
the adjacent row. Enter/Space activates; the context-menu key opens row
actions. Escape closes menu, closes confirmation, collapses the ledger, then
emits `backRequested`.

- [ ] **Step 3: Add accessibility and tooltips**

Use `DownloadsAction` for every verb and chrome control. Give icon actions
names such as `Back`, `Search`, `Minimize`, `Restore`, and `Close`. Every target
has a 44 px effective hit region and visible focus ring.

- [ ] **Step 4: Implement snapshot capture/restore**

Return:

```qml
{
    contentY: mainFlick.contentY,
    openGroups: root.openGroups,
    openLedgerWorld: root.openLedgerWorld,
    openLedgerKey: root.openLedgerKey,
    openSeasons: root.openSeasons,
    focusKey: root.lastFocusKey
}
```

Restore maps defensively, clamp `contentY` after layout, and focus the saved
semantic key. If missing, focus the ledger's nearest surviving item, then the
lane header, then Back.

- [ ] **Step 5: Extend keyboard harness**

Drive only keys to:

- focus Back;
- move into a job action;
- open its context menu;
- choose Cancel;
- reject the confirmation once;
- confirm once;
- close and restore a snapshot to the same semantic key.

Print `KEYBOARD PASS` only after the fake facade records exactly one cancel
request.

- [ ] **Step 6: Run and commit**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File tests/test_downloads_behavior.ps1
powershell -ExecutionPolicy Bypass -File tests/test_downloads_page_loads.ps1
powershell -ExecutionPolicy Bypass -File tests/test_downloads_truth_contract.ps1
```

Expected: all exit `0`.

Commit:

```powershell
git commit -m "feat: make Downloads keyboard and remote complete"
```

---

### Task 7: Preserve state in Main and route completed audiobooks

**Files:**

- Modify: `qml/Main.qml`
- Modify: `qml/DownloadsPage.qml`
- Modify: `qml/reader2/ReaderShell.qml`
- Modify: `tests/test_downloads_truth_contract.ps1`
- Modify: `tests/test_downloads_play_arriving_p0.ps1`

**Interfaces:**

- Consumes: `captureState`, `restoreState`, and `openAudiobookRequested`.
- Produces: `downloadsLayer.savedState`.
- Produces: `routeDownloadedAudiobook(item)`.
- Produces: `ReaderShell.openAudioPanel()`.

- [ ] **Step 1: Add failing routing/state assertions**

Require `savedState`, `captureState()`, `restoreState(savedState)`,
`openAudiobookRequested.connect`, and `routeDownloadedAudiobook`. Retain the
existing Theatre, book, comic, and manga route assertions.

Run the contracts. Expected: failure naming `savedState`.

- [ ] **Step 2: Capture before unloading and restore after load**

Change:

```qml
function closeDownloadsPage() {
    if (downloadsLayer.item)
        downloadsLayer.savedState = downloadsLayer.item.captureState()
    downloadsLayer.active = false
}
```

Add `property var savedState: ({})` to the Loader. In `onLoaded`, connect all
signals and call `item.restoreState(savedState)`. Do not keep the Loader active
while hidden.

- [ ] **Step 3: Route completed audiobooks**

Implement:

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

Pass `openAudio:true` through the existing book session metadata. In
`ReaderShell.qml`, add this narrow public seam beside `openBook(path)`:

```qml
function openAudioPanel() {
    chrome.openPanelTo("audio")
}
```

In `bookReaderLayer.onLoaded`, after `item.openBook(bookReaderLayer.bookPath)`,
call:

```qml
if (bookReaderLayer.bookMeta.openAudio === true)
    Qt.callLater(item.openAudioPanel)
```

Do not introduce a player route. Post a Brotherhood `agents/chat.md` heads-up
before touching the Agent 2-owned reader file; the seam is additive and does
not alter reader behavior for normal opens.

- [ ] **Step 4: Migrate taskbar reveal to split revisions**

Watch `LocalDownloads.activeChanged` for a transition from zero to positive
active work. Failed-only attention must update the badge/summary without
repeatedly revealing the taskbar.

- [ ] **Step 5: Run routing verification**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File tests/test_downloads_truth_contract.ps1
powershell -ExecutionPolicy Bypass -File tests/test_downloads_play_arriving_p0.ps1
powershell -ExecutionPolicy Bypass -File tests/test_taskbar_download_reveal_p0.ps1
powershell -ExecutionPolicy Bypass -File tests/test_downloads_page_loads.ps1
```

Expected: all exit `0`.

- [ ] **Step 6: Commit the routing slice safely**

Stage only the Downloads hunks from `qml/Main.qml`, verify no unrelated
Main changes are staged, then commit with the page/test files:

```powershell
git commit -m "feat: restore Downloads state and audiobook routing"
```

---

### Task 8: Virtualize growing collections and finish local polish

**Files:**

- Modify: `qml/DownloadsPage.qml`
- Modify: `tests/test_downloads_truth_contract.ps1`
- Modify: `tests/downloads_page_behavior_harness.qml`

**Interfaces:**

- Consumes: semantic focus and page models from Tasks 5–6.
- Produces: no unbounded `Repeater` for jobs, series, seasons, or items.

- [ ] **Step 1: Add failing capacity assertions**

Require `ListView`, `sourceSize`, `maximumContentWidth`, overflow controls, and
the existing accent token. Reject Unicode window/action glyphs and unbounded
model-sized `Repeater` blocks.

Run the contract. Expected: failure naming `maximumContentWidth`.

- [ ] **Step 2: Virtualize every growing collection**

Use vertical `ListView` for active groups, group children, season groups, and
ledger items. Use horizontal `ListView` for each media lane. Fixed chrome
collections may remain declarative.

Keep delegate identity in model data so virtualization does not break semantic
focus restoration.

- [ ] **Step 3: Add shelf overflow affordances**

Show previous/next buttons only when:

```qml
visible: laneView.contentWidth > laneView.width
enabled: laneView.contentX > 0 // previous
enabled: laneView.contentX < laneView.contentWidth - laneView.width // next
```

Buttons animate `contentX` by one viewport and remain keyboard accessible.

- [ ] **Step 4: Apply page-local rendering repairs**

- Center a content column capped by `property real maximumContentWidth: 1680`.
- Bind cover `sourceSize.width/height` to rendered size ×
  `Screen.devicePixelRatio`.
- Use existing Lucide SVGs for back/search/window/action glyphs.
- Replace hardcoded gold with `theme.accent` or the page's existing equivalent
  token.
- Remove the page-owned `Behavior on height` fold animation. Colosseum has no
  global reduced-motion preference today, so an instant fold is the only
  truthful local reduced-motion path without inventing a new global setting.
- Leave shared Theme, ShaderEffectSource/backdrop, and ScrollGlide code
  untouched.

- [ ] **Step 5: Exercise the 100-series/500-item harness**

Populate the fake facade with 100 series and 500 completed items. After layout,
assert instantiated delegates remain bounded to the viewport plus cache, shelf
next/previous controls change `contentX`, and focus restoration succeeds after
delegates recycle.

Run:

```powershell
powershell -ExecutionPolicy Bypass -File tests/test_downloads_behavior.ps1
powershell -ExecutionPolicy Bypass -File tests/test_downloads_page_loads.ps1
powershell -ExecutionPolicy Bypass -File tests/test_downloads_truth_contract.ps1
```

Expected: all exit `0`.

- [ ] **Step 6: Commit the capacity/polish slice**

Stage the page and tests only, inspect, then commit:

```powershell
git commit -m "perf: bound Downloads rendering work"
```

---

### Task 9: Run the full gate and prepare Hemanth's eyes-on pass

**Files:**

- Create: `agents/downloads-page-eyes-on-checklist-2026-07-26.md`
- Modify: `C:\Users\Suprabha\Desktop\Brotherhood\agents\chat.md`

**Interfaces:**

- Produces: one evidence ledger mapping all 28 findings to automated evidence
  and an unchecked eyes-on result.

- [ ] **Step 1: Run scoped contracts**

Run:

```powershell
$tests = @(
  'tests/test_downloads_truth_contract.ps1',
  'tests/test_downloads_behavior.ps1',
  'tests/test_downloads_page_loads.ps1',
  'tests/test_downloads_manager_p0.ps1',
  'tests/test_downloads_play_arriving_p0.ps1',
  'tests/test_taskbar_download_reveal_p0.ps1',
  'tests/test_chrome_fullscreen_toggle_p0.ps1',
  'tests/test_scroll_glide_p0.ps1'
)
foreach ($test in $tests) {
    powershell -ExecutionPolicy Bypass -File $test
    if ($LASTEXITCODE -ne 0) { throw "$test failed" }
}
```

Expected: every script exits `0`.

- [ ] **Step 2: Run native and stock build verification**

Run:

```powershell
cmake --build native/build-msvc --target download_file_ops_harness colosseum --config Release
native\build-msvc\Release\download_file_ops_harness.exe
```

Expected: build and harness exit `0`; no new library appears in the link line.

- [ ] **Step 3: Inspect correctness and security-sensitive diffs**

Run:

```powershell
git diff fc2bf80..HEAD -- native/engine native/player qml/DownloadsPage.qml qml/Main.qml tests
rg -n "LocalDownloads\\.retry\\(|text:\\s*\"Remove\"|cancelled by user|console\\.log\\(.*path|console\\.log\\(.*url" qml/DownloadsPage.qml native/engine
```

Expected: no direct page retry bypass, no destructive `Remove`, no
cancellation-as-failure state, and no new logging of paths or URLs.

- [ ] **Step 4: Write the eyes-on checklist**

Create one row for each P0/P1/P2 finding with:

- code/test evidence;
- real-app recipe;
- `Hemanth result: UNCHECKED`.

Include explicit recipes for forced deletion failure, audiobook-only activity,
unknown length, unequal byte sizes, keyboard-only navigation, close/reopen
state, and 100-series/500-item capacity.

- [ ] **Step 5: Post the handoff pointer**

Append one signed line to Brotherhood `agents/chat.md`:

```text
[Agent 0 (Codex), foundation] READY FOR EYES — Downloads repair: fc2bf80..HEAD | Gate ledger: Colosseum/agents/downloads-page-eyes-on-checklist-2026-07-26.md | Hemanth alone closes findings.
```

- [ ] **Step 6: Commit the gate artifacts in their owning repositories**

In Colosseum, stage and commit the checklist alone:

```powershell
git add -- agents/downloads-page-eyes-on-checklist-2026-07-26.md
git diff --cached
git commit -m "docs: hand off Downloads eyes-on gate"
```

In Brotherhood, stage only the new chat hunk, verify no other brother's chat
entry is staged, and commit:

```powershell
git -C C:\Users\Suprabha\Desktop\Brotherhood add -p -- agents/chat.md
git -C C:\Users\Suprabha\Desktop\Brotherhood diff --cached -- agents/chat.md
git -C C:\Users\Suprabha\Desktop\Brotherhood commit -m "chat: post Downloads eyes-on gate"
```

- [ ] **Step 7: Stop at the human gate**

Report automated results and the exact checklist path. Do not mark any audit
finding closed until Hemanth records his real-app result.
