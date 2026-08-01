# Tankoban Discover Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship a Theatre-parity Tankoban Discover tab with offline-first Manga and Comics catalogues, one grouped filter, deterministic house ranking, future catalogue-extension seams, See-all deep links, and a global Explicit Content preference.

**Architecture:** Extract Theatre's current Discover interaction/rendering into a world-neutral `DiscoverBrowser` driven by small QML adapter objects. Extend the existing read-only `MalCatalog` and `ComicsCatalog` native seams with bounded, paged discovery queries; Tankoban's JS adapter normalizes those results and overlays non-blocking Jikan refreshes. A single QML `ContentPreferences` object and pure explicit-classification policy feed all three worlds.

**Tech Stack:** Qt 6.11.1, QML/Qt Quick/Qt Quick Controls, QML JavaScript libraries, C++17, Qt SQL/SQLite, QSettings through QtCore `Settings`, PowerShell contract runners, offscreen `qml.exe`, CMake/Ninja/MSVC 2022.

## Global Constraints

- Default Tankoban tab order is exactly `Discover · Manga · Comics`; Discover is first and default.
- Discover remains utilitarian: no preview pane, editorial hero, decorative shelf stack, or direct-download control.
- Results are series only; cards open existing series pages.
- Only one grouped filter value is active: Manga uses Genres/Demographics; Comics uses Genres/Publishers.
- Manga includes manga, manhwa, and manhua.
- Bundled SQLite results paint before any network response; Jikan refresh never blocks or clears the wall.
- Availability boosts ranking but never removes an otherwise valid title.
- Comics expose one Tankoban house ranking, never source-specific catalogue brands.
- Global setting copy is exactly `Explicit Content` and `Show sexually explicit titles across Theatre, Tankoban, and Biblio. Violence, horror, mature themes, and standard age ratings are not filtered.`
- Explicit Content defaults off. `Berserk`, `Game of Thrones`, Ecchi, violence, horror, `Mature Readers`, R, NC-17, and TV-MA do not become explicit solely from those labels.
- Existing adult-extension installation policy remains unchanged.
- No Comic Vine or Metron runtime dependency is added.
- Execute directly on `master`. Do not create a branch or worktree. Record the initial `git status --short`, preserve every pre-existing dirty/untracked path, and commit only files named by the active task.
- Every offscreen QML harness collects failures and calls `Qt.exit(fails.length)` once; uncaught QML exceptions can hang `qml.exe`.
- QML `font.pixelSize` values remain integers.

---

## File and Interface Map

### New files

- `qml/ExplicitContentPolicy.js` — pure, conservative cross-world classification and visibility policy.
- `qml/ContentPreferences.qml` — one QSettings-backed source of truth for `content/showExplicit`.
- `qml/SettingsPage.qml` — global settings surface; initially contains the Content section and Explicit Content toggle.
- `qml/DiscoverBrowser.qml` — shared Discover controls, state machine, wall, paging, keyboard behavior, and pin application.
- `qml/TankobanDiscoverPage.qml` — Tankoban adapter wrapper and Manga/Comics open signals.
- `qml/TankobanDiscoverApi.js` — Tankoban catalogue descriptors, filters, normalization, pins, local/live merge, and in-session refresh cache.
- `tests/explicit_content_policy_harness.qml`
- `tests/content_preferences_harness.qml`
- `tests/discover_browser_harness.qml`
- `tests/tankoban_discover_api_harness.qml`
- `tests/tankoban_discover_page_harness.qml`
- `tests/mal_catalog_discover_harness.cpp`
- `tests/test_explicit_content_policy.ps1`
- `tests/test_content_preferences.ps1`
- `tests/test_discover_shared_shell.ps1`
- `tests/test_tankoban_discover.ps1`

### Modified files

- `qml/DiscoverPage.qml:17-165` — become Theatre compatibility wrapper around `DiscoverBrowser`.
- `qml/DiscoverApi.js:1-184` — expose a normalized Theatre adapter contract without changing Theatre transport behavior.
- `tests/discover_api_harness.qml` and `tests/discover_page_harness.qml` — preserve Theatre regression coverage.
- `scripts/anime_brain/build_mal_db.py:125-216` — retain explicit rows, add classification axes and discover fields while preserving legacy tag tables.
- `native/engine/MalCatalog.h`, `native/engine/MalCatalog.cpp` — paged Manga discovery/filter queries.
- `native/engine/ComicsCatalog.h`, `native/engine/ComicsCatalog.cpp` — paged Comics discovery/filter queries and house-rank diagnostics.
- `tests/comics_catalog_engine_harness.cpp` — discovery/ranking fixture assertions.
- `native/CMakeLists.txt:114-117,951-956` — native discovery harness target.
- `qml/TankobanWorld.qml:183-231` — new default tab, Discover page, routing, and retained Manga/Comics pages.
- `qml/TankobanMangaTab.qml` and `qml/TankobanComicsTab.qml` — See-all pins.
- `tests/test_tankoban_tabs.ps1` — three-tab contract.
- `qml/WorldPage.qml` — inherited `showExplicitContent` property.
- `qml/Taskbar.qml:18-25,139-224` — Settings signal, active state, and icon.
- `qml/Main.qml:54-106,876-930,1786-1875,2415-2562` — preference instance, settings layer, world bindings, Escape order, and taskbar wiring.
- `qml/GenreIndexApi.js:113-154`, `qml/GenreApi.js:169-210`, `qml/TheatreGenreApi.js:221-312`, `qml/TheatreApi.js:130-140`, and relevant pages — consume the global policy instead of unconditional SFW behavior.
- `qml/BiblioGenreApi.js` and Biblio browse callers — apply explicit policy when source metadata identifies erotica/pornography.

### Shared adapter interface

`DiscoverBrowser.adapter` consumes these exact callable members:

```js
types() // [{ key, label }]
catalogs(type) // [{ key, title, sourceKind, section, attribution }]
filters(type, catalog) // [{ group, options: [{ key, label }] }]
defaultCatalog(type) // string key
resolvePin(pin) // { missing, type, catalogKey, filterGroup, filterKey, missingName }
fetchPage(state, cursor, generation, done)
// done(generation, { items, nextCursor, exhausted, freshness, warning })
```

Every normalized card has:

```js
{
  id: "stable-id",
  type: "manga" | "comics" | "movie" | "series" | "anime",
  title: "Display title",
  cover: "https://...",
  year: 0,
  rating: 0,
  format: "Manga",
  publisher: "",
  availability: false,
  explicit: false,
  raw: {}
}
```

---

### Task 1: Conservative Explicit Content Policy

**Files:**
- Create: `qml/ExplicitContentPolicy.js`
- Create: `tests/explicit_content_policy_harness.qml`
- Create: `tests/test_explicit_content_policy.ps1`

**Interfaces:**
- Consumes: normalized item maps from any world.
- Produces: `classify(world, item) -> { explicit: bool, reason: string }` and `visible(world, item, showExplicit) -> bool`.

- [ ] **Step 1: Write the failing pure-policy harness**

Create fixtures that pin the semantic boundary, including mainstream adult works:

```qml
var cases = [
    ["tankoban", { title:"Berserk", genres:["Action","Gore"], rating:"R+" }, false],
    ["tankoban", { title:"School Comedy", genres:["Ecchi"] }, false],
    ["tankoban", { title:"Explicit Work", genres:["Hentai"] }, true],
    ["tankoban", { title:"Erotic Work", genres:["Erotica"] }, true],
    ["theatre",  { title:"Game of Thrones", certification:"TV-MA" }, false],
    ["theatre",  { title:"Explicit Film", behaviorHints:{ adult:true } }, true],
    ["biblio",   { title:"Adult Novel", audiences:["Adult"] }, false],
    ["biblio",   { title:"Explicit Book", subjects:["Pornography"] }, true],
    ["tankoban", { title:"Unknown" }, false]
]
```

Assert every expected classification, assert hidden only when `showExplicit === false`, and print `EXPLICIT_CONTENT_POLICY_OK` before the single `Qt.exit`.

- [ ] **Step 2: Run the harness and verify the missing module fails**

Run:

```powershell
& 'C:\Qt\6.11.1\msvc2022_64\bin\qml.exe' -platform offscreen -I qml tests\explicit_content_policy_harness.qml
```

Expected: non-zero exit because `ExplicitContentPolicy.js` does not exist.

- [ ] **Step 3: Implement the minimal source-aware policy**

Use explicit source flags first, then exact normalized classifications. Do not keyword-match titles or synopses.

```js
.pragma library

var EXPLICIT_TAGS = {
    "hentai": true,
    "erotica": true,
    "pornography": true,
    "sexually explicit": true,
    "adult film": true
};

function values(item) {
    var out = [];
    [item.genres, item.subjects, item.tags, item.categories].forEach(function(xs) {
        for (var i = 0; i < (xs || []).length; i++) out.push(String(xs[i]).toLowerCase());
    });
    return out;
}

function classify(world, item) {
    item = item || {};
    if (item.explicit === true) return { explicit:true, reason:"source-explicit" };
    if (item.behaviorHints && item.behaviorHints.adult === true)
        return { explicit:true, reason:"source-adult" };
    var tags = values(item);
    for (var i = 0; i < tags.length; i++)
        if (EXPLICIT_TAGS[tags[i]]) return { explicit:true, reason:"classification:" + tags[i] };
    return { explicit:false, reason:"not-explicit" };
}

function visible(world, item, showExplicit) {
    return showExplicit === true || !classify(world, item).explicit;
}
```

- [ ] **Step 4: Run the policy test**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File tests\test_explicit_content_policy.ps1
```

Expected: `EXPLICIT_CONTENT_POLICY_OK` and exit 0.

- [ ] **Step 5: Commit**

```powershell
git add qml/ExplicitContentPolicy.js tests/explicit_content_policy_harness.qml tests/test_explicit_content_policy.ps1
git commit -m "feat: define explicit content policy"
```

---

### Task 2: Global Preference and Settings Surface

**Files:**
- Create: `qml/ContentPreferences.qml`
- Create: `qml/SettingsPage.qml`
- Create: `tests/content_preferences_harness.qml`
- Create: `tests/test_content_preferences.ps1`
- Modify: `qml/Taskbar.qml:18-25,139-224`
- Modify: `qml/Main.qml:54-106,335-365,876-930,2499-2562`

**Interfaces:**
- Consumes: QtCore `Settings`, `assets/icons/settings.svg`, existing full-page loader pattern.
- Produces: object property `showExplicit: bool`, signal `changed()`, taskbar `settingsClicked()`, and `SettingsPage.preferences`.

- [ ] **Step 1: Write failing persistence and static-wiring tests**

The QML harness uses a temporary settings file passed as `settingsLocation`, sets `showExplicit = true`, destroys/recreates the component, and asserts the value reloads. The PowerShell contract asserts:

```powershell
Assert-Contains $taskbar 'signal settingsClicked()' 'taskbar exposes settings door'
Assert-Contains $taskbar '../assets/icons/settings.svg' 'existing settings icon is used'
Assert-Contains $main 'id: contentPreferences' 'one global preference instance'
Assert-Contains $main 'source: "SettingsPage.qml"' 'settings surface is host-owned'
Assert-Contains $page 'Show sexually explicit titles across Theatre, Tankoban, and Biblio.' 'locked helper copy'
```

- [ ] **Step 2: Run and verify failure**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File tests\test_content_preferences.ps1
```

Expected: fail because the preference and settings components are absent.

- [ ] **Step 3: Add the single persisted preference source**

Create `ContentPreferences.qml` with a test-injectable location:

```qml
import QtQuick
import QtCore

QtObject {
    id: root
    property alias settingsLocation: store.location
    property alias showExplicit: store.showExplicit
    signal changed()
    property Settings settingsStore: Settings {
        id: store
        category: "content"
        property bool showExplicit: false
        onShowExplicitChanged: root.changed()
    }
}
```

Production leaves `settingsLocation` unset so Qt uses the application QSettings store. The harness assigns a temporary INI URL through the alias before changing `showExplicit`.

- [ ] **Step 4: Add Settings page and host wiring**

`SettingsPage.qml` exposes:

```qml
property Item backdrop: null
property var preferences: null
signal backRequested()
signal minimizeRequested()
signal fullscreenRequested()
signal closeRequested()
```

Its Content section contains the exact title/helper copy and a switch whose checked value is `preferences ? preferences.showExplicit : false`; clicking assigns `preferences.showExplicit = checked`.

In `Main.qml`, instantiate `ContentPreferences { id: contentPreferences }`, add `openSettingsPage()`/`closeSettingsPage()`, insert `settingsLayer` beside Extensions/Downloads, add it to Escape priority, and wire Taskbar's settings active/clicked properties. Opening any one of Downloads, Extensions, or Settings closes the other two.

- [ ] **Step 5: Run persistence and shell contracts**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File tests\test_content_preferences.ps1
& 'C:\Qt\6.11.1\msvc2022_64\bin\qml.exe' -platform offscreen -I qml qml\Main.qml
```

Expected: preference test passes; the Main smoke reaches a stable frame without a QML component error.

- [ ] **Step 6: Commit**

```powershell
git add qml/ContentPreferences.qml qml/SettingsPage.qml qml/Taskbar.qml qml/Main.qml tests/content_preferences_harness.qml tests/test_content_preferences.ps1
git commit -m "feat: add global explicit content setting"
```

---

### Task 3: Extract the Shared Discover Browser Without Theatre Behavior Change

**Files:**
- Create: `qml/DiscoverBrowser.qml`
- Create: `tests/discover_browser_harness.qml`
- Create: `tests/test_discover_shared_shell.ps1`
- Modify: `qml/DiscoverPage.qml:17-660`
- Modify: `qml/DiscoverApi.js:82-184`
- Modify: `tests/discover_api_harness.qml`
- Modify: `tests/discover_page_harness.qml`

**Interfaces:**
- Consumes: shared adapter interface from this plan's File and Interface Map.
- Produces: `DiscoverBrowser.applyPin(pin)`, `itemOpenRequested(item)`, and public state used by harnesses: `currentType`, `currentCatalog`, `filterSelection`, `items`, `loading`, `keyboardMode`, `catalogMenuOpen`.

- [ ] **Step 1: Write failing shared-shell contracts**

Create a fake adapter in `discover_browser_harness.qml`:

```qml
QtObject {
    id: fake
    function types() { return [{key:"manga", label:"Manga"},{key:"comics", label:"Comics"}] }
    function catalogs(t) { return [{key:"popular", title:"Popular", sourceKind:"builtin", section:"Tankoban", attribution:"Tankoban built-in catalogue"}] }
    function filters(t, c) { return [{group:"Genres", options:[{key:"action",label:"Action"}]}] }
    function defaultCatalog(t) { return "popular" }
    function resolvePin(p) { return {missing:false,type:p.type,catalogKey:p.catalogId,filterGroup:p.filterGroup||"",filterKey:p.filterKey||""} }
    function fetchPage(s, cursor, gen, done) { done(gen,{items:[{id:"1",type:s.type,title:"One",cover:"",year:2001,rating:8,format:"Manga",publisher:"",availability:true,explicit:false,raw:{}}],nextCursor:null,exhausted:true,freshness:"bundled",warning:""}) }
}
```

Assert the default type/catalogue, one active filter, pin application, stale-generation rejection, item activation, per-type session state restoration, filtered-empty clear action, missing-catalogue fallback, bundled/offline warning, and coverless-card construction.

- [ ] **Step 2: Run shared-shell and existing Theatre harnesses to establish the baseline**

Run:

```powershell
& 'C:\Qt\6.11.1\msvc2022_64\bin\qml.exe' -platform offscreen -I qml tests\discover_browser_harness.qml
& 'C:\Qt\6.11.1\msvc2022_64\bin\qml.exe' -platform offscreen -I qml tests\discover_api_harness.qml
& 'C:\Qt\6.11.1\msvc2022_64\bin\qml.exe' -platform offscreen -I qml tests\discover_page_harness.qml
```

Expected: new harness fails because `DiscoverBrowser.qml` is absent; existing Theatre harnesses pass.

- [ ] **Step 3: Move generic state and rendering into `DiscoverBrowser.qml`**

Copy the current visual tree intact, replace direct `Api.*` calls with adapter calls, and collapse filters to one active selection:

```qml
property var adapter: null
property string currentType: ""
property string currentCatalogKey: ""
property string filterGroup: ""
property string filterKey: ""
property var cursor: null
property int fetchGen: 0
property var typeStates: ({})

function requestPage() {
    if (!adapter || loading || exhausted) return
    var gen = ++fetchGen
    adapter.fetchPage({type:currentType, catalogKey:currentCatalogKey,
                       filterGroup:filterGroup, filterKey:filterKey}, cursor, gen,
        function(replyGen, page) {
            if (replyGen !== fetchGen) return
            acceptPage(page)
        })
}
```

`catalogMenuModel` groups by descriptor `section`. A filter menu concatenates groups into section headers plus options and replaces the previous selection on pick.

Implement the exact state outcomes in the shell:

```qml
readonly property string emptyMessage: filterKey.length
    ? "No series match this filter."
    : "This catalogue answered with nothing."
readonly property bool showOfflineNotice: warning === "Showing offline catalogue"
```

A missing pinned extension/catalogue selects the same type's built-in default, clears an invalid filter, and exposes one explanatory notice. A coverless normalized card renders the standard fallback artwork and remains activatable.

- [ ] **Step 4: Turn `DiscoverPage.qml` into the Theatre compatibility wrapper**

Keep its public `applyPin` and `itemOpenRequested` behavior. Define a QML `QtObject` adapter that translates current `DiscoverApi.js` output to the shared contract. Do not change Cinemeta fallback, extension ordering, URLs, required extras, or skip paging.

Expose wrapper aliases needed by existing harnesses:

```qml
property alias currentType: browser.currentType
property alias keyboardMode: browser.keyboardMode
property alias catalogMenuOpen: browser.catalogMenuOpen
readonly property alias catalogMenuModel: browser.catalogMenuModel
function applyPin(pin) { browser.applyPin(pin) }
```

- [ ] **Step 5: Run shared-shell and Theatre regression tests**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File tests\test_discover_shared_shell.ps1
& 'C:\Qt\6.11.1\msvc2022_64\bin\qml.exe' -platform offscreen -I qml tests\discover_api_harness.qml
& 'C:\Qt\6.11.1\msvc2022_64\bin\qml.exe' -platform offscreen -I qml tests\discover_page_harness.qml
& 'C:\Qt\6.11.1\msvc2022_64\bin\qml.exe' -platform offscreen -I qml tests\discover_picker_harness.qml
```

Expected: all pass with Theatre's existing behavior unchanged.

- [ ] **Step 6: Commit**

```powershell
git add qml/DiscoverBrowser.qml qml/DiscoverPage.qml qml/DiscoverApi.js tests/discover_browser_harness.qml tests/discover_api_harness.qml tests/discover_page_harness.qml tests/test_discover_shared_shell.ps1
git commit -m "refactor: share discover browser shell"
```

---

### Task 4: Extend the MAL Bake and Native Manga Discovery Queries

**Files:**
- Modify: `scripts/anime_brain/build_mal_db.py:125-216`
- Modify: `native/engine/MalCatalog.h`
- Modify: `native/engine/MalCatalog.cpp`
- Create: `tests/mal_catalog_discover_harness.cpp`
- Modify: `native/CMakeLists.txt:114-117,951-956`
- Modify: `tests/test_mal_genre_catalog_p0.ps1`

**Interfaces:**
- Consumes: SQLite `manga`, legacy `tag`/`tag_count`, new `classification`/`classification_count` tables.
- Produces:

```cpp
Q_INVOKABLE QVariantList discoverFilters(const QString& axis, bool includeExplicit) const;
Q_INVOKABLE QVariantMap discoverPage(const QString& catalogId,
                                     const QString& filterAxis,
                                     const QString& filterKey,
                                     bool includeExplicit,
                                     int offset, int limit) const;
```

- [ ] **Step 1: Write a failing native fixture harness**

Build a temporary SQLite fixture containing legacy tables plus:

```sql
alter table manga add column explicit integer not null default 0;
alter table manga add column start_date text not null default '';
alter table manga add column favorites integer not null default 0;
create table classification(medium text, axis text, value text, mal_id integer);
create table classification_count(medium text, axis text, value text, total integer,
                                  primary key(medium,axis,value));
```

Insert Berserk, a Manhwa, a low-vote 9.9 title, and an explicit title. Assert Popular order, Top Rated vote-floor/Bayesian behavior, New Releases order, genre/demographic filtering, pagination, `includeExplicit`, and Trending's explicit `fallbackCatalog:"popular"` before comparable snapshots exist.

- [ ] **Step 2: Add the harness target and verify compile failure**

Add `mal_catalog_discover_harness` linking `Qt6::Core Qt6::Sql`, then run:

```powershell
native\build-target.bat mal_catalog_discover_harness
```

Expected: compile failure because the two `MalCatalog` methods do not exist.

- [ ] **Step 3: Extend the bake without breaking existing genre pages**

Stop discarding every `sfw != true` row. Store `explicit = 1` only when the source marks it non-SFW or its exact classification is `Hentai`, `Erotica`, or `Pornography`. Preserve legacy flattened `tag` and `tag_count` tables, and add axis-aware rows:

```python
axes = {
    "genre": listify(row.get("genres")),
    "demographic": listify(row.get("demographics")),
    "theme": listify(row.get("themes")),
}
explicit = int((row.get("sfw") or "").strip().lower() == "false" or
               any(x.lower() in {"hentai", "erotica", "pornography"}
                   for values in axes.values() for x in values))
```

Retain `approved != false`, identity validation, and the existing top-by-members bounded bake. Add `start_date` and `favorites` when present in the Kaggle columns.

- [ ] **Step 4: Implement bounded, allowlisted SQL queries**

Clamp `offset >= 0` and `1 <= limit <= 100`. Accept only catalogue ids `popular`, `top-rated`, `new-releases`, `trending` and axes `genre`, `demographic`, or empty. Bind filter values; never concatenate them into SQL.

Return:

```cpp
QVariantMap{{"items", rows},
            {"nextOffset", offset + rows.size()},
            {"exhausted", rows.size() < limit},
            {"freshness", "bundled"},
            {"fallbackCatalog", catalogId == "trending" ? "popular" : ""}};
```

Each row includes stable MAL id, title, type, score, voters, members, favourites, year/start date, cover, classifications, explicit, and `availability:false` for the adapter to enrich.

- [ ] **Step 5: Build and run the native harness**

Run:

```powershell
native\build-target.bat mal_catalog_discover_harness
native\build-msvc\mal_catalog_discover_harness.exe
powershell -ExecutionPolicy Bypass -File tests\test_mal_genre_catalog_p0.ps1
```

Expected: native harness prints `MAL_CATALOG_DISCOVER_OK`; existing genre-catalog test passes.

- [ ] **Step 6: Rebuild and probe the local untracked MAL artifact**

Run:

```powershell
python scripts\anime_brain\build_mal_db.py
@'
import sqlite3
d=sqlite3.connect('data/mal_catalog.db')
print(d.execute("select count(*) from classification where medium='manga'").fetchone()[0])
print(d.execute("select count(*) from manga where explicit=1").fetchone()[0])
'@ | python -
```

Expected: classification count is non-zero and explicit count is non-zero. `data/mal_catalog.db` remains an untracked deployment artifact and is not staged.

- [ ] **Step 7: Commit**

```powershell
git add scripts/anime_brain/build_mal_db.py native/engine/MalCatalog.h native/engine/MalCatalog.cpp tests/mal_catalog_discover_harness.cpp native/CMakeLists.txt tests/test_mal_genre_catalog_p0.ps1
git commit -m "feat: add paged manga discovery catalog"
```

---

### Task 5: Add Comics Discovery Queries and House Ranking

**Files:**
- Modify: `native/engine/ComicsCatalog.h`
- Modify: `native/engine/ComicsCatalog.cpp:190-409`
- Modify: `tests/comics_catalog_engine_harness.cpp`
- Modify: `tests/test_comics_catalog_db.ps1`

**Interfaces:**
- Consumes: `curated_series`, `curated_genre`, `curated_edition`; existing LOCG rank and GetComics availability.
- Produces:

```cpp
Q_INVOKABLE QVariantList discoverFilters(const QString& axis, bool includeExplicit) const;
Q_INVOKABLE QVariantMap discoverPage(const QString& catalogId,
                                     const QString& filterAxis,
                                     const QString& filterKey,
                                     bool includeExplicit,
                                     int offset, int limit) const;
```

- [ ] **Step 1: Extend the existing fixture with ranking edge cases**

Add curated rows for a high-LOCG-rank unavailable title, a slightly lower-ranked downloadable title, a no-rank title, a recent release, a deep-stocked series, and a `Mature Readers` series. Add an explicitly classified row separately. Assert:

```cpp
if (popular[0].toMap().value("title") != "Available Favorite") return fail("availability boost");
if (allTitles.size() != explicitOffCount + 1) return fail("explicit gate only");
if (!matureReadersVisible) return fail("Mature Readers is not explicit by itself");
if (missingRankRow.value("houseComponents").toMap().value("metadata").toDouble() > 0.10)
    return fail("metadata redistribution ceiling");
```

Also assert publisher/genre filters, stable pagination, New Releases by publication date rather than row modification, Most Stocked by known edition/issue depth, and All Series alphabetical ordering.

- [ ] **Step 2: Build and verify the missing-method failure**

Run:

```powershell
native\build-target.bat comics_catalog_engine_harness
```

Expected: compile failure at the new discovery calls.

- [ ] **Step 3: Implement filter enumeration**

`discoverFilters("genre", includeExplicit)` groups/counts `curated_genre`; `discoverFilters("publisher", ...)` groups/counts `curated_series.publisher`. Return `{key,label,count}` maps ordered count descending then label ascending. Exclude only rows classified explicit when the setting is off.

- [ ] **Step 4: Implement deterministic catalogue queries and diagnostics**

For Popular, compute normalized components per candidate and return both the composite and debug map:

```cpp
const double house = 0.65 * popularity
                   + 0.20 * availability
                   + 0.10 * recency
                   + 0.05 * metadata;
row["houseScore"] = house;
row["houseComponents"] = QVariantMap{{"popularity", popularity},
                                      {"availability", availability},
                                      {"recency", recency},
                                      {"metadata", metadata}};
```

When LOCG rank is missing, redistribute its weight only across available availability/recency signals; metadata remains capped at 0.10 of the final weight. Sort ties by canonical normalized title then start year. Return normalized card inputs, `nextOffset`, `exhausted`, and `freshness:"bundled"`.

- [ ] **Step 5: Run native and PowerShell tests**

Run:

```powershell
native\build-target.bat comics_catalog_engine_harness
native\build-msvc\comics_catalog_engine_harness.exe
powershell -ExecutionPolicy Bypass -File tests\test_comics_catalog_db.ps1
```

Expected: all pass, including legacy `series`, search, downloads, shelves, curated rows, and new discovery cases.

- [ ] **Step 6: Commit**

```powershell
git add native/engine/ComicsCatalog.h native/engine/ComicsCatalog.cpp tests/comics_catalog_engine_harness.cpp tests/test_comics_catalog_db.ps1
git commit -m "feat: add comics discovery house ranking"
```

---

### Task 6: Tankoban Discover Adapter and Offline/Live Merge

**Files:**
- Create: `qml/TankobanDiscoverApi.js`
- Create: `tests/tankoban_discover_api_harness.qml`
- Modify: `tests/test_tankoban_discover.ps1`

**Interfaces:**
- Consumes: passed-in `MalCatalog` and `ComicsCatalog` objects; `ExplicitContentPolicy`; Jikan XHR.
- Produces: Tankoban implementation of the shared adapter interface plus pure helpers `normalizeManga`, `normalizeComic`, `mergeByIdentity`, `jikanUrl`, and `resolvePin`.

- [ ] **Step 1: Write the failing pure-adapter harness**

Use fake catalog objects with synchronous `discoverFilters`/`discoverPage`. Assert exact launch descriptors:

```js
Manga: Trending, Popular, Top Rated, New Releases
Comics: Popular, New Releases, Most Stocked, All Series
```

Assert Manga filter sections Genres/Demographics, Comics sections Genres/Publishers, stable keys, one-filter pin resolution, Manga/Manhwa/Manhua normalization, series-only identity, explicit filtering, availability preservation, canonical-id dedupe, and future extension catalogues appended under `Extensions`.

- [ ] **Step 2: Run and verify failure**

Run:

```powershell
& 'C:\Qt\6.11.1\msvc2022_64\bin\qml.exe' -platform offscreen -I qml tests\tankoban_discover_api_harness.qml
```

Expected: module-not-found failure.

- [ ] **Step 3: Implement descriptors, filters, normalization, and pins**

The adapter factory receives dependencies explicitly because `.pragma library` cannot see QML context properties:

```js
function create(malCatalog, comicsCatalog, extensions, showExplicit) {
    return {
        types: function() { return [{key:"manga",label:"Manga"},{key:"comics",label:"Comics"}] },
        catalogs: function(type) { return catalogsFor(type, extensions || []) },
        filters: function(type, catalog) { return filtersFor(type, catalog, malCatalog, comicsCatalog, showExplicit) },
        defaultCatalog: function(type) { return "popular" },
        resolvePin: resolvePin,
        fetchPage: function(state, cursor, generation, done) {
            fetchPage(malCatalog, comicsCatalog, showExplicit, state, cursor, generation, done)
        }
    };
}
```

Built-in descriptors use `section:"Tankoban"` and `attribution:"Tankoban built-in catalogue"`. Future compatible descriptors use `section:"Extensions"` and their manifest name as attribution. Download-only extensions are rejected by the catalogue-capability predicate.

- [ ] **Step 4: Add local-first page delivery and non-blocking Jikan refresh**

For Manga, call native `discoverPage`, normalize, policy-filter, and invoke `done` immediately with `freshness:"bundled"`. Then issue the matching Jikan request. Use `sfw=true` when Explicit Content is off and `sfw=false` when on. Live responses merge only by stable MAL id.

Maintain an in-session cache:

```js
var liveCache = {}; // state key -> { fetchedAt, items }
var CACHE_MS = 15 * 60 * 1000;
```

Do not reorder a wall after `interactionStarted` is true; cache the refreshed order for the next reload. Trending compares two cached snapshots of members/favourites; with fewer than two comparable snapshots it returns Popular order and warning `Trending is using the latest popularity snapshot.`

For Comics, deliver the native page only; no new live dependency is introduced.

- [ ] **Step 5: Run adapter tests**

Run:

```powershell
& 'C:\Qt\6.11.1\msvc2022_64\bin\qml.exe' -platform offscreen -I qml tests\tankoban_discover_api_harness.qml
powershell -ExecutionPolicy Bypass -File tests\test_tankoban_discover.ps1
```

Expected: adapter harness passes; static contract confirms no Comic Vine/Metron dependency and no direct download action.

- [ ] **Step 6: Commit**

```powershell
git add qml/TankobanDiscoverApi.js tests/tankoban_discover_api_harness.qml tests/test_tankoban_discover.ps1
git commit -m "feat: add Tankoban discover adapter"
```

---

### Task 7: Tankoban Discover Page, Default Tab, and Existing Detail Routes

**Files:**
- Create: `qml/TankobanDiscoverPage.qml`
- Create: `tests/tankoban_discover_page_harness.qml`
- Modify: `qml/TankobanWorld.qml:183-231`
- Modify: `qml/WorldPage.qml`
- Modify: `qml/Main.qml:1786-1875`
- Modify: `tests/test_tankoban_tabs.ps1`
- Modify: `tests/test_tankoban_discover.ps1`

**Interfaces:**
- Consumes: shared `DiscoverBrowser`, Tankoban adapter, `MalCatalog`, `ComicsCatalog`, global `showExplicitContent`.
- Produces: `mangaSeriesRequested(var item)`, `comicSeriesRequested(var item)`, and `applyPin(var pin)`.

- [ ] **Step 1: Write the failing page and routing harness**

Instantiate `TankobanDiscoverPage` with fake catalog dependencies and assert:

- default type is Manga and catalogue is Popular;
- Manga card activation emits `mangaSeriesRequested` once;
- Comics card activation emits `comicSeriesRequested` once;
- changing type preserves each type's state;
- a stale pin filter is dropped while its valid type/catalogue remains;
- no card path emits a download request.

Update `test_tankoban_tabs.ps1` to require `activeTab: "discover"`, three exact tab keys, and `TankobanDiscoverPage`.

- [ ] **Step 2: Run and verify failure**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File tests\test_tankoban_tabs.ps1
& 'C:\Qt\6.11.1\msvc2022_64\bin\qml.exe' -platform offscreen -I qml tests\tankoban_discover_page_harness.qml
```

Expected: fail because Tankoban still defaults to Manga and the page is absent.

- [ ] **Step 3: Build the Tankoban wrapper**

`TankobanDiscoverPage.qml` creates the adapter with passed dependencies, binds it to `DiscoverBrowser`, and routes by normalized type:

```qml
property var malCatalog: null
property var comicsCatalog: null
property var extensions: []
property bool showExplicitContent: false
signal mangaSeriesRequested(var item)
signal comicSeriesRequested(var item)

DiscoverBrowser {
    id: browser
    adapter: root.adapter
    onItemOpenRequested: function(item) {
        if (item.type === "comics") root.comicSeriesRequested(item)
        else root.mangaSeriesRequested(item)
    }
}
```

Recreate the adapter when extension registry or explicit preference revision changes, preserving valid browser state.

- [ ] **Step 4: Wire TankobanWorld and Main**

Change Tankoban tab model to:

```qml
[
  { key:"discover", label:"Discover" },
  { key:"manga", label:"Manga" },
  { key:"comics", label:"Comics" }
]
```

Use a `Loader` or visible page branches so Discover is constructed once and retained. Manga and Comics keep their current data ownership and signals. Add inherited `WorldPage.showExplicitContent`; in Main's world-loader `onLoaded`, bind it to `contentPreferences.showExplicit`.

Manga cards route to existing `seriesRequested` using title/id data already understood by `openSeries`; Comics cards emit the existing `comicSeriesRequested` normalized map understood by `openComicSeries`/`openGcdSeries`. Do not add a new detail page.

- [ ] **Step 5: Run page, tab, and Main smoke tests**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File tests\test_tankoban_tabs.ps1
powershell -ExecutionPolicy Bypass -File tests\test_tankoban_discover.ps1
& 'C:\Qt\6.11.1\msvc2022_64\bin\qml.exe' -platform offscreen -I qml tests\tankoban_discover_page_harness.qml
& 'C:\Qt\6.11.1\msvc2022_64\bin\qml.exe' -platform offscreen -I qml qml\Main.qml
```

Expected: all pass and Main reaches stable frame.

- [ ] **Step 6: Commit**

```powershell
git add qml/TankobanDiscoverPage.qml qml/TankobanWorld.qml qml/WorldPage.qml qml/Main.qml tests/tankoban_discover_page_harness.qml tests/test_tankoban_tabs.ps1 tests/test_tankoban_discover.ps1
git commit -m "feat: add Tankoban Discover tab"
```

---

### Task 8: Theatre-Style See-All Pins From Manga and Comics Shelves

**Files:**
- Modify: `qml/TankobanMangaTab.qml`
- Modify: `qml/TankobanComicsTab.qml`
- Modify: `qml/TankobanWorld.qml`
- Modify: `tests/test_tankoban_tabs.ps1`
- Modify: `tests/tankoban_discover_page_harness.qml`

**Interfaces:**
- Consumes: pin shape `{type,catalogId,filterGroup,filterKey}` and `TankobanDiscoverPage.applyPin`.
- Produces: `discoverPinRequested(var pin)` from each browse tab.

- [ ] **Step 1: Add failing static and behavioral assertions**

Require these mappings:

```text
Top in Tankoban — Manga -> {type:manga,catalogId:popular}
Manga genre/demographic -> {type:manga,catalogId:popular,filterGroup:genre|demographic,filterKey:key}
Top in Tankoban — Comics -> {type:comics,catalogId:popular}
Marvel/DC/Image shelf -> {type:comics,catalogId:popular,filterGroup:publisher,filterKey:key}
Most Stocked -> {type:comics,catalogId:most-stocked}
```

Harness applies each pin and asserts type/catalogue/filter plus wall scroll reset.

- [ ] **Step 2: Run and verify failure**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File tests\test_tankoban_tabs.ps1
& 'C:\Qt\6.11.1\msvc2022_64\bin\qml.exe' -platform offscreen -I qml tests\tankoban_discover_page_harness.qml
```

Expected: missing signals/mappings fail.

- [ ] **Step 3: Emit stable pins and connect them in TankobanWorld**

Add `signal discoverPinRequested(var pin)` to each tab. Existing See-all/genre/publisher doors emit stable lower-case keys. In TankobanWorld:

```qml
function openDiscoverPin(pin) {
    activeTab = "discover"
    discoverPage.applyPin(pin)
}
```

Do not alter current direct series-card routes. If a shelf has no honest Discover equivalent, retain its existing route instead of manufacturing a pin.

- [ ] **Step 4: Run pin tests**

Run the two commands from Step 2.

Expected: all mappings pass; invalid filter keys clear only the filter.

- [ ] **Step 5: Commit**

```powershell
git add qml/TankobanMangaTab.qml qml/TankobanComicsTab.qml qml/TankobanWorld.qml tests/test_tankoban_tabs.ps1 tests/tankoban_discover_page_harness.qml
git commit -m "feat: route Tankoban shelves into Discover"
```

---

### Task 9: Apply Explicit Content Preference Across All Worlds

**Files:**
- Modify: `qml/GenreIndexApi.js:113-154`
- Modify: `qml/GenreApi.js:169-210`
- Modify: `qml/GenreIndex.qml`
- Modify: `qml/GenrePage.qml`
- Modify: `qml/TheatreGenreApi.js:221-312`
- Modify: `qml/TheatreApi.js:130-140`
- Modify: `qml/TheatreGenreIndex.qml`
- Modify: `qml/DiscoverApi.js`
- Modify: `qml/DiscoverPage.qml`
- Modify: `qml/BiblioGenreApi.js`
- Modify: `qml/BiblioWorld.qml`
- Modify: `qml/Main.qml`
- Modify: `tests/explicit_content_policy_harness.qml`
- Modify: `tests/test_explicit_content_policy.ps1`

**Interfaces:**
- Consumes: `ContentPreferences.showExplicit` and `ExplicitContentPolicy.visible`.
- Produces: consistent filtering/URL policy across Tankoban, Theatre, and Biblio surfaces with source metadata capable of identifying explicit work.

- [ ] **Step 1: Extend failing cross-world contracts**

Static assertions require pages to receive `showExplicitContent`, Jikan URLs to derive `sfw` from it, genre indexes to include explicit groups only when enabled, and Theatre/Biblio normalized results to pass through `ExplicitContentPolicy.visible`.

Keep regression fixtures for `Berserk`, `Game of Thrones`, Ecchi, `Mature Readers`, R/NC-17/TV-MA, horror, romance, and adult readership visible when the setting is off.

- [ ] **Step 2: Run and verify failure**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File tests\test_explicit_content_policy.ps1
```

Expected: fail because several world paths still hardcode `sfw=true` or do not consume the preference.

- [ ] **Step 3: Thread the preference through host-owned pages**

Bind world pages from Main using `WorldPage.showExplicitContent`. For standalone genre/index layers, add a property on the Loader and loaded item:

```qml
property bool showExplicitContent: contentPreferences.showExplicit
onLoaded: item.showExplicitContent = Qt.binding(function() {
    return contentPreferences.showExplicit
})
```

Reload only the active catalogue/filter when the setting changes; preserve valid navigation state.

- [ ] **Step 4: Replace unconditional SFW behavior with conservative policy**

For Jikan URLs:

```js
var sfw = showExplicit ? "false" : "true";
```

For local/extension results, apply `Policy.visible(world, item, showExplicit)`. Change Manga's explicit group from `Ecchi, Erotica, Hentai` to `Erotica, Hentai`; Ecchi remains an ordinary visible genre. Never use certification alone as explicit evidence.

Do not change `ExtensionsStore` preview/install refusal for manifests declaring `behaviorHints.adult`.

- [ ] **Step 5: Run policy and existing world regressions**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File tests\test_explicit_content_policy.ps1
powershell -ExecutionPolicy Bypass -File tests\test_mal_genre_catalog_p0.ps1
powershell -ExecutionPolicy Bypass -File tests\test_theatre_af2_p0.ps1
& 'C:\Qt\6.11.1\msvc2022_64\bin\qml.exe' -platform offscreen -I qml tests\discover_api_harness.qml
```

Expected: all pass; adult-extension installation contract remains unchanged.

- [ ] **Step 6: Commit**

```powershell
git add qml/GenreIndexApi.js qml/GenreApi.js qml/GenreIndex.qml qml/GenrePage.qml qml/TheatreGenreApi.js qml/TheatreApi.js qml/TheatreGenreIndex.qml qml/DiscoverApi.js qml/DiscoverPage.qml qml/BiblioGenreApi.js qml/BiblioWorld.qml qml/Main.qml tests/explicit_content_policy_harness.qml tests/test_explicit_content_policy.ps1
git commit -m "feat: apply explicit content preference across worlds"
```

---

### Task 10: Full Acceptance, Eyes-On, and Documentation Reconciliation

**Files:**
- Modify: `README.md:30-58,61-123,157-169,278-286`
- Modify: `tests/test_tankoban_discover.ps1`
- Modify only if failures prove a defect: files already named in Tasks 1-9.

**Interfaces:**
- Consumes: all prior tasks.
- Produces: one repeatable acceptance runner, updated current-state documentation, and eyes-on evidence.

- [ ] **Step 1: Make the acceptance runner execute every focused gate**

`tests/test_tankoban_discover.ps1` must run or invoke:

```powershell
tests\test_explicit_content_policy.ps1
tests\test_content_preferences.ps1
tests\test_discover_shared_shell.ps1
tests\test_tankoban_tabs.ps1
tests\test_mal_genre_catalog_p0.ps1
tests\test_comics_catalog_db.ps1
native\build-msvc\mal_catalog_discover_harness.exe
native\build-msvc\comics_catalog_engine_harness.exe
qml.exe -platform offscreen -I qml tests\tankoban_discover_api_harness.qml
qml.exe -platform offscreen -I qml tests\tankoban_discover_page_harness.qml
```

It prints `TANKOBAN_DISCOVER_ACCEPTANCE_OK` only after every child exits 0.

- [ ] **Step 2: Run a clean targeted native build**

Run:

```powershell
native\build-target.bat mal_catalog_discover_harness
native\build-target.bat comics_catalog_engine_harness
native\build-target.bat colosseum
```

Expected: all targets build successfully with the MSVC Qt 6.11.1 kit.

- [ ] **Step 3: Run the full focused acceptance suite**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File tests\test_tankoban_discover.ps1
```

Expected: `TANKOBAN_DISCOVER_ACCEPTANCE_OK` and exit 0.

- [ ] **Step 4: Run the app and perform eyes-on acceptance**

Launch the development build using the repository's normal launcher. Verify:

1. Tankoban defaults to Discover.
2. Featured/Next Up/Continue remain above the tab bar.
3. Manga and Comics share the Theatre shell and restore independent state.
4. One grouped filter is active at a time.
5. Manga/Comics covers open existing series pages and Back restores scroll.
6. Local rows appear before a delayed/offline Jikan response.
7. Covers do not reorder after pointer/keyboard interaction begins.
8. See-all doors land on the correct pinned state.
9. Settings copy is exact and persists after restart.
10. Explicit off hides an explicit fixture but does not hide Berserk/Game of Thrones-class mainstream work.

Record the command, build identity, and pass/fail notes in the implementation session handoff. Do not commit generated logs, `.superpowers/`, `dist/`, or local SQLite artefacts.

- [ ] **Step 5: Update README current-state boundaries**

Document:

- Tankoban now has Discover/Manga/Comics tabs.
- Discover is offline-first and series-only.
- Comics use a house ranking; Manga uses the bundled MAL catalogue plus Jikan refresh.
- Explicit Content is global and means sexually explicit, not mature-rated.
- Discovery extensions are supported by the seam but none ship yet.
- Download-source extensions remain separate.

- [ ] **Step 6: Run final diff and regression checks**

Run:

```powershell
git diff --check
git status --short
powershell -ExecutionPolicy Bypass -File tests\test_tankoban_discover.ps1
```

Expected: no whitespace errors; only intentional files are modified; acceptance passes again.

- [ ] **Step 7: Commit**

```powershell
git add README.md tests/test_tankoban_discover.ps1
git commit -m "docs: record Tankoban Discover rollout"
```

---

## Completion Gate

Before declaring completion:

1. Run `superpowers:verification-before-completion`.
2. Review every locked decision in `docs/superpowers/specs/2026-08-01-tankoban-discover-design.md` as MET, PARTIAL, or NOT-MET.
3. Run a cross-substrate self-review using `brotherhood-review` against the written Definition of Done.
4. Request code review using `superpowers:requesting-code-review`.
5. Do not push until review findings are fixed, the focused acceptance runner passes, and no task-owned change remains uncommitted. Pre-existing unrelated dirty paths remain untouched.
