# Comic Reader — Public Caller Contract (Task 1 oracle)

> **What this is.** The current reader `qml/MangaReader.qml` is a WeebCentral-HTML-shaped
> component about to be rebuilt from scratch ("Comic Reader"). At the Task 13 cutover
> `MangaReader.qml` becomes a **thin wrapper** over the new shell. This document is the
> frozen promise that the rebuild must keep: the exact input properties, signals, injected
> store/Progress calls, the `Progress.record(...)` payload shape (byte-for-byte), and the
> namespace derivation that every caller depends on.
>
> The executable form of this promise is `tests/comicreader_contract_harness.qml`, run by
> `tests/test_comicreader_contract.ps1`. It instantiates the REAL reader offscreen and asserts
> the contract; it prints `COMICREADER_CONTRACT_OK` on success. It passes against the current
> (old) reader today and MUST still pass, unchanged, after the cutover.
>
> All line references below are into `qml/MangaReader.qml` at the Task-1 snapshot.

## 0. Ground-truth note

Every property and signal name my task assumed was **verified against the real component and
matches exactly** — no corrections were needed:

- Input properties `backdrop, seriesTitle, seriesId, seriesCover, chapters, chapterId,
  chapterLabel, western, pageStore, entryKind, entryLabelPrefix` — all present.
- Signals `backRequested(), minimizeRequested(), fullscreenRequested(), closeRequested(),
  sourceRequested(string entryId)` — all present.
- The page-count property is `max` (confirmed, not renamed).

The one contract reality worth flagging (see §4): **`Progress` is a C++ context property**
(`native/main.cpp:808`), not an injectable QML object. Under the offscreen `qml.exe` runner it
is simply undefined, and the reader guards every use with `typeof Progress === "undefined"`.
So the harness cannot inject a fake `Progress` the reader will actually call; it asserts the
namespace via the reader's own `progressKind` property — which is the exact value the record
call stamps into `"kind"`. The `Progress.record(...)` payload shape below is captured verbatim
from source (documentation), not from a live call.

## 1. Required input properties

Declared on the reader root (`qml/MangaReader.qml`):

| Property | Type | Line | Meaning |
|---|---|---|---|
| `backdrop` | `Item` | 22 | The shell backdrop item behind the reader. |
| `seriesTitle` | `string` | 23 | Series display title (also the Continue caption/title). |
| `seriesId` | `string` | 24 | Series identity; the Progress record key. |
| `seriesCover` | `string` | 25 | Series cover URL for the Continue card. |
| `chapters` | `var` (array) | 26 | ALL chapters/volumes, **newest-first**; each `{id, number, name}` (western rows also carry `url`, `sizeMB`). |
| `chapterId` | `string` | 27 | Incoming open target. |
| `chapterLabel` | `string` | 28 | Incoming fallback label. |
| `western` | `bool` | 40 | GetComics comic vs manga. Selects the `Comics` store and the `"comic"` namespace when no `pageStore` is injected. |
| `pageStore` | `var` | 50 | Injected page store (Tankoban volumes façade). Non-null OVERRIDES the western/manga default store. |
| `entryKind` | `string` (default `"manga"`) | 51 | `"manga"` chapters \| `"tankoban"` volumes. Drives the progress namespace and the not-ready acquisition routing. |
| `entryLabelPrefix` | `string` | 52 | Label prefix for entries (e.g. `"Vol. "` for tankoban). |

The page-count property callers/tests read is:

- `readonly property int max: pagesModel.length` — **line 192**. This is THE "page count"
  property (my task's assumed name `max` is correct).

## 2. Required signals

| Signal | Line | Emitted for |
|---|---|---|
| `backRequested()` | 31 | Leave the reader (Esc / back). |
| `minimizeRequested()` | 32 | Minimize the shell window. |
| `fullscreenRequested()` | 33 | Toggle shell fullscreen. |
| `closeRequested()` | 34 | Close the reader window. |
| `sourceRequested(string entryId)` | 57 | A not-ready tankoban volume needs the series page's source chooser (never a chapter-download API). |

**How callers wire them** (verified call sites):

- `qml/MangaSeries.qml:928-951` — full manga/tankoban wiring: sets `entryKind`, `entryLabelPrefix`,
  `pageStore` (`TankobanVolumes` for tankoban, else null), `chapters`, and handles
  `onBackRequested`, `onSourceRequested`, `onMinimizeRequested`, `onFullscreenRequested`,
  `onCloseRequested`.
- `qml/ComicSeries.qml:755-771` — western comic: `western: true`, no `pageStore`, handles back +
  the three chrome signals (no `onSourceRequested` — comics never route to a volume chooser).
- `qml/ComicSeriesPage.qml:687-703` — western comic (GetComics), `western: true`,
  `seriesId: "gc:" + page.gcTag`.
- The chrome signals bubble up through the series pages' own
  `readerMinimizeRequested/readerFullscreenRequested/readerCloseRequested` signals, which
  `qml/Main.qml` connects to the window at lines 1689-1691, 1723-1725, 1750-1752.

## 3. Injected store contract (the `store` object)

`store` is resolved at **lines 59-61**:

```qml
    readonly property var store: pageStore ? pageStore
        : (western ? (typeof Comics !== "undefined" ? Comics : null)
                   : (typeof Downloads !== "undefined" ? Downloads : null))
```

i.e. an injected `pageStore` wins; else `Comics` (western) or `Downloads` (manga). All three
implement the same page-store shape. Methods/signals the reader calls on `store`:

| Call | Line(s) | Contract |
|---|---|---|
| `store.localPages(entryId)` | 272, 466, 549 | Returns an **array** of page objects. Documented shape (line 181): `[{ index, url, group }]` where `url` is a LOCAL `file:///…` string. The reader consumes `pagesModel[i].url` (line 417) and `.length` (`max`). An empty/undefined array means "not downloaded" → the download panel. |
| `store.statusOf(entryId)` | 311-312 | Returns `{ state, done, total }`. Fallback when no id: `{ state: "none", done: 0, total: 0 }`. `state` strings the reader treats as in-flight: `"downloading" \| "queued" \| "resolving" \| "extracting" \| "ingesting" \| "packing"` (line 316-318); `done`/`total` are numbers driving the progress line. |
| `store.downloadChapter(entryId, seriesId, seriesTitle, label)` | 335 | Manga path (`western === false`). Start a chapter download. |
| `store.downloadIssue(entryId, url, seriesId, seriesTitle, label, sizeBytes)` | 332-333 | Western path (`western === true`). `url` = the release permalink from the chapter row (`c.url`); `sizeBytes = (c.sizeMB || 0) * 1024 * 1024`. |

**Store signals** consumed via a `Connections { target: reader.store; ignoreUnknownSignals: true }`
block (**lines 419-437**) — an injected store MAY omit these without warnings:

- `progress(entryId, done, total)` — updates the download line.
- `finished(entryId)` — clears downloading and re-runs `load()`.
- `failed(entryId, reason)` — sets `errorMsg`.

Note `entryKind === "tankoban"` volumes are NOT page-downloaded: `startDownload()` for a tankoban
entry emits `sourceRequested(entryId)` instead of calling any download API (**lines 326, 459, 475**).

## 4. Injected `Progress` contract

`Progress` is a **C++ context property** (`native/main.cpp:808`:
`engine.rootContext()->setContextProperty(QStringLiteral("Progress"), progress);`), referenced as
a global name and guarded with `typeof Progress === "undefined"` at every use.

Calls the reader makes:

- `Progress.get(kind, seriesId)` — **line 217** (`Progress.get(reader.progressKind, reader.seriesId)`)
  and **line 283** (`Progress.get(progressKind, seriesId)`). Returns the saved entry; the reader
  reads `.cover` (to avoid clobbering a saved cover with an empty one) and `.resume`
  (`{ chapterId, page, scrollFrac, maxSeen, finished }`) to restore the reading spot.
- `Progress.record(payload)` — **line 220**. This is the ONLY `Progress.record` call in the file.

### 4.1 The `Progress.record(...)` payload — VERBATIM (load-bearing)

Copied byte-for-byte from `qml/MangaReader.qml:220-233`. The rebuild MUST reproduce this object
exactly (keys, literal colors, and the nested `resume` shape):

```qml
        Progress.record({
            "id": reader.seriesId,
            "kind": reader.progressKind,
            "caption": reader.seriesTitle,
            "title": reader.seriesTitle,
            "sub": reader.curLabel,
            "cover": cov,
            "c1": "#3a2f55", "c2": "#15111f",
            "progress": Math.min(1, Math.max(0, reader.page / reader.max)),
            "resume": { "chapterId": reader.curChapterId, "page": reader.page,
                        "scrollFrac": reader.style === "long_strip" ? reader.stripFrac() : 0,
                        "maxSeen": reader.maxSeen,
                        "finished": reader.maxSeen >= reader.max }
        })
```

- `cov` is `reader.seriesCover`, but if empty it is back-filled from the previously saved
  record's `.cover` (lines 214-219) so a cover-less resume-save never wipes a good cover.
- `stripFrac()` (lines 206-209) is the long-strip scroll fraction (0 outside long-strip).
- `curLabel` (lines 170-175) resolves the chapter/volume display label.

## 5. Progress namespace / kind derivation

The record's `"kind"` is `reader.progressKind`, derived at **line 64**:

```qml
    // western never sets entryKind but keeps its "comic" namespace; every other
    // caller's entryKind IS the namespace ("manga" chapters, "tankoban" volumes).
    readonly property string progressKind: entryKind === "manga" && western ? "comic" : entryKind
```

Resulting namespace:

| `entryKind` | `western` | `progressKind` |
|---|---|---|
| `"manga"` | `false` | `"manga"` |
| `"manga"` | `true` | `"comic"` |
| `"tankoban"` | (any) | `"tankoban"` |

This is the exact logic the harness asserts (flipping `western`/`entryKind` on a live reader and
checking `progressKind`). It guarantees a volume record and a chapter record for the same series
can never overwrite each other.

## 6. The oracle harness

- `tests/comicreader_contract_harness.qml` — offscreen QML harness; instantiates the REAL
  `qml/MangaReader.qml` with a fake page store returning 5 local pages, exercises the manga /
  western-comic / tankoban configurations, and asserts:
  1. `reader.max === 5` after pages load;
  2. `progressKind` flips `manga → comic → tankoban` as `western`/`entryKind` change;
  3. every required signal exists, connects, and emits (chrome signals via a direct emit;
     `sourceRequested` additionally via the tankoban `startDownload()` routing);
  4. the injected store contract is exercised — `localPages(curChapterId)` was called, and
     `downloadChapter` / `downloadIssue` receive the documented arguments.

  It follows the house harness pattern: a non-throwing `ck(cond, label)` collector (a thrown
  error HANGS the offscreen `qml.exe`), prints exactly one `COMICREADER_CONTRACT_OK` sentinel
  when all checks pass, otherwise prints each `COMICREADER_CONTRACT_FAIL: …` and `Qt.exit(1)`.

- `tests/test_comicreader_contract.ps1` — locates `qml.exe` at
  `C:/Qt/6.11.1/msvc2022_64/bin/qml.exe` (the same path every sibling `test_*.ps1` uses), runs the
  harness with `-platform offscreen -I tests/qmlmock`, and asserts the sentinel is present.

Run:

```
powershell -NoProfile -ExecutionPolicy Bypass -File tests/test_comicreader_contract.ps1
```

Expected output: `COMICREADER_CONTRACT_OK`.

### 6.1 Scope boundary — what a GREEN oracle does NOT prove

A green `COMICREADER_CONTRACT_OK` proves the caller-facing surface (properties, signals, store
calls, namespace) is intact. It does **not** prove the `Progress.record(...)` resume payload
(§4.1) survived — `Progress` is a C++ context property that cannot be injected or invoked under
the offscreen `qml.exe` runner, so the payload is captured verbatim here as documentation only,
never machine-asserted by this harness. The resume/Continue payload guarantee lives in **Task 8**
(`ComicReaderState.progressPayload` unit test, asserted against §4.1) and **Task 13** (migration
acceptance). At cutover, do NOT read "contract oracle is green" as "Continue/resume is safe" —
those are two separate guarantees.
