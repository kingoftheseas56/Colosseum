# Extensions — subsystem guide

> **Hand-written. Keep it true.** If you change how Colosseum carries, installs, orders, enables, or asks
> Stremio-protocol extensions, update this file in the same commit. The per-file index beside it
> ([`extensions-index.md`](extensions-index.md)) is generated — never edit that one.
>
> Drafted via Preflight-Architect, ground-truthed and adopted by the ZCode-seat session, 2026-08-09.
> Source-read against `master@157986433732c9907e159fa9199dda666b5f6866` (= HEAD at adoption); the draft's original
> pin was one commit earlier (`03c16cd…`) and the intervening commit touched no extensions-owned files. Ground-truth
> pass verified the zero-overlap ownership boundary (`BrowserDrawer.qml` owned by `shell.paths`, correctly excluded
> here), the generation number (`kHouseDefaultsVersion = 10`), the mechanism specifics (12s manifest timeout,
> `behaviorHints.adult` refusal, `QSaveFile` atomic write, `revision`/`changed()` signal), and the migration
> behavior in trap 19 by reading `migrateDefaults()` + `appendHouseDefaults()` + the `kRetiredIds` loop in full.
> Trap 19 was tightened from Preflight's hedged wording to state the exact mechanism. Adopt as the current truth;
> re-verify on the next house-roster change.

## 1. What this subsystem is for

Make **one ordered, persistent roster of what Colosseum is allowed to ask for catalogues, metadata, streams,
subtitles, and universe payloads** — then make every consumer obey that roster instead of quietly carrying its own
idea of which add-ons exist.

The stored unit is an extension entry:

```text
{
    id,
    transportUrl,
    installedAt,
    enabled,
    core,
    manifest
}
```

`ExtensionsStore` owns that list and its persistence. It seeds the house roster, upgrades that roster across house
generations, previews and installs remote manifests, refuses adult manifests, protects core rows, persists the
enabled/order state, and emits one change signal when the roster changes.

The C++ store deliberately does **not** know what “Theatre”, “Tankoban”, “Biblio”, or “Universes” mean.

That interpretation belongs to `ExtensionsCatalog.js`:

- manifest `types` say which media worlds an extension belongs to;
- the `universe` resource is classified by role before ordinary media types;
- a locked `core + catalog` row is a **catalogue** — it fills shelves;
- a non-core `stream` row is a **well** — it is asked for a playable/downloadable answer;
- array order is global storage order, while the visible rank is relative to one world's filtered wells.

**First-run consent (2026-08-09).** A well is *seeded* installed but **disabled** — a fresh profile enables no
removable acquisition/playback source until the user turns it on (the locked Stremio model; fixes the
NoTorrent/Torrentio-on-by-default violation). Core catalogues and non-`stream` capabilities (subtitles,
universes) seed enabled. The seed derives this from the manifest's `resources` + `core`, never a name; an
existing profile keeps its own enabled choices (`add` preserves them, no defaults-version bump). See
`ExtensionsStore.cpp` `entry()` and `tests/auto/extensions/tst_extensions_first_run.cpp`.

`AddonClient.js` is the generic Stremio-protocol caller. It receives the current installed roster from the shell,
matches manifests against a requested resource/type/id, then asks only the enabled extensions that claim they can
answer.

That ownership split is the whole subsystem:

```text
ExtensionsStore
    = what is carried, enabled, and in what stored order

ExtensionsCatalog.js
    = what each entry MEANS to Colosseum's worlds

AddonClient.js / DiscoverApi.js / UniverseExtApi.js
    = ask the resulting capabilities

ExtensionsPage.qml
    = let the user see and change the roster
```

Do not collapse those into one giant “extension API.” Each seam exists because the fact it owns changes for a
different reason.

## 2. The flow

**Boot — persisted roster to every consumer:**

```text
native/main.cpp
    |
    | creates ExtensionsStore with the ordinary uncached network manager
    v
ExtensionsStore()
    |
    +-- load <AppData>/extensions/installed.json
    |
    +-- no rows?
    |      |
    |      +--> seed current house defaults
    |
    +-- older defaultsVersion?
           |
           +--> migrate house defaults
                    |
                    +-- remove explicitly retired ids (kRetiredIds)
                    +-- add / refresh current house rows
                    +-- save new generation
    |
    v
QML context property: Extensions
    |
    +--> Main.qml
    |      |
    |      +--> TheatreApi.setExtensions(Extensions.installed())
    |      +--> Subtitles.setExtensions(Extensions.installed())
    |      +--> refresh those snapshots whenever Extensions.changed()
    |
    +--> ExtensionsPage.qml
    |
    +--> UniverseExtApi reader:
           Extensions.universePayload(...)
```

`.pragma library` JavaScript cannot reach QML context properties by magic. Where a pure JS module needs the roster,
the shell or page passes it in. That is why `Extensions.installed()` appears at composition boundaries instead of
inside every library.

**The Extensions page:**

```text
ExtensionsPage.qml
    |
    +-- SOURCES
    |     |
    |     +--> ExtensionsSources.qml
    |             |
    |             +--> ExtensionsCatalog.worldsFor()
    |             +--> catalogue / well / universe classification
    |             +--> visible per-world ask order
    |             +--> enable / remove / configure
    |             +--> world-relative reorder
    |
    +-- BROWSE EVERYTHING
    |     |
    |     +--> ExtensionsCatalog.browse()
    |             |
    |             +--> stremio-addons.net community registry
    |             +--> official collection fallback
    |             +--> adult rows rejected at the catalogue-data layer
    |             +--> installable manifest URL selected
    |
    +-- INSTALLED
          |
          +--> current persisted roster
          +--> filtered by selected world
          +--> enable / remove / reorder
```

The Sources pane is intentionally world-agnostic. It shows the whole system at once and derives its sections from
the installed roster. A world with no matching extensions does not get a fake empty section.

Universes are the deliberate exception to the “ask order” metaphor. A universe aggregates an IP; it is not a
catalogue and it does not fetch a playable source. `ExtensionsCatalog.js` detects the `universe` resource first,
puts that entry only in the Universes section, and keeps it out of the ranked fetch chain.

**Install from a link:**

```text
user pastes
  stremio://...
  https://...
  or host/path
      |
      v
Extensions.normalizeUrl()
      |
      +-- stremio://host/... -> https://host/...
      +-- no scheme         -> https://...
      +-- no manifest.json  -> append /manifest.json
      |
      v
Extensions.preview()
      |
      v
GET manifest
  Accept: application/json
  12 s timeout
  no-less-safe redirects
      |
      v
parse object
      |
      +-- missing id/name --------> refuse
      +-- behaviorHints.adult ----> refuse
      |
      v
slimManifest()
      |
      +-- cap description
      +-- reject data: logo
      +-- preserve resources SHAPE
      +-- keep types / idPrefixes
      +-- keep catalogue routing fields
      +-- keep selected behavior hints
      |
      v
preview cache
      |
      v
ExtensionsPage shows what it offers
      |
   user confirms
      |
      v
Extensions.install()
      |
      +-- cached preview exists -> install that copy
      +-- otherwise             -> fetch + validate first
      |
      v
same manifest id already exists?
      |
      +-- yes -> replace manifest/transport in place
      |          KEEP old core flag
      |          KEEP old enabled switch
      |          KEEP array position
      |
      +-- no  -> append enabled, non-core
      |
      v
QSaveFile installed.json
      |
      v
revision++ / changed()
      |
      v
page + shell consumers refresh
```

**A Theatre stream ask:**

```text
Extensions.installed()
      |
      v
AddonClient.streamExtensions(list, type, id)
      |
      +-- enabled only
      +-- manifest claims resource "stream"
      +-- type matches
      +-- idPrefixes match when declared
      |
      v
ordered extension list
      |
      v
AddonClient.loadStreams(...)
      |
      +--> ask extensions IN PARALLEL
      |       but retain stored order as source priority
      |
      +--> per-extension timeout
      +--> partial answers allowed
      +--> normalize stream shapes
      +--> dedupe
      +--> common quality / seeders / language ranking
      |
      v
SourcesSheet / player route
```

The roster still matters when a path carries a historical hardcoded Torrentio fallback. Those paths must ask
`AddonClient.torrentioEnabled()` first. Removing or switching Torrentio off must mean it is actually no longer
asked.

**Catalogue discovery:**

```text
Extensions.installed()
      |
      v
DiscoverApi.js
      |
      +--> derive eligible catalogues from manifests
      +--> derive picker choices
      +--> construct catalogue requests
      +--> use AddonClient's request lane
      |
      v
Discover / Theatre catalogue surfaces
```

`DiscoverApi.js` receives the installed list from its caller because it is a `.pragma library`; it does not own
registry state.

**Universe extensions:**

```text
ExtensionsStore seeded universe entry
      |
      v
ExtensionsCatalog.worldsFor()
      |
      +--> role = universe
      |
      v
Universe UI opens extension id
      |
      v
UniverseExtApi.load(id)
      |
      +--> map extension id -> bundled payload stem
      +--> injected reader calls Extensions.universePayload(stem)
      +--> parse
      +--> validate sections / entries
      +--> drop invalid rows rather than guessing
      +--> process-memory cache
      |
      v
UniverseExtensionPage.qml
```

The current transport is intentionally transitional: the payload shape is designed for a later HTTPS-served
`universe.json`, but at this revision the files are bundled under `assets/universes/` and C++ performs the file
read.

## 3. The files that matter

Full per-file descriptions belong in [`extensions-index.md`](extensions-index.md).

| File | Role |
|---|---|
| `native/engine/ExtensionsStore.h` | authoritative registry contract exposed to QML: ordered installed entries, preview/install, remove, enable, absolute move, URL normalization, universe payload read, revision/signals |
| `native/engine/ExtensionsStore.cpp` | persistence, current house roster and migrations, manifest fetch/slimming/adult gate, install/update behavior, and bundled universe-file transport |
| `qml/ExtensionsPage.qml` | the full Extensions product surface: Sources / Browse everything / Installed, search, install-from-link preview, install result UI, and roster mutations |
| `qml/ExtensionsSources.qml` | world-agnostic source map: derives live sections, catalogue/source grouping, per-world ranks, cross-world ties, enable/remove/configure, and world-relative reordering |
| `qml/ExtensionsCatalog.js` | extension-store data/meaning layer: community browse mapping, adult filtering, world derivation, catalogue/well/universe classification, jobs, and safe world-relative move destinations |
| `qml/AddonClient.js` | generic Stremio-protocol consumer: resource/type/id matching, extension catalogue/meta/stream asks, stream normalization/ranking, and the single Torrentio-enabled truth check |
| `qml/DiscoverApi.js` | extension-derived catalogue picker/request layer; turns installed manifest catalogues into Discover requests |
| `qml/UniverseExtApi.js` | universe-extension payload adapter: id→payload mapping, validation, injected C++ reader, and process cache |
| `qml/AddonLogo.qml` | add-on mark renderer: bundled logo first, manifest logo second, honest letter fallback last |
| `qml/AddonLogos.js` | deterministic id/name→bundled-logo match table |
| `scripts/fetch_addon_logos.py` | **maintenance helper, not an `extensions.paths` member** — one-shot manifest-logo fetcher for bundled assets; the encyclopedia generator does not accept `.py` manifests |
| `assets/addon-logos/` | runtime logo assets consumed by `AddonLogos.js`; data assets, not source-manifest entries |
| `native/main.cpp` | **owned by `shell.paths`** — creates `ExtensionsStore`, exposes it as `Extensions`, and provides extension dev-harness settings |
| `qml/Main.qml` | **owned by `shell.paths`** — pushes the roster into Theatre/Subtitles, refreshes consumers on changes, mounts `ExtensionsPage`, and installs the `UniverseExtApi` reader |
| `qml/BrowserDrawer.qml` | **owned by `shell.paths`** — in-player episode/source browser. Before its lazy series-meta fetch it refreshes TheatreApi from `Extensions.installed()`. That is a consumer boundary, not extension-page ownership |
| `qml/UniverseExtensionPage.qml` | downstream universe renderer. It consumes `UniverseExtApi`; the registry guide owns the protocol/payload seam, not the whole Universe UI |

Do not duplicate shell-owned files into `extensions.paths` merely because they mention `Extensions`. The manifest
boundary is about **who owns the file's behavior**, not who calls whom.

The `extensions.paths` ownership set is:

```text
native/engine/ExtensionsStore.cpp
native/engine/ExtensionsStore.h
qml/AddonClient.js
qml/AddonLogo.qml
qml/AddonLogos.js
qml/DiscoverApi.js
qml/ExtensionsCatalog.js
qml/ExtensionsPage.qml
qml/ExtensionsSources.qml
qml/UniverseExtApi.js
```

## 4. Where state lives

- **`<AppData>/extensions/installed.json` is the registry authority.** There is no Stremio-account sync behind it.
  The file contains the persisted ordered extension array plus the house `defaultsVersion`.

- **Array order is persistent global order.** It is not directly the visible rank in any one world. Tankoban,
  Biblio, and Theatre derive their own well order by filtering the same array.

- **`enabled` belongs to the stored entry.** Consumers are expected to obey it. An extension being present in the
  file is not permission to call it.

- **`core` belongs to the stored entry.** Core rows cannot be removed, disabled, or reordered. The UI should read
  that truth from the installed entry, not infer it from a curated card.

- **The manifest copy is persisted.** Remote installs keep a deliberately slim representation sufficient for
  routing and UI. House entries carry embedded manifests and can be refreshed when the house-default generation
  changes.

- **`defaultsVersion` is migration state.** It records which generation of the house roster the profile has seen.
  It is not the file schema version; `v` and `defaultsVersion` are separate concepts.

- **`ExtensionsStore::m_previewCache` is process memory only.** A successful preview stores the slim manifest by
  normalized transport URL so the confirm click does not immediately fetch it again.

- **`ExtensionsStore::revision` is a wake-up counter, not persisted domain state.** Mutations bump it and emit
  `changed()` so QML rereads `installed()`.

- **`ExtensionsPage.qml` owns transient view state only:** current pane, Installed world tab, search text, Browse
  sort, community rows/loading, pending install URLs, notice copy, and install-sheet preview state.

- **`UniverseExtApi._cache` is process-only.** At this revision bundled universe payloads are read-only, so a valid
  payload is cached forever for that QML engine. Failed/empty validation is not cached, allowing a later retry.

- **`AddonClient.js`, `DiscoverApi.js`, and `ExtensionsCatalog.js` do not own the roster.** They operate on a list
  passed in by a caller. If they grow their own durable installed-extension state, there are now two registries.

- **Bundled logos are repository state, not profile state.** `AddonLogos.js` maps known identities to
  `assets/addon-logos/`; an unknown or failed mark falls back visibly rather than generating a fake brand.

## 5. Traps

1. **Installed is not enabled.** An entry can stay in `installed.json` with `enabled: false`, and protocol consumers
   must filter it before asking.
   **WHY:** if a direct fallback or new caller tests only “is this id present?”, the off-switch becomes cosmetic.
   Torrentio already needed a dedicated `torrentioEnabled()` gate because old hardcoded paths could otherwise keep
   calling a source the user had disabled or removed.

2. **Global array order is NOT one world's ask order.** One stored list contains Theatre-only, Tankoban-only,
   Biblio-only, shared, subtitle, catalogue, and universe entries. Moving the global neighbour of a Tankoban well
   can therefore move an unrelated Biblio row while producing no visible Tankoban change.
   **WHY:** this already happened. `ExtensionsCatalog.moveDestination()` must compute the world-relative swap and
   return an absolute destination; `ExtensionsStore.moveTo()` performs only that absolute move.

3. **Do not reintroduce the retired `move(id, ±1)` contract.** The QML/JS seam now returns `{id, index}` or `null`.
   **WHY:** a prior API change updated the JS unit tests while leaving QML call sites speaking the old numeric
   contract. Every isolated unit test was green and the on-screen arrows were dead. The grep-level page wiring
   contract exists because interface agreement needs its own test.

4. **Manifest `resources` must keep their original shape.** Stremio manifests may declare a resource as a string
   or as `{name, types, idPrefixes}`. `slimManifest()` deliberately preserves that array verbatim.
   **WHY:** `AddonClient.accepts()` gives specific resource objects precedence over manifest-level
   `types/idPrefixes`. Flattening them to strings can make Colosseum ask an extension for content it explicitly
   did not claim.

5. **A community directory URL is not necessarily an install URL.** The community registry can return a human
   page in `url` and the actual manifest in `manifestUrl`; `transportUrl` wins when supplied.
   **WHY:** appending `/manifest.json` to the directory page caused real 404 installs. Keep the mapping rule in
   `ExtensionsCatalog.js`; do not make the C++ URL normalizer guess what a registry response meant.

6. **The adult wall exists in two different ingress paths for a reason.** Community Browse filters NSFW/adult
   rows before they reach the UI; direct manifest preview/install rejects `behaviorHints.adult` in the C++ store.
   **WHY:** protecting only Browse leaves paste-a-link as a bypass. Protecting only installation still advertises
   material the product rule says the store does not carry.

7. **Preview then install intentionally uses cached manifest data.** A successful preview is stored in
   `m_previewCache`; confirmation installs that slim manifest without fetching it again.
   **WHY:** this preserves “install what I just showed you,” but it also means a server-side manifest change
   between preview and confirm is not observed. Do not describe confirmation as a second network validation.

8. **Reinstall-by-id is an UPDATE, not another row.** If a fetched manifest has the same id as an existing entry,
   the store replaces its transport/manifest in place while preserving the old `core`, `enabled`, and position.
   **WHY:** matching on URL alone would allow the same extension identity to appear multiple times after endpoint
   changes, and resetting enabled/order on update would overwrite user decisions.

9. **`core` comes from the stored roster, not from the Browse card.** Curated/community card data is not the
   authority for whether an entry is locked.
   **WHY:** the page has already displayed a removable extension as “built-in” when presentation data was trusted
   over the installed entry.

10. **World membership is derived; do not persist it.** `ExtensionsCatalog.worldsFor()` derives worlds from
    manifest types, except that a `universe` resource wins by role before type derivation.
    **WHY:** storing a second `world` field creates drift as manifests update. A universe is the sharp example: its
    content spans media types, but it belongs to one Universes role, not four duplicated rows sharing one enabled
    switch.

11. **A catalogue and a well are different jobs even when both come from the same site.** The house catalogue
    fills shelves; a well is consulted later to fetch a source/file. Retired WeebCentral/GetComics catalogue rows
    did not imply retiring their wells.
    **WHY:** confusing “what exists in our library/catalogue” with “where can I fetch it?” produces false shelf
    ownership and bad ask order.

12. **`ExtensionsCatalog.js` contains history as well as live page behavior.** Its old curated Discover rail data
    still exists while the page's visible first pane is now the world-agnostic Sources surface.
    **WHY:** reading a constant in the JS file is not proof that a user can currently see that rail. Follow the
    live `ExtensionsPage.qml` visibility/mounting path before documenting UI.

13. **The Sources pane and Installed pane answer different questions.** Sources shows the cross-world system and
    the actual ask chain; Installed applies a selected media-world filter. World tabs deliberately belong only to
    Installed.
    **WHY:** putting Theatre/Tankoban/Biblio tabs back above Sources makes a world-agnostic map look filtered even
    when it is not, recreating the decorative-tab defect the redesign removed.

14. **Universes are not wells.** They declare a `universe` resource, aggregate an IP, fetch no playable source,
    and carry no rank in the Sources ask chain.
    **WHY:** deriving them only from their media `types` would scatter one universe into several worlds and make a
    single enabled flag appear to control several supposedly independent rows.

15. **Universe payload validation is a routing safety boundary.** Invalid entries are dropped; a video needs both
    identity and a valid movie/series type; unknown section kinds vanish; an empty post-validation section vanishes.
    **WHY:** the downstream page routes by section kind. Guessing a missing video type can send a series into a
    movie path and fail somewhere much farther from the corrupt payload.

16. **The universe payload cache has a future expiry problem, not a current one.** Valid payloads are cached for
    the process lifetime because today's source is a bundled read-only JSON file.
    **WHY:** when transport moves to the planned HTTPS `universe.json`, “cache forever” changes from a cheap local
    optimization into stale remote-data policy. Revisit the cache at that migration, not before.

17. **`BrowserDrawer.qml` is an extension consumer but not an extension-owned file.** Its job is the shell/player
    episode/source drawer. It refreshes TheatreApi's extension snapshot before a lazy series-meta lookup and then
    continues through TheatreApi.
    **WHY:** putting it in both `shell.paths` and `extensions.paths` would make two encyclopedia pages responsible
    for keeping one source description current. Cross-reference the boundary; keep one owner.

18. **`native/main.cpp` and `qml/Main.qml` are the same kind of boundary.** Native construction exposes the
    `Extensions` context property; shell QML pushes snapshots into `.pragma` libraries, mounts the page, and wires
    UniverseExtApi.
    **WHY:** the registry cannot consume itself. Composition belongs to Shell even when the thing being composed
    is Extensions.

19. **The house-default migration resurrects a USER-removed current-default row on the next generation bump.**
    There are two different "removal" paths inside `migrateDefaults()` and they are NOT equally protected:
    - **House-retired rows are protected.** `migrateDefaults()` walks `kRetiredIds` and removes any of those ids
      from the profile before appending. A row the *house* deliberately retired stays retired across every future
      generation, because its id is named in `kRetiredIds` and is also absent from the current house-default
      `add(...)` calls.
    - **User-removed current-default rows are NOT protected.** `appendHouseDefaults(onlyMissing=true)` only checks
      `indexOfId(id)`: if the id is present it refreshes the manifest and returns; if the id is **absent** it
      appends a fresh enabled row. A user who deliberately removed a *current* house-default extension (one not in
      `kRetiredIds`) leaves only an absence in `installed.json`, and the next `defaultsVersion` bump will re-append
      that row as a freshly-enabled default.
    The source comment above `appendHouseDefaults()` states the stronger intent ("must never resurrect one the
    user deliberately removed"), but the mechanism as implemented only delivers that promise for house-retired ids.
    **WHY:** treating "removed stays removed" as a universal guarantee hides a real UX regression — a user's
    deliberate removal of a current house row is silently undone the next time the house generation advances. If
    that stronger promise is genuinely required, a persisted "user-removed id" set must be added (and consulted by
    the `add` lambda) before the comment and the behavior agree. Until then, do not encode the stronger promise in
    product behavior, and expect user-removed current-default rows to reappear after a generation bump.

20. **Some extension tests mirror old seed generations by hand.** In particular, the reorder regression fixture
    still describes itself as the generation-3 shipped roster while `ExtensionsStore` is now generation 10
    (`kHouseDefaultsVersion = 10`).
    **WHY:** it remains useful for the algorithmic defect it pins, but it is not a current-roster census. A green
    mirrored fixture does not prove a newly seeded row cannot create a new ordering interaction.

21. **Logo maintenance has two truths: the matcher and the asset.** `AddonLogos.js` can return a perfectly plausible
    path to a file that was renamed or never bundled.
    **WHY:** runtime then falls back to the same letter square used for an intentionally unknown logo, so the
    missing asset is visually easy to misread as expected behavior. The logo contract test checks matcher→disk,
    not just matcher output.

22. **Do not put `scripts/fetch_addon_logos.py` or image assets into `extensions.paths`.** They matter to the feature,
    but `code_encyclopedia.py` accepts only C/C++ headers/sources, QML, and JS.
    **WHY:** the manifest is an input contract for the generator, not a generic “everything related” inventory.
    Keep non-supported maintenance/data dependencies visible in this page instead of making the verification
    script fail on them.

## 6. How to test it

The current extension coverage is mostly **contract tests plus deliberate live smokes**. They answer different
questions.

**World derivation and cross-world isolation:**

```bash
node tests/extension_worlds_derivation_test.mjs
node tests/extension_world_isolation_test.mjs
```

These pin the two easiest architectural regressions:

- manifest capability decides which world may ask a source;
- a Theatre request must not suddenly query Tankoban/Biblio wells just because all of them use the Stremio
  `stream` resource.

**World-relative reordering and QML↔JS wiring:**

```bash
node tests/extension_reorder_world_test.mjs
node tests/extension_page_wiring_contract.mjs
```

The first exercises the reorder algorithm. The second deliberately reads the real QML and JS source to ensure the
page still speaks the current `{id,index}`/`moveTo()` contract.

Do not treat the reorder fixture as a current roster census; review it when the seeded house roster changes.

**Page rules:**

```bash
node tests/extension_panes_and_search_test.mjs
```

This pins the current Sources / Browse / Installed model, including the rule that world tabs belong to Installed
alone and search stays in the pane the user is already on.

**Community Browse install-address mapping:**

```bash
node tests/extensions_catalog_test.mjs
```

This protects the `transportUrl → manifestUrl → url` preference that prevents community directory-page 404
installs.

**Torrentio removal/off-switch honesty:**

```bash
node tests/addon_torrentio_honesty_test.mjs
```

Run this when touching any historical Torrentio fallback. The important behavior is not “Torrentio is seeded”; it
is “once the roster says no, every fallback also says no.”

**Universe payload contract:**

```bash
node tests/universe_ext_api_test.mjs
```

This checks the validator, role payload mapping, reader seam, failure behavior, ordering, and cache semantics
without needing a real remote universe service.

**Bundled logos:**

```bash
node tests/addon_logos_house_wells_test.mjs
```

This loads the real matcher and checks that its referenced assets actually exist.

**Load the real Extensions page:**

From a Windows developer shell using the normal built app:

```bat
set "COLOSSEUM_OPEN_EXTENSIONS=1"
dev.bat
```

That uses the shell's dedicated dev door to activate the lazy `ExtensionsPage.qml` Loader at boot. The point is to
catch creation/wiring failures that a JS contract test cannot.

Manually check at least:

- Sources renders the installed roster across all real worlds;
- universes appear in their own section and do not receive ranks;
- Installed world tabs actually filter;
- disabling a removable source changes the roster and survives restart;
- moving a shared well changes the intended world's rank without silently corrupting another;
- core catalogue rows cannot be toggled, removed, or ranked;
- remove confirmation removes only the selected non-core row;
- paste-a-link shows a manifest preview before install;
- an adult-flagged manifest is refused;
- install/update persists after restart;
- bundled marks render, with an honest letter fallback for an unknown add-on.

**Live multi-extension stream ask:**

```bat
set "COLOSSEUM_STREAMS_SELFTEST=movie|tt0816692"
dev.bat
```

This drives the production `Extensions.installed() → AddonClient.streamExtensions() → loadStreams()` path and logs
which extensions were asked and what rows arrived.

It is network-dependent. Empty/changed answers can mean the remote add-on changed, not necessarily that the local
matching code regressed.

**Live extension-catalogue contribution:**

```bat
set "COLOSSEUM_CATALOG_SELFTEST=movies"
dev.bat
```

This exercises the shell's production catalogue path and logs the house plus extension-contributed rows.

Again: this is a live network smoke, not a deterministic unit test.

### What the source/contract tests do not prove

- that `ExtensionsStore` can currently fetch and persist a real third-party manifest on the target machine;
- that the currently seeded generation migrates every historical profile correctly;
- that a deliberately removed house row remains removed across a **future** defaults-version bump (see trap 19 —
  house-retired rows do; user-removed current-default rows do not);
- that every community registry endpoint or installed add-on is alive today;
- that every consumer of the roster refreshes at the correct moment after a mutation;
- that a remote add-on's current manifest still matches the persisted slim copy;
- that all downstream Theatre/Tankoban/Biblio behavior respects an extension's off/remove state;
- that future HTTPS universe payloads have a correct cache invalidation policy.

Those require Brotherhood's live/profile ground-truth pass, not stronger prose here.

## Keeping this page honest

```bash
# refresh the generated index after changing a covered source file
python scripts/code_encyclopedia.py --paths docs/encyclopedia/extensions.paths \
  --output docs/encyclopedia/extensions-index.md --state docs/encyclopedia/extensions-state.json

# gate: fails when a covered file changed since its description was accepted
python scripts/code_encyclopedia.py --paths docs/encyclopedia/extensions.paths \
  --output docs/encyclopedia/extensions-index.md --state docs/encyclopedia/extensions-state.json --check

# after reviewing a changed description, ratify that file
python scripts/code_encyclopedia.py --paths docs/encyclopedia/extensions.paths \
  --output docs/encyclopedia/extensions-index.md --state docs/encyclopedia/extensions-state.json --accept <path>
```

When changing this subsystem: `BrowserDrawer.qml`, `Main.qml`, and `native/main.cpp` remain Shell-owned even
though they are real extension integration boundaries. Do not solve cross-guide relationships by giving the same
file two owners. If the house-roster migration changes (trap 19), re-derive the two removal paths from
`migrateDefaults()` + `appendHouseDefaults()` + `kRetiredIds` before editing this page.
