## Recommended UX

Revised direction after design review: this should **not** be a small taskbar popover. Downloads should become a full Colosseum page / world-level surface, closer to a file manager with a media OS library twist.

The taskbar still owns entry. Colosseum has no sidebar, so the persistent shell should stay Windows-taskbar-like at the bottom. Put the **Downloads** button directly beside the **Colosseum** taskbar icon. When selected, it should look like an active taskbar app/page with the same active underline/glow language as the current taskbar. A small badge can show active jobs, for example `5`.

The selected UX is **Concept B: world lanes**. The page content should be organized as:

1. **World first**
   - `Tankoban`
   - `Biblio`
   - `Theatre`

2. **Series second**
   - Tankoban: manga/comic series, for example `Berserk`, `One Piece`, `Kagurabachi`.
   - Biblio: book series, author/title clusters, or `Standalone Books`, for example `Dune Saga`, `Foundation`, `Standalone Books`.
   - Theatre: movie/TV title/franchise, for example `Foundation`, `Gladiator`, `The Expanse`.

3. **Downloaded item third**
   - Tankoban item = chapter/volume unit.
   - Biblio item = book edition/file.
   - Theatre item = movie file or episode file.

The page should have a compact page header, not a sidebar: `Downloads` plus `Organized by world, then series`. Under that, a metrics strip is useful: total items, Tankoban count, Biblio count, Theatre count, active jobs, local storage size.

Each world lane should be a tall glass panel. Each lane header shows world name, icon, count, and subtitle. Inside the lane, series cards can expand/collapse. Expanded cards show rows with thumbnail/cover slot, item title, metadata, status, progress, and action buttons.

Item actions:

- Complete item primary action opens the right surface: manga reader, book reader, or player.
- Active item action shows pause/cancel, depending on backend support.
- Queued item action shows cancel/remove from queue.
- Failed item action shows retry only if the native layer has enough payload to retry safely.
- Complete item secondary action deletes/removes local files, guarded if the item is currently open.
- Optional later action: reveal in file manager. Do not make it part of slice 1 unless a native cross-platform opener already exists.

Empty state should be real, not fake. If a world has no downloads, the lane should show a calm empty card such as `No Biblio downloads yet` with a route action like `Open Biblio`. Do not fill the page with sample Catalog data in shipped code.

HTML design artifact committed for this direction:

`chatgpt_requests/20260629-171426-colosseum-taskbar-unified-downloads-panel-plan-review/downloads-page-concept-b-taskbar.html`

## Data/API Plan

Create a unified native/QML facade exposed as something like `LocalDownloads`, `DownloadsVault`, or `ReadingDownloads` if Codex wants to keep the earlier name. Since the UX now includes Theatre too, I recommend the more general name:

```cpp
LocalDownloads
```

Recommended QML contract:

```cpp
Q_PROPERTY(int revision READ revision NOTIFY changed)
Q_INVOKABLE QVariantList worlds() const;
Q_INVOKABLE QVariantList series(const QString& world) const;
Q_INVOKABLE QVariantList items(const QString& world, const QString& seriesKey = QString()) const;
Q_INVOKABLE QVariantList flatItems(const QString& world = QString()) const;
Q_INVOKABLE QVariantMap statusOf(const QString& world, const QString& id) const;
Q_INVOKABLE void cancel(const QString& world, const QString& id);
Q_INVOKABLE void remove(const QString& world, const QString& id);
Q_INVOKABLE void retry(const QString& world, const QString& id);

signals:
void changed();
void itemChanged(QString world, QString id);
```

World records:

```js
{
  key,          // "tankoban" | "biblio" | "theatre"
  title,        // "Tankoban" | "Biblio" | "Theatre"
  icon,
  itemCount,
  activeCount,
  bytes,
  subtitle
}
```

Series records:

```js
{
  key,          // stable world-local series key
  world,        // "tankoban" | "biblio" | "theatre"
  title,
  subtitle,
  cover,
  itemCount,
  completeCount,
  activeCount,
  bytes,
  updatedAt,
  collapsedDefault
}
```

Item records:

```js
{
  id,
  key,              // world + seriesKey + id
  world,            // "tankoban" | "biblio" | "theatre"
  kind,             // "manga" | "comic" | "book" | "movie" | "episode"
  seriesKey,
  seriesTitle,
  title,
  subtitle,
  thumb,
  cover,
  localPath,
  bytes,
  addedAt,
  updatedAt,
  state,            // "done" | "queued" | "resolving" | "downloading" | "failed" | "missing"
  progress,         // 0..1
  received,
  total,
  canOpen,
  canCancel,
  canDelete,
  canRetry,
  route             // payload Main.qml can use to open the correct surface
}
```

Backend composition:

- Tankoban comes from `MangaDownloader` / existing `Downloads`.
- Biblio comes from `BookDownloader` / existing `Books`.
- Theatre should be represented in the schema now, even if its actual local-download backend is slice-later or currently absent. If no Theatre download store exists yet, the Theatre lane can be empty in production, but the architecture should not need redesign later.

Existing downloaders need small list APIs. Keep QML out of private JSON index files.

```cpp
// MangaDownloader
Q_INVOKABLE QVariantList downloadedChapters() const;
Q_INVOKABLE QVariantList activeChapterDownloads() const;

// BookDownloader
Q_INVOKABLE QVariantList downloadedBooks() const;
Q_INVOKABLE QVariantList activeBookDownloads() const;

// Future Theatre/local video store, name depends on current architecture
Q_INVOKABLE QVariantList downloadedVideos() const;
Q_INVOKABLE QVariantList activeVideoDownloads() const;
```

Do not use `Progress` as the source of truth. `Progress` answers “what did I recently watch/read and where do I resume?” Downloads answers “what local media exists or is currently being fetched?” The two can share `route` payloads but should remain separate stores.

The facade should normalize all backends into the same world → series → item shape. It should also own sorting and state naming so QML is mostly rendering, not data interpretation.

## Implementation Slices

1. **Lock the design target / artifact**
   - Target files: this `response.md`, the committed HTML mockup artifact.
   - Direction: Concept B, full page, no sidebar, bottom taskbar entry beside Colosseum icon.
   - Reason: prevents Codex from implementing the old popover plan.

2. **Add native listing APIs to existing backbones**
   - Target files: `native/engine/MangaDownloader.h`, `native/engine/MangaDownloader.cpp`, `native/engine/BookDownloader.h`, `native/engine/BookDownloader.cpp`.
   - Add downloaded and active list methods.
   - Normalize basic fields: id, title, series title, bytes, state, progress, local path/thumb if available.
   - Harden stale-file detection while listing.

3. **Add the unified facade**
   - Target files: new `native/LocalDownloads.h/.cpp` or `native/engine/LocalDownloads.h/.cpp`, plus the project build file.
   - Constructor should receive `MangaDownloader*`, `BookDownloader*`, and later a Theatre/local video provider if one exists.
   - Expose `worlds()`, `series(world)`, `items(world, seriesKey)`, `flatItems(world)`, action methods, and `revision`.
   - Connect to backend progress/finished/failed/removed signals and emit `changed()` / `itemChanged()`.

4. **Register the facade**
   - Target file: `native/main.cpp`.
   - Create it after `downloads` and `books` are created.
   - Expose to QML as `LocalDownloads`.
   - Keep existing `Downloads` and `Books` QML context properties unchanged to avoid breaking current reader/detail flows.

5. **Create the full Downloads page**
   - Target files: new `qml/DownloadsWorld.qml` or `qml/DownloadsPage.qml`; optionally `qml/DownloadWorldLane.qml`, `qml/DownloadSeriesCard.qml`, `qml/DownloadItemRow.qml` if the file gets large.
   - Use a `ListView` for item rows inside a series if lists can be long.
   - Use world lanes for Concept B. Each lane reads `LocalDownloads.series(world)` and then series card rows.
   - No sidebar.

6. **Wire taskbar navigation**
   - Target files: `qml/Taskbar.qml`, `qml/Main.qml`.
   - Place Downloads immediately beside the Colosseum/start icon.
   - Add an active visual state when the current shell page is Downloads.
   - Clicking Downloads should switch to/open the full Downloads page, not open a popover.
   - Maintain the current `visible: !win.immersiveSurfaceOpen` rule so Downloads does not strand over reader/player surfaces.

7. **Route item open actions**
   - Target file: `qml/Main.qml` and/or page signal wiring.
   - Tankoban item opens existing comic/manga session with series id + chapter id.
   - Biblio item opens `openBookSession(localPath, bookMeta)`.
   - Theatre item opens player/local video path or the current Theatre playback route, depending on what local video support exists.

8. **Add Theatre later if backend is not present**
   - If there is no Theatre download store yet, implement the Theatre lane empty state first and keep the schema ready.
   - Do not fake Theatre rows in shipped QML.

## Edge Cases

- **Partial Tankoban download:** show queued/downloading with page progress. Do not open reader until `localPages(chapterId)` returns valid local files.
- **Partial Biblio download:** show resolving before byte progress exists. Never expose `.part` files as readable.
- **Partial Theatre download:** show byte progress if backend exists. Do not route to player until the file is complete enough for the intended playback mode.
- **Failed download:** show failed only if backend reports it. Retry only when the original payload is available. Otherwise route back to the owning detail page.
- **Deleted files outside app:** mark as missing or clean stale entries. Do not offer a broken open action.
- **Manga index stale pages:** `MangaDownloader::localPages()` appears to trust indexed filenames. Listing/opening should verify files exist before exposing `canOpen:true`.
- **Book metadata is thin:** old `books/index.json` entries may only have path/title/bytes/addedAt. Fall back cleanly to filename/title and a book glyph.
- **Multiple book editions:** dedupe by md5, not title. Group by book series/title cluster but keep editions separate.
- **Multiple manga chapters:** dedupe by chapterId. Verify chapterId is globally stable enough in the current scraper.
- **Theatre grouping:** movies can be their own one-item series card. TV episodes should group by show, then season if needed later.
- **Current item deleted while open:** either block delete or close/refresh the active reader/player before deletion. Do not leave surfaces pointing at removed local files.
- **No sample data:** empty world lane means empty world lane. Do not use Catalog rows to populate downloads.
- **Taskbar state:** Downloads active state should clear when another world/page is active.
- **Immersive surfaces:** player/book reader/manga reader should hide taskbar and close any Downloads page overlays/panels if any exist.

## Verification Checklist

- Open the committed HTML mockup and confirm it has no sidebar.
- Confirm the taskbar places the Downloads button immediately beside the Colosseum icon.
- Confirm the page is full-screen/full-page, not a popover.
- Confirm the visual grouping is `Tankoban / Biblio / Theatre → series → items`.
- Build compiles after new native files are added to the build system.
- QML loads with no downloads and shows real empty states, not fake sample rows.
- Download one manga chapter and verify it appears under `Tankoban → Series → Chapter` with correct status/progress.
- Click completed manga row and verify it opens from local pages only.
- Download one Biblio book and verify it appears under `Biblio → Series/title cluster → Book edition` with local path and open action.
- Click completed Biblio row and verify it opens through `openBookSession` / `BookReader.qml`.
- If Theatre backend exists, download or register one local video and verify it appears under `Theatre → Title/franchise → Movie/Episode`.
- If Theatre backend does not exist, verify the Theatre lane is an honest empty state.
- Cancel active Tankoban/Biblio jobs from the Downloads page and verify native backend state changes correctly.
- Delete completed Tankoban/Biblio items and verify files and indexes update.
- Manually delete a book file outside the app and restart; row should not offer broken open.
- Manually delete one manga page outside the app and restart; row should be missing/repairable/deletable, not silently broken.
- Confirm `Progress` continue rows still work and were not repurposed as the downloads source.
- Confirm taskbar hides under immersive surfaces and Downloads cannot remain visually stranded.

## Risks / Things To Inspect Locally

- The original mailbox request asked for a taskbar panel/popover and only mentioned Tankoban/Biblio reading media. Hemanth’s later correction changes the product direction to a full Downloads page with Tankoban, Biblio, and Theatre. Codex should follow the later correction.
- GitHub HEAD may not match the local worktree. The request explicitly says there is unrelated/uncommitted work. Inspect local files before editing.
- The actual taskbar implementation details were not fully visible in the packet. Codex must inspect `qml/Taskbar.qml` locally before wiring active page state.
- The build file path was not visible in the packet. Codex must locate where new native `.cpp` files are registered.
- Theatre may not yet have a real local download backend. Do not fake Theatre entries. Keep the lane/schema ready and ship an empty state if necessary.
- `BookDownloader` currently persists minimal metadata. The page will look better if book cover/author/format/bookId can be saved going forward, but old entries must stay valid.
- `MangaDownloader` stale-file handling needs inspection because a central Downloads page makes broken local entries more visible.
- Retry is risky unless native backends store original request payloads. Prefer `canRetry:false` over guessing.
- The committed HTML file is a design artifact, not production QML. Treat it as layout guidance, not code to port line-for-line.
