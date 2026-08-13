# The Vault (local media — scan, index, identify, browse, launch) — subsystem guide

> **Hand-written. Keep it true.** If you change how the Vault scans, identifies, projects the
> Browse face, or launches a local file, update this file in the same commit. The per-file index
> beside it ([`vault-index.md`](vault-index.md)) is generated — never edit that one.
>
> Verified against `master` (2026-08-13 state — the Browse-face overhaul, execution plan
> `docs/superpowers/plans/2026-08-12-colosseum-vault-browse-face-plan.md`, is the current face).
> Related guides: [`comics.md`](comics.md) (comic reading, shares `CbzArchive`/`image://comiccover/`
> with the Vault's comic path), [`biblio.md`](biblio.md) (the Reader 2 EPUB reader the Vault hands
> off to for books), [`player.md`](player.md) (the player the Vault hands off to for video).

## 1. What this subsystem is for

The Vault is Colosseum's **"On this machine"** world: point it at folders/drives you already own,
and it tells you honestly what is physically there — never pretending to know more than the file
allows. It never streams and never fetches remote artwork; V1 art is only comic covers, local
folder-artwork companions, and a typographic fallback. The Vault is entered from the **taskbar
door**, not the Tankoban/Biblio/Theatre mode bar — it is not a fourth mode (`project_three_modes`
memory fact).

Two experience halves ship today: **Browse** (this doc's main subject — the folder-true,
media-faced grid the user actually looks at) and the founding **scan/confirm/identify** machinery
that feeds it. **Manage** (the parent design's six areas, ownership graph, DLNA) is out of scope —
not built.

## 2. The flow

**Intake — a folder becomes shelvable:**

```
VaultPage.qml (drop surface / rail "Add storage" / addFolderRequested)
  → VaultLibrary.addFolder(path)
      → VaultScanner.scanRoot()  (off-thread census via VaultKit::census/groupByFirstLevelSubdir)
      → VaultLibrary.scanFinished → candidate raised → VaultConfirmCard.qml (the founding card)
  → user reassigns chips (kind overrides) → VaultConfirmCard.shelveRequested
  → VaultLibrary.confirmRoot() → VaultConfig.confirmRoot() + VaultScanner.publishConfirmed()
      → VaultIndex.publish()  (ONE transaction, the UNION of every confirmed root)
```

**Live arrivals — a file already inside a confirmed root:**

```
QFileSystemWatcher (VaultWatcher, per confirmed root)
  → debounced VaultWatcher::processRoot()  (same VaultKit walk laws as the census)
  → VaultIndex::upsertMany()  (incremental, no full republish) → VaultLibrary.liveArrival (door pulse)
  → a law-breaking new-kind file raises a one-slice VaultConfirmCard instead of silently shelving
```

**Browse — what the user actually looks at (this plan's face):**

```
VaultPage.qml (taskbar door → vaultPage)
  → FeaturedCarousel (recentArrivals(6))  →  VaultBrowseRail (rootsDetail())  →
    VaultBrowseCrumb (crumbStack)  →  GridView("vaultBrowseGrid") over VaultLibrary.browseAt(path)
       delegate: VaultPosterCard (2:3, folder/show/season/film) or VaultWideCard (16:9, episode/clip)
  → click a container (folder/show/season) → pushCrumb → drills one level (VaultKit::planBrowseLevel)
  → click a Film → VaultDetailSheet (VaultLibrary.browseDetail(key)) → Play → openMediaRequested(path)
  → click an episode/clip → openMediaRequested(path) directly (no sheet)
```

`browseAt()`/`browseDetail()`/`rootsDetail()`/`recentArrivals()`/`browseEmptyCause()` are the
**whole read surface** QML paints from — see §3's VaultKit/VaultLibrary rows below for the
projection spine itself (`VaultKit::planBrowseLevel` does the pure filesystem-structural
classification; `VaultLibrary` decorates it with index facts: identity state, away, coverRef).

**Identify-in-place and launch:**

```
uncertain tile's "?" mark → VaultIdentifyDialog (seeded, offline candidates) → identifyGroupWith()
  → VaultIdentifier.applyGroup/identifyGroupWith (decorate-only write) → VaultIndex row updated
  → next browseAt() re-projects the SAME delegate to "identified" (VaultPage's syncGridModel, §5.3)

Play (grid card / sheet / Open Recent / Open Media control)
  → win.openLocalMedia([path])  (Main.qml — Vault's OWN launch router, app-wide adoption per
    project_local_media_is_agent0_turf memory fact)
  → LocalLaunch classifies by extension, backend-validates (CbzArchive / MediaAdmissionProbe /
    extension for books) → routes to comicreader / player / Reader2, or rejects with a category
```

## 3. The files that matter

| File | Role |
|---|---|
| `native/engine/VaultKit.{h,cpp}` | the Vault's **pure-logic kit**, no QObject: the census classifier, the SxxExx + absolute-numbering episode grammar, the media-folder title cleaner, and (browse-face plan) **`planBrowseLevel`** — the browse-collapse planner — and `describeFilmFolder` (companions/extras for the detail sheet) |
| `native/engine/VaultIndex.{h,cpp}` | the rebuildable scan product: SQLite at `<appdata>/vault/index-v1.sqlite`, transactional `publish()` (whole-index replace) + `upsert()`/`upsertMany()` (live arrivals), natural-order sort key, `rowsForIdentity()` (cross-root "same film, N copies" join) |
| `native/engine/VaultConfig.{h,cpp}` | user intent: roots (+confirmed/synthetic/hidden flags), per-subtree kind overrides, `scanIgnore` needles — never written by the scanner |
| `native/engine/VaultIdentity.{h,cpp}` | content-addressed file identity (`vault:`+SHA1 of `path::size::mtimeMs`) so progress survives rename/move; `reconcile()` migrates unambiguous renames, parks ambiguous ones |
| `native/engine/VaultScanner.{h,cpp}` | the cancellable off-thread census: `buildScan()` (pure, sync) on a pool thread, `applyResult()`/`applyPublish()` (thread-affine identity+index commit) back on the GUI thread |
| `native/engine/VaultWatcher.{h,cpp}` | the live-shelf engine: one `QFileSystemWatcher` per confirmed root, debounced `processRoot()`, the immersive gate (no upserts while a reader/player is open) |
| `native/engine/VaultEnricher.{h,cpp}` | fills honest facts post-census: comic page count + cover entry (via `CbzArchive`), EPUB OPF metadata, ffprobe video duration (cached), and (Slice 3) **local artwork adoption by filename convention only** (`findLocalArtwork`) |
| `native/engine/VaultIdentifier.{h,cpp}` | the certainty gate: `matchGroup()` auto-adopts ONLY on exactly-one catalogue candidate; `recordAmbiguous()` makes "Vault isn't sure" a durable fact instead of looking merely unscanned |
| `native/engine/VaultLibrary.{h,cpp}` | **the one QML façade** — the read-model (`browseAt`/`browseDetail`/`rootsDetail`/`recentArrivals`/`browseEmptyCause`/`browseEmptyAwayCount`/`series`/`items`) plus every command (`addFolder`, `confirmRoot`, `identifyGroup*`, `hideGroup`, `revealInExplorer`, …); C++ owns every multi-step sequence so QML only paints and fires gestures |
| `native/engine/VaultBrowseAway.{h,cpp}` | pulled out of `VaultLibrary` (Slice 6) so a Qt Test drives it without the full façade: which confirmed root owns a browse path, whether it's away, and `offlineBrowseAt()` — the durable-index fallback when a level's own directory can no longer be walked |
| `native/engine/VaultBrowseDetail.{h,cpp}` | pulled out the same way (Slice 7): `detailFor()` — the detail sheet's ONE projection (copies/companions/extras/evidence), composing `VaultKit::describeFilmFolder` with `VaultIndex::rowsForIdentity` |
| `native/engine/VaultBrowseEmpty.{h,cpp}` | pulled out the same way (Slice 9): pure classification of which of the four empty causes applies — `noRoots` / `emptyFolder` / `allAway` (`filtered` is named but never produced — no filter control ships) |
| `native/engine/VaultRecent.{h,cpp}` | the Open Media "recent files" shortcut list (path+title+kind+id only — never progress); dedup by normalized path, capped at 12 |
| `native/engine/VaultDownloadsRoot.{h,cpp}` | derives the synthetic, pre-confirmed "Downloads" root's rows from the four download backbones via `QMetaObject::invokeMethod` — reads only, never edits the Downloads lane |
| `native/engine/VaultPageStore.{h,cpp}` | the comic-reader adapter: a Vault CBZ opens in ComicReader 2 with zero reader edits (same descriptor shape as `MangaVolumeIndex`) |
| `native/engine/VaultBookCoverProvider.{h,cpp}` | `image://vaultbookcover/<archive>/<entry>` — stateless EPUB cover decode, same id-encoding convention as `image://comiccover/` |
| `native/engine/VaultBookStateMigrator.{h,cpp}` | copies Reader 2's path-keyed stores forward after an unambiguous VaultIdentity rename; never edits Reader 2 sources |
| `native/engine/VaultStoreIo.h` | shared header-only atomic JSON write/read-with-`.bak`-fallback discipline both `VaultConfig` and `VaultIdentity` use |
| `qml/VaultPage.qml` | the host page: read-model bindings, the Browse-face state machine (`crumbStack`/`currentBrowsePath`/`gridModel` sync), empty/away wiring, and (still shipped, pre-Browse) the drop surface + confirm card + folder view assembly |
| `qml/VaultBrowseRail.qml` | the collapsible root rail (`vaultBrowseRail`) — collapsed-by-default glyph+dot, expands to name+counts; also carries Add-storage and the reversible Hidden shelf |
| `qml/VaultBrowseCrumb.qml` | the breadcrumb (`vaultBrowseCrumb`) — middle-segment collapse past 4 levels, first/last always visible |
| `qml/VaultPosterCard.qml` | the 2:3 card (folder/show/season/film) — the full state wardrobe: resolving/identified/uncertain/away/localOnly/no-art |
| `qml/VaultWideCard.qml` | the 16:9 sibling (episode/clip) — same state contract, different aspect + fact-line grammar |
| `qml/VaultDetailSheet.qml` | the film detail sheet (`vaultBrowseSheet`) — copies/companions/extras/evidence/Play; same-window overlay (never a `Window`/`Popup`, so the Lanista bridge can see it) |
| `qml/VaultBrowseEmpty.qml` | the four-cause empty-state family (`vaultBrowseGridEmpty`), copy keyed strictly off the C++-computed cause |
| `qml/VaultDoor.qml` | the taskbar door itself — idle/scanning/arrival pulse states |
| `qml/VaultConfirmCard.qml` | the founding ceremony's one card — per-subtree kind chips + the honest leftover line |
| `qml/VaultFolderView.qml` | the pre-Browse folder pane (sort, synopsis, reveal, live progress join) — still shipped underneath the confirm/hidden flows |
| `qml/VaultTile.qml` | the shared shelf/folder tile — keeps an away root's items in the gallery instead of erasing them |
| `qml/VaultIdentifyDialog.qml` / `VaultIdentityCeremonyDialog.qml` | the explicit identify gesture and the shared rename/move ceremony surface (launch sessions + Vault rows both use the ceremony dialog) |
| `qml/VaultApi.js` | pure derivation joining index rows against live Progress — **no kind translation**, deliberately (fix the writer, never add a translation map here) |
| `qml/VaultBrowseState.js` | in-process (not persisted) per-path scroll memory for the Browse grid, surviving the Loader destroy/recreate that happens every time you leave and re-enter Vault |

**Not owned here (shared house chrome, cross-referenced):** `qml/FeaturedCarousel.qml` +
`qml/CarouselSlide.qml` — the Browse face reuses them verbatim for its "recently arrived" head,
with two translations only (design §4.10): the blurb slot carries the physical fact instead of a
tagline, and the gradient stays neutral house tokens instead of a per-slide colour.

## 4. Where state lives

- **Index (rebuildable) — `<appdata>/vault/index-v1.sqlite`**, owned by `VaultIndex`. A full
  `publish()` replaces the whole index in one transaction (a cancelled/errored publish rolls back,
  previous contents intact); `upsert()`/`upsertMany()` land live arrivals without a full
  republish. This file can be deleted and rebuilt from disk at any time — it is a product, never
  the user's decisions.
- **Config (user intent) — `<appdata>/vault/config.json` (+`.bak`)**, owned by `VaultConfig` via
  `VaultStoreIo`: roots, confirmed/synthetic/hidden flags, kind overrides, `scanIgnore`.
- **Identity — `<appdata>/vault/identity.json` (+`.bak`)**, owned by `VaultIdentity`: the
  content-addressed id registry + path aliases from reconciled renames.
- **Open Recent — `<appdata>/vault/open-recent.json` (+`.bak`)**, owned by `VaultRecent`.
- **Session-only, in-process (not on disk):** `VaultBrowseState.js`'s per-path scroll positions —
  resets on app restart by design.
- **Registry-backed, restart-durable:** current Browse folder (whole crumb trail) + rail
  expanded/collapsed, in `VaultPage.qml`'s `Settings { category: "vaultBrowseV1" }` (same
  tag-isolated backend every other Colosseum store uses under `COLOSSEUM_APPDATA_TAG`).
- **What does NOT persist a second time:** reading/watch progress — that stays in Biblio's
  `BookStores` / the player's own progress store, joined by `VaultIdentity`'s id, never duplicated
  into a Vault-owned progress file.

## 5. Traps

1. **A GridView's `highlightItem` is null until something is current — `verify(!x || !x.prop)` is
   vacuous the moment `x` can be null.** `tests/qml/tst_vault_browse_page.qml`'s
   `test_focus_ring_visible_on_keyboard_focus_only` asserted exactly this shape at two points
   where `grid.currentIndex` was still -1, so the null short-circuited past `.visible` and the
   check never ran. Proven by mutation: hard-coding the shipped highlight's `visible: true` (a
   real design §4.9 violation) still left the suite green. Fix: force a current item
   (`grid.currentIndex = 0`) *without* granting keyboard focus so `highlightItem` genuinely exists
   before its `visible` property is read — assert existence, THEN the property, always in that
   order. This is the **second** vacuous test this plan surfaced (`VaultBrowseDetail`'s
   `spiderManFixtureOneCopyTwoCompanionsTwoJunk` extras-miscount was the first, §5.7 below) —
   treat any bare `!x || !x.prop` guard in this subsystem's tests as a claim to re-derive, not
   trust.
2. **A Film node's `path` must be the FILE, never its containing folder.** `BrowseNode::path` for
   a film starts life as the containing directory (`VaultKit::planBrowseLevel` never overrides
   it); `VaultLibrary::browseAt()` (VaultLibrary.cpp, the Film branch) rewrites it to
   `rows.first().path` — the group's one real video file, via VaultScanner's own "one video file,
   one group" convention — specifically because `openMediaRequested`/Play expects a file, not a
   directory. Any new code path that reads a Film row's `path` before this rewrite runs will hand
   Play a folder.
3. **Folder/Show/Season/Episode nodes must report `state: "identified"`, never the
   "resolving" default, or the grid cannot be drilled or played at all.** `VaultPosterCard`/
   `VaultWideCard` only mount their open `MouseArea` once `faceState` flips off `"resolving"` to
   `"settled"` (design §4.6's own resolve-in-place contract). Container nodes have no per-catalogue
   identification pending — their title is already structurally known from the filesystem walk —
   so leaving them at Slice 1's honest-default `"resolving"` (correct for a Film, which genuinely
   waits on a catalogue match) silently made every folder/show/season/episode tile unclickable.
   Fixed in `VaultLibrary::browseAt()`; Film nodes are the ONLY node type whose state is a real
   catalogue-identification question.
4. **Extras/Featurettes rows share the SAME index group as their film — an unfiltered "all rows in
   this group are copies" read overcounts.** `VaultScanner::groupByFirstLevelSubdir` nests
   everything under a film's folder, trailers included, into one group/subtree. `VaultBrowseDetail`
   must fold out any row whose `subfolder` is an Extras/Featurettes dir
   (`VaultKit::isExtrasDirName`) before joining "copies you hold" — this was a real bug (found via
   the Lanista replay, not the original hand-authored fixtures, which only ever seeded one row per
   group) fixed by `VaultKit::isExtrasDirName`, now exposed specifically so this join can use it.
5. **The hit area cannot live inside a `settled`-only layer, or a resolving tile is unclickable —
   contradicting the design's own "remains fully interactive" contract (§4.6).** `VaultPosterCard`'s
   `settledLayer` is `visible: opacity > 0`, and an invisible item receives no mouse events. The
   card's `MouseArea` (`..._hitArea`) is a **sibling** of `settledLayer`, not nested inside it —
   found live driving a real Play click on a still-resolving Film card whose identity would never
   resolve without a live catalogue lookup (permanently unclickable otherwise). Hover chrome (the
   dim scrim + play glyph) stays settled-only on purpose; only the click itself must survive both
   faces.
6. **`GridView.model` cannot bind straight to `browseAt()`'s return value — every navigation
   destroys and recreates every delegate, and the resolve-in-place crossfade never gets a "before"
   frame to animate from.** `browseGridRows` is a fresh array on every recompute (a publish, a root
   going away, an identify settling). `VaultPage.qml`'s `gridModel` (a `ListModel`) is the one
   stable thing the `GridView` binds to; `syncGridModel()` diffs by row `key` and calls
   `ListModel.set()` in place when the key set is unchanged (same delegate Item, new `row` data —
   the `settledOpacity` `Behavior` gets its crossfade) versus `clear()`+`append()` only on a
   genuine structural change (different folder, different row set, the Hidden-view toggle).
7. **A never-scanned away root reads as `emptyFolder`, not `allAway`, if you check the away flag
   alone.** `VaultIndex::markRootAway()` only ever flips rows that already exist; a root that
   vanished before it was ever scanned has no row to carry that flag. `VaultBrowseEmpty::
   isLevelAway` combines the index's away flag with a live `QDir::exists()` check
   (`hasOwnerRoot`/`ownerDirectoryExists`) — found live driving the all-away-empty Lanista fixture.
8. **The four empty-state causes must never share copy — and "filtered" is real code with no live
   trigger.** `VaultBrowseEmpty.qml` renders all four (design §4.5's own requirement — only one of
   the four is actually a problem), but `VaultLibrary::browseEmptyCause()` never returns
   `"filtered"`: no filter control has shipped on the Browse face. Do not wire a fake trigger for
   it to "complete" the family — that is inventing product surface the plan explicitly deferred.
9. **`browseAt()` needs an offline fallback, or a vanished drive's level reads as EMPTY instead of
   AWAY.** `VaultKit::planBrowseLevel` cannot walk a directory that no longer exists — it bails at
   `QDir::exists()`. `VaultLibrary::browseAt()` detects "no nodes AND the level's root is away" and
   falls back to `VaultBrowseAway::offlineBrowseAt()`, which re-derives one row per group from the
   durable index instead (a structural simplification: multi-file groups fold to a plain folder
   tile, not a reconstructed show/season, since re-deriving that fidelity offline is the parent
   ownership arc's business). Without this the design's own "nothing disappears" contract (§4.7)
   would be false the instant a level's own root goes away.
10. **`identityState` is intentionally NOT carried across a `publish()`.** Unlike
    `admissionVerdict` (carried across a destructive publish when the `(id,size,mtimeMs)` tuple is
    unchanged), `FileRow::identityState`/`identityCandidateCount` reset to `""` on every rescan —
    deliberately (the identity-carry hazard is a different arc's business per the plan's own
    ground-truth pin). `VaultIdentifier`'s auto-identify pass, already scheduled on every index
    change, re-derives it. Do not "fix" this reset without reading the plan's Recon Gate 9 note
    first.
11. **The Quick Test harness for the Browse page does not — and cannot — instantiate `VaultPage.qml`
    itself.** `VaultPage.qml` reads the real `VaultLibrary` C++ singleton throughout
    (`typeof VaultLibrary !== "undefined"` guards), which the Quick Test engine never registers.
    `tests/qml/tst_vault_browse_page.qml` is a hand-maintained **local replica** of `VaultPage`'s
    navigation state machine (`pushCrumb`/`goToCrumb`/`handleBrowseCardOpen`/`syncGridModel`),
    wired through the SAME production sub-components (`VaultPosterCard`, `VaultWideCard`,
    `VaultBrowseRail`, `VaultBrowseCrumb`, `VaultDetailSheet`, `FeaturedCarousel`). A change to
    `VaultPage.qml`'s own navigation logic must be mirrored into the harness by hand, or a passing
    Quick Test proves nothing about the shipped page — the assembled-page proof is the Lanista
    replay (`vault_browse_smoke.json`), never this layer alone.
12. **No `.ps1` gate exists for the Vault or the Browse face.** Unlike some other Colosseum
    subsystems, the Vault's deterministic proof is entirely `ctest` (`-L unit` for the Qt Test
    family + `colosseum.qml` for the Quick Test family) — do not go looking for a
    `test_vault_*.ps1`; none is registered. Runtime proof is Lanista scenario replay under
    `tests/lanista_scenarios/vault_*.json`.

## 6. How to test it

- **Fast native gate:** `ctest --test-dir native/build-msvc -L unit --output-on-failure` runs the
  Vault's whole Qt Test family: `colosseum.qttest.vault_kit` (the collapse planner + grammar +
  census, pure), `vault_stores` (VaultConfig/VaultIdentity round-trip + `.bak` recovery),
  `vault_index` (publish/upsert/natural order), `vault_scanner` (async census + generation guard),
  `vault_enricher` (comic/EPUB facts + local artwork adoption), `vault_browse_away`,
  `vault_browse_empty`, `vault_browse_detail`, `vault_downloads_root`, `vault_identifier`,
  `vault_mal_match`, `vault_imdb_match`. Plus two C++ harnesses outside the `qttest` family:
  `colosseum.vault_admission_probe_harness` (real headless libmpv decode-frame admission) and
  `colosseum.vault_launch_router_harness` (extension→backend routing + `VaultPageStore`).
- **Quick Test gate:** `ctest --test-dir native/build-msvc -R colosseum.qml --output-on-failure`
  runs, among others, `VaultBrowsePage` (the page-level navigation contract — see trap #11),
  `VaultCards`, `VaultDoor`, `VaultFolderView`, `VaultHome`, `VaultIdentifyDialog`,
  `VaultIdentityDialogs`, `VaultRecentFilter`, `VaultTileMenu`, `VaultConfirmCard`,
  `VaultAdmissionApi`.
- **Runtime/Lanista:** `tests/lanista_scenarios/vault_*.json` — `vault_browse_smoke`,
  `vault_browse_resolve`/`_away`/`_uncertain`/`_churn`, `vault_browse_empty_folder`/
  `_allaway_empty`/`_no_storage`, `vault_launch_smoke`, `vault_launch_baseline`, `vault_open_recent`,
  `vault_identify`, `vault_door`, `vault_shelves`. Vault fixtures live in Roaming
  (`--seed` reaches only Local — a ledger-documented workaround), pre-placed per scenario. Full
  detail in `docs/colosseum-lanista-verification.md`.
- **What it cannot cover:** the crossfade's actual *feel*, artwork rendering fidelity, and any
  aesthetic verdict against the mock — those stay eyes-on, Hemanth's screen, never a suite.

## Keeping this page honest

```bash
# refresh the index after editing any source comment
python scripts/code_encyclopedia.py --paths docs/encyclopedia/vault.paths \
  --output docs/encyclopedia/vault-index.md --state docs/encyclopedia/vault-state.json

# gate: fails if a file changed since its description was accepted
python scripts/code_encyclopedia.py ... --check

# after reviewing a changed comment, ratify it
python scripts/code_encyclopedia.py ... --accept <path>
```
