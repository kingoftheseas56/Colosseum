# Catalogs — subsystem guide

> **Hand-written. Keep it true.** If you change how Colosseum bakes, ships, queries, or falls back from its
> offline metadata catalogues, update this file in the same commit. The per-file index beside it
> ([`catalogs-index.md`](catalogs-index.md)) is generated — never edit that one.
>
> Drafted via Preflight-Architect, ground-truthed and adopted by the ZCode-seat session, 2026-08-09.
> Source-read against `master@157986433732c9907e159fa9199dda666b5f6866` (= HEAD at adoption). Ground-truth pass
> verified the cross-world asymmetry (ImdbCatalog consumers are Theatre-only; MalCatalog consumers span Theatre
> and Tankoban), the bake specifics (`KEEP_TOP_BY_MEMBERS = 20000`, SFW-anime / explicit-manga policy), the
> harness-registration claim (built in `native/CMakeLists.txt` but no `add_test`, so CTest does not invoke them),
> and the `notGenre` header-comment drift (trap 10). The deployment section was rewritten from a real read of
> `scripts/data_vault/`; that read surfaced a genuine gap — `imdb_catalog.db` is not in either vault script's
> artifact list (trap 24). Adopt as the current truth; re-verify on the next bake or vault change.

## 1. What this subsystem is for

Give Colosseum **large, deterministic metadata questions that can be answered instantly and without depending on
a live API**, while keeping the online services underneath as fallback or freshness lanes where the product
actually has one.

There are two stores in this guide, and they are deliberately asymmetric:

- `ImdbCatalog` is **Theatre-only**. It gives Movies and Shows the facts the keyless live sources do not reliably
  carry: real IMDb vote counts, original-language evidence, true miniseries typing, episode counts, and a separate
  anime identity bit. Theatre uses those facts to build deep catalogue shelves and to filter live rows honestly.

- `MalCatalog` is **cross-world**. The same baked MyAnimeList artifact serves:
  - Theatre anime shelves and anime genre pages;
  - Tankoban manga genre pages;
  - Tankoban's paged manga Discover wall and its genre/demographic facets.

Do not flatten that into “both catalogues are Theatre databases.” `mal_catalog.db` contains both `anime` and
`manga`, and its public C++ contract has separate APIs for both jobs.

The full lifecycle is:

```text id="rqx7at"
public bulk dataset
      |
      v
build-time Python baker
      |
      v
gitignored data/*.db
      |
      v
release / catalogue deployment
      |
      v
read-only SQLite store in the app
      |
      v
small, allowlisted QML-facing queries
      |
      +--> baked answer
      |
      +--> live fallback / live-only shelf / live enrichment
           depending on the consumer
```

The database is a **build artifact, not a user database**. Runtime code does not download IMDb TSVs or the MAL
Kaggle dump, does not migrate these databases in place, and does not write user state into them.

There is a third store with the same broad architectural doctrine:
`native/engine/ComicsCatalog.cpp/.h`. It is already owned by [`comics.md`](comics.md) / `comics.paths`.
This page cross-references that sibling pattern only. **Do not add `ComicsCatalog.cpp/.h` to `catalogs.paths`.**

Likewise, the writable `BiblioCatalog` is not part of this family. It is a per-user AppData cache refreshed from
live providers. The stores here are prebuilt, pipeline-deployed, read-only artefacts.

## 2. The flow

**IMDb bake — public dumps to the Theatre index:**

```text id="vbylzq"
datasets.imdbws.com
  |
  +-- title.ratings.tsv.gz
  +-- title.basics.tsv.gz
  +-- title.episode.tsv.gz
  +-- title.akas.tsv.gz
  |
  +-- anime identity cross-reference
  |
  v
scripts/theatre_brain/build_imdb_db.py
  |
  +-- keep movie / tvSeries / tvMiniSeries only
  +-- exclude IMDb adult rows
  +-- require >= 1000 votes
  +-- derive original language conservatively
  +-- count episodes
  +-- classify anime from the identity cross-reference,
  |      NOT from language
  |
  v
data/imdb_catalog.db.building
  |
  +-- title
  +-- genre
  +-- meta
  +-- query indexes
  |
  v
data/imdb_catalog.db
  |
  v
publish_release.py  (NOTE: see trap 24 — IMDb is not currently in the artifact list)
  |
  v
<installed Colosseum>/data/imdb_catalog.db
  |
  v
ImdbCatalog
  |
  +-- titleCatalog(allowlisted query, offset, limit)
  +-- titleFacts([tt...])
  |
  v
Theatre
```

The builder exists because the live Theatre sources do not carry enough stable information for shelves such as
real vote-band Hidden Gems, original-language shelves, true miniseries separation, or reliable anime exclusion.

`ImdbCatalog` does not expose arbitrary SQL. QML gives it a small query map; C++ checks every key, converts each
ordering choice to a fixed SQL fragment, binds every caller-provided value, clamps paging, and returns normalized
maps.

**MAL bake — one artifact, two worlds:**

```text id="ps5ohe"
weekly public MyAnimeList Kaggle dump
        |
        +-- anime.csv
        +-- manga.csv
        |
        v
scripts/anime_brain/build_mal_db.py
        |
        +-- normalize MAL/Jikan-shaped type + status fields
        +-- normalize studios / authors
        +-- flatten genre + theme + demographic tags
        |
        +-- ANIME:
        |     keep SFW rows only
        |
        +-- MANGA:
        |     retain explicit rows
        |     mark explicit=1
        |     retain genre / demographic classifications
        |
        +-- compute TRUE tag/classification totals
        |     BEFORE the browsable slice
        |
        +-- keep top 20,000 rows per medium by members
        |
        v
data/mal_catalog.db.building
        |
        +-- anime
        +-- manga
        +-- tag / tag_count
        +-- classification / classification_count
        +-- meta
        +-- Theatre + Tankoban query indexes
        |
        v
data/mal_catalog.db
        |
        v
publish_release.py  ->  Colosseum-Data vault release (data-YYYY-MM-DD)
        |
        v
pull_data.py  ->  <installed Colosseum>/data/mal_catalog.db
        |
        v
MalCatalog
        |
        +---------------------------+-------------------------------+
        |                           |                               |
        v                           v                               v
genreEntries()               animeCatalog()                  discoverPage()
genreCount()                                                  discoverFilters()
        |                           |                               |
        v                           v                               v
Theatre anime +              Theatre deep anime              Tankoban manga
Tankoban manga               catalogue                       Discover
genre pages
```

**Runtime construction:**

```text id="hw5tmz"
native/main.cpp
    |
    +--> MalCatalog("data/mal_catalog.db")
    |       -> QML context property "MalCatalog"
    |
    +--> ImdbCatalog("data/imdb_catalog.db")
            -> QML context property "ImdbCatalog"
```

Each store tries the requested `data/...` path first. If that path is not visible from the current working
directory, it also tries the same path two directories above the executable. That second shape matches the
repository-style installed layout where the executable lives under `native/build-msvc/` and the catalogue lives
at the package root under `data/`.

Both connections use SQLite's read-only mode.

If the file does not exist or cannot be opened:

```text id="rvjbbe"
ready() == false
queries return empty
```

The C++ stores do not pop an error page or start a network fetch themselves. **The caller decides what “empty”
means.**

That distinction matters because the fallback is not identical on every surface.

**MAL genre pages — real baked-first fallback ladder:**

```text id="miuyyr"
TheatreGenrePage / GenrePage
      |
      | page has QML context access
      v
passes MalCatalog into .pragma library
      |
      +--> TheatreGenreApi.loadGenre(anime,...)
      |       |
      |       +-- MalCatalog.genreEntries("anime", ...)
      |       |
      |       +-- empty / not ready
      |              |
      |              +--> Jikan
      |                     |
      |                     +--> AniList on failure/empty
      |
      +--> GenreApi.loadGenre(manga,...)
              |
              +-- MalCatalog.genreEntries("manga", ...)
              |
              +-- empty / not ready
                     |
                     +--> Jikan
                            |
                            +--> Kitsu on failure/empty
```

The baked rows deliberately come back in the Jikan-shaped structure those card mappers already understand, so
falling between baked and live transport does not require a separate renderer.

**Tankoban Discover — bundled truth with live enrichment, NOT the same fallback:**

```text id="m0cpd4"
TankobanDiscoverApi
      |
      v
MalCatalog.discoverPage(...)
      |
      v
bundled wall lands immediately
      |
      +--> optional Jikan refresh
              |
              +-- first unfiltered page only
              +-- process-memory cache
              +-- stable MAL id matching only
              +-- overlay metadata on NEXT reload
              +-- preserve bundled order
              +-- preserve bundled availability truth
```

Jikan is an enrichment lane here, not a substitute source of wall membership. If the baked MAL database is absent,
`discoverPage()` produces no base rows. A later live result has no bundled identities onto which the wall overlay
can apply. Do not describe Tankoban Discover as having the same full “MAL → Jikan” fallback as the old genre page.

`trending` is another deliberate honesty boundary: the baked store has no comparable snapshots yet, so it uses
Popular order and returns `fallbackCatalog: "popular"` rather than inventing momentum.

**Theatre IMDb use has another shape again:**

```text id="zwitmp"
TheatreCatalogRules
      |
      | recipe
      v
TheatreApi
      |
      +-- IMDb-backed deep shelf
      |      |
      |      +--> ImdbCatalog.titleCatalog(...)
      |             |
      |             +-- absent/empty -> that index shelf honestly has no index rows
      |
      +-- live Cinemeta / extension shelf
             |
             +--> live rows still load
             +--> ImdbCatalog.titleFacts(...) may filter/enrich them
```

So the house rule is not “database missing means every catalogue transparently switches providers.”

The honest rule is:

> **C++ returns empty. The owning product surface decides whether that means live fallback, live-only continuation,
> live enrichment, or omission of an offline-only shelf.**

## 3. The files that matter

Full per-file descriptions belong in [`catalogs-index.md`](catalogs-index.md).

| File | Role |
|---|---|
| `native/engine/ImdbCatalog.h` | Theatre-only read contract over the baked IMDb database: readiness, constrained paged title queries, batched title facts |
| `native/engine/ImdbCatalog.cpp` | read-only SQLite opening, query allowlist/binding, fixed sort fragments, genre/language/anime filters, paging, result shaping |
| `native/engine/MalCatalog.h` | cross-world MAL contract: anime/manga genre rows and counts, Theatre anime catalogue, Tankoban manga facets/discovery |
| `native/engine/MalCatalog.cpp` | read-only MAL SQLite implementation, Jikan-shaped rows, anime query allowlist, explicit-aware manga discovery, Bayesian top-rated ordering, facet lookups |
| `qml/TheatreCatalogRules.js` | pure deep-Theatre catalogue brain: shelf inventories and the recipes that become constrained IMDb/MAL queries |
| `qml/TheatreCatalogPage.qml` | Movies / Shows / Anime deep-catalogue surface; injects both catalogue stores into its data lane |
| `qml/TheatreSeeAllPage.qml` | paged deep-catalogue grid; forwards both stores into Theatre's row loader |
| `qml/TheatreGenreApi.js` | Theatre genre data adapter: baked MAL first for anime, then Jikan → AniList; movies/shows stay on their Theatre live lane |
| `qml/TheatreGenrePage.qml` | context-bearing Theatre genre page that passes `MalCatalog` into the `.pragma library` adapter |
| `qml/GenreApi.js` | Tankoban manga genre adapter: baked MAL first, then Jikan → Kitsu |
| `qml/GenrePage.qml` | context-bearing manga genre page that passes `MalCatalog` into `GenreApi.js` |
| `qml/TankobanDiscoverApi.js` | Tankoban catalogue adapter over `MalCatalog` and the sibling `ComicsCatalog`; owns paging/facet adaptation and non-blocking MAL live enrichment |
| `scripts/theatre_brain/build_imdb_db.py` | **build-time dependency, not manifest-eligible** — fetch/normalize/bake `data/imdb_catalog.db` |
| `scripts/anime_brain/build_mal_db.py` | **build-time dependency, not manifest-eligible** — fetch/normalize/bake `data/mal_catalog.db` |
| `native/main.cpp` | **owned by `shell.paths`** — constructs both stores and exposes `MalCatalog` / `ImdbCatalog` to QML |
| `qml/TheatreApi.js` | **owned by `player.paths`** — principal Theatre consumer of both baked stores; deep catalogue, fact filtering, anime More Like This |
| `qml/MangaCatalogPage.qml` | **owned by `tankoban.paths`** — Tankoban consumer of MAL catalogue data |
| `qml/TankobanDiscoverPage.qml` | **owned by `tankoban.paths`** — page wrapper that injects `MalCatalog` and sibling `ComicsCatalog` into the Discover adapter |
| `qml/TankobanWorld.qml` | **owned by `tankoban.paths`** — Tankoban composition boundary |
| `qml/TheatreWorld.qml` | broad Theatre composition boundary. It injects both stores into deep catalogue / See All pages; cross-reference it rather than making catalogue storage own the whole world |
| `qml/TheatreSeries.qml` | broad Theatre detail page. Anime More Like This passes `MalCatalog` into TheatreApi; the detail/player lifecycle remains outside this guide |
| `native/engine/ComicsCatalog.cpp/.h` | **owned by `comics.paths`** — sibling baked-catalogue pattern only; NEVER duplicate it into `catalogs.paths` |
| `scripts/data_vault/publish_release.py` | **build-time dependency, not manifest-eligible** — pushes baked dbs to the private `Colosseum-Data` vault as dated release assets |
| `scripts/data_vault/pull_data.py` | **build-time dependency, not manifest-eligible** — universal fetch side: downloads latest (or `--tag`) release assets into `data/`, atomic tmp-then-swap |
| `scripts/data_vault/vault_common.py` | **build-time dependency, not manifest-eligible** — shared vault helper (repo, token, `gh` client) |

The Python builders and the vault scripts are essential to understanding the subsystem but cannot be placed in a
`.paths` manifest: the encyclopedia generator currently accepts C/C++ headers/sources, QML, and JS, not `.py`.

The `catalogs.paths` ownership set is:

```text id="7dq85j"
# Baked/offline metadata catalogues (IMDb + MAL).
# Python database builders (theatre_brain/build_imdb_db.py, anime_brain/build_mal_db.py)
# are documented by catalogs.md but are not manifest-eligible (.py is not harvested).
# Shell/player/tankoban-owned composition consumers remain cross-references.
# ComicsCatalog stays owned by comics.paths.

native/engine/ImdbCatalog.cpp
native/engine/ImdbCatalog.h
native/engine/MalCatalog.cpp
native/engine/MalCatalog.h
qml/GenreApi.js
qml/GenrePage.qml
qml/TankobanDiscoverApi.js
qml/TheatreCatalogPage.qml
qml/TheatreCatalogRules.js
qml/TheatreGenreApi.js
qml/TheatreGenrePage.qml
qml/TheatreSeeAllPage.qml
```

## 4. Where state lives

- **`data/imdb_catalog.db` is the baked IMDb runtime artifact.** It is generated, gitignored, and opened
  read-only. Its schema contains the title rows, flattened genre membership, and bake metadata.

- **`data/mal_catalog.db` is the baked MAL runtime artifact.** One file contains both the `anime` and `manga`
  worlds plus tag/facet tables. Do not split mental ownership merely because two product worlds consume it.

- **`data/*.db.building` is bake-time scratch.** Both builders construct a fresh temporary database, close it,
  remove the old final file if present, then rename the new file into place. Runtime never reads the `.building`
  path intentionally.

- **The vault is the release boundary between bake and install.** `publish_release.py` uploads the baked dbs as
  GitHub release assets on the **private `kingoftheseas56/Colosseum-Data`** repo under a dated tag
  (`data-YYYY-MM-DD`); `pull_data.py` fetches them back into `data/`. `pull_data.py` writes to a `.downloading`
  temp file then atomically renames, so a half-download never leaves a corrupt store in place. There is also a
  `COLOSSEUM_VAULT_HOST` escape hatch — set it to a flat base URL and `pull_data.py` fetches
  `<host>/<asset>` directly (the intended Cloudflare R2 / object-store path for the day the assets outgrow
  GitHub's 2 GB release-asset limit).

- **`enrichment/imdb/` is IMDb builder input cache.** `build_imdb_db.py` downloads an IMDb/Fribb input only when
  the cached basename does not already exist. A successful rerun therefore proves “rebaked from the cached
  inputs,” not necessarily “refetched today's upstream dumps.”

- **The MAL input cache belongs to `kagglehub`.** The builder calls `kagglehub.dataset_download()` for the anime
  and manga CSVs. The repository itself does not own a separate MAL raw-data cache directory.

- **The databases carry bake provenance, not user preferences.**
  - IMDb `meta`: at least `baked_at` and `vote_floor`.
  - MAL `meta`: at least `baked_at` and the source dataset id.

- **MAL count tables intentionally describe more rows than the browse tables may retain.** `tag_count` and
  `classification_count` are computed before the top-20,000-by-members slice. That is why a genre hero can
  truthfully say there are more titles than the local card wall can ever enumerate.

- **`MalCatalog` and `ImdbCatalog` hold process-lifetime read-only SQL connections.** They do not refresh
  themselves when a `.db` file changes underneath a running process. Replace/rebake the artifact between app
  runs unless an explicit reload mechanism is added.

- **QML context properties are composition state, not database state.** `native/main.cpp` exposes the objects as
  `MalCatalog` and `ImdbCatalog`. `.pragma library` JavaScript cannot see those context properties directly, so
  pages that have context access pass the object into their JS adapters.

- **Tankoban Discover's `liveCache` is process memory only.** It holds a short-lived Jikan enrichment snapshot,
  keyed by catalogue/filter/explicit state. It does not replace the bundled SQLite catalogue or persist across
  launches.

- **No user collection/progress/download state belongs here.** These are metadata indexes. Mutating a user's
  library must never require writing into `mal_catalog.db` or `imdb_catalog.db`.

## 5. Traps

1. **`MalCatalog` is not Theatre-only.** It serves Theatre anime **and** Tankoban manga. `genreEntries()` accepts
   either medium; `animeCatalog()` is the Theatre deep-catalogue seam; `discoverPage()` / `discoverFilters()` are
   explicitly manga/Tankoban seams.
   **WHY:** treating MAL as “the anime database” makes the Tankoban Discover dependency invisible. A release can
   then test Theatre Anime successfully while shipping a MAL artifact/schema that breaks manga facets or paging.

2. **“Missing database falls back live” is not one universal behavior.** MAL genre pages genuinely fall through
   to Jikan then AniList/Kitsu. Tankoban Discover instead needs the bundled wall and uses Jikan as identity-based
   enrichment. IMDb-backed deep Theatre shelves may simply disappear while unrelated live Theatre shelves keep
   working.
   **WHY:** a blanket fallback claim turns missing release data into a false-green test. Exercise each consumer,
   not just `ready()==false`.

3. **The deployment step is the `Colosseum-Data` vault, not a committed file.** `data/` is gitignored
   (`publish_release.py` / `pull_data.py` ship the baked dbs as private release assets — see §4). The public repo
   and its installer do not carry the databases.
   **WHY:** “the baker produced the file on my checkout” does not prove a clean installed machine received it.
   A fresh machine runs `pull_data.py` (or an equivalent release step) to populate `data/` before the catalogues
   are `ready()`.

4. **Do not commit the databases merely to make shipping obvious.** Their gitignored/generated status is
   deliberate: bulk public datasets are bake inputs, and the runtime consumes the artifact rather than source
   dumps.
   **WHY:** checking generated SQLite blobs into source changes repository economics without solving provenance,
   refresh, or release verification. Keep the vault as the release boundary instead.

5. **The runtime must never become a database builder.** IMDb download/parsing and Kaggle acquisition happen in
   the Python bake scripts; C++ opens one finished SQLite artifact read-only.
   **WHY:** moving upstream-data fetching into startup trades a deterministic, keyless local query for network
   latency, upstream schema churn, large downloads, and partial database construction inside the user session.

6. **An IMDb rebake is not automatically a fresh IMDb fetch.** The builder caches its raw inputs under
   `enrichment/imdb/` and downloads only missing cache files.
   **WHY:** running `build_imdb_db.py` twice can produce two freshly timestamped databases from the same cached
   source dump. Refresh policy must explicitly decide when those cached inputs are discarded/refetched.

7. **The MAL headline count and the MAL browsable row count answer different questions.** Counts are calculated
   before the builder keeps only the 20,000 most-membered rows per medium.
   **WHY:** `genreCount("manga", "Action")` can legitimately exceed the number of Action rows that repeated
   `genreEntries()`-style browsing could surface. Do not “fix” that mismatch by changing the hero count to the
   truncated slice unless product meaning changes.

8. **Anime and manga have different explicit-content bake policy.** Anime rows are SFW-only at MAL bake time.
   Manga retains non-SFW rows and marks them `explicit`, because Tankoban Discover has a runtime explicit-content
   gate.
   **WHY:** applying manga's assumption to anime can expose rows the Theatre anime database never carried;
   applying anime's assumption to manga would permanently discard content an opted-in Tankoban user is allowed
   to see.

9. **The stores accept a deliberately tiny query language, not arbitrary filter maps.** `animeCatalog()` and
   `titleCatalog()` reject unknown keys; order values map to fixed SQL; values are bound; limits are clamped.
   **WHY:** passing a convenient new QML property into the query does not make it “ignored.” It can intentionally
   turn the whole query into an empty result. Add a filter at the C++ contract and its harness before recipes use
   it.

10. **`ImdbCatalog` has one source-comment drift to be aware of.** The implementation and native harness support
    `notGenre`, while the header's documented allowlist does not name it.
    **WHY:** the encyclopedia generator harvests the file's own top comment. Accepting the current header as
    complete would publish a smaller contract than the implementation/tests actually exercise. Fix the header
    comment or ratify the wider contract deliberately before trusting the generated index's allowlist line.

11. **`genreEntries()` is older and less strict than the newer catalogue APIs.** Its intended order values are
    `members` and `score`, but the implementation treats `score` specially and routes every other value to members
    ordering.
    **WHY:** do not generalize the strict unknown-order rejection of `animeCatalog()` / `titleCatalog()` to this
    older function. Callers should still pass only the documented values.

12. **IMDb original language is evidence-derived, not omniscient.** The bake tries explicit original-aka language,
    script evidence, matching aka evidence, then narrow regional/US fallbacks; otherwise it leaves the language
    unknown.
    **WHY:** the system intentionally prefers an empty language to a guess. A `notLang:"en"` query also excludes
    unknown-language rows, so “international” means **known non-English**, not “anything that wasn't proved
    English.”

13. **IMDb's anime flag does not mean “Japanese-language.”** The builder uses an anime identity cross-reference;
    language is kept separately.
    **WHY:** using `origLang == "ja"` as an anime filter loses dubbed/romanized identity and misclassifies
    Japanese live action. The two fields answer different questions.

14. **MAL rows are Jikan-shaped on purpose.** The baker/store reconstruct nested image and credit/genre shapes
    familiar to the live Jikan adapters.
    **WHY:** “simplifying” the local return shape means every consumer needs a separate baked-data renderer, and
    the local→live fallback stops being one presentation path.

15. **A `.pragma library` cannot see the QML context property by bare name.** `GenreApi.js` and
    `TheatreGenreApi.js` take `catalog` explicitly; their QML pages pass `MalCatalog` in.
    **WHY:** this already failed once: the native object existed and the database existed, but the library saw
    `MalCatalog` as undefined and silently stayed on the live provider.

16. **Tankoban Discover's Jikan refresh must not reorder the wall under the user.** The live response is cached and
    merged by stable MAL id on the next reload; the bundled order remains authoritative for the current wall.
    **WHY:** replacing the wall immediately from a live set makes posters jump after first paint and loses local
    availability truth that Jikan cannot know.

17. **A live Tankoban row without a stable MAL id must not title-match itself into the bundled wall.**
    **WHY:** title similarity is not identity. Wrong merges can attach rating/artwork to the wrong edition or
    work; the current enrichment seam deliberately requires canonical MAL ids.

18. **“Trending” currently means an honest fallback, not measured trend.** `MalCatalog.discoverPage("trending")`
    uses Popular order and reports `fallbackCatalog:"popular"` because comparable snapshots do not exist yet.
    **WHY:** a label should not manufacture a temporal claim the data cannot support. When snapshots become real,
    change the bake/query contract and its tests together.

19. **Query failure and a legitimate empty result currently have the same C++ shape.** A failed `QSqlQuery` returns
    an empty collection, just like “no titles matched.”
    **WHY:** if a future schema mismatch ships, UI fallback can disguise it as an unpopular shelf. Release
    verification must probe known rows/schema rather than relying only on “the app did not crash.”

20. **`ready()` proves the SQLite file opened, not that the expected schema is complete or fresh.**
    **WHY:** a stale pre-Discover MAL database can open successfully while lacking later tables/columns. The
    acceptance runner already distinguishes legacy schema in places; deployment verification should inspect the
    artifact generation/schema it intends to ship.

21. **The SQLite plugin is part of the runtime dependency, not inside the `.db`.** The native CMake build explicitly
    stages Qt SQL and the SQLite driver beside the application.
    **WHY:** a perfectly shipped `mal_catalog.db` is still “not ready” if the deployed Qt build cannot load
    `QSQLITE`. Validate from a clean installed environment, not a developer shell whose Qt installation can fill
    in missing plugins.

22. **Do not duplicate `ComicsCatalog.cpp/.h` into this manifest.** It follows the sibling baked-store pattern and
    TankobanDiscoverApi can consume it, but Comics already owns those source descriptions.
    **WHY:** encyclopedia cross-references are cheap; two accepted owners for the same file are drift ambiguity.
    Keep Comics' store in `comics.paths`, and describe only the integration boundary here.

23. **The builders and vault scripts themselves cannot currently go in `catalogs.paths`.** The encyclopedia
    generator rejects `.py` as an unsupported source suffix.
    **WHY:** the database build + ship pipeline is still load-bearing knowledge. Leaving the builders and vault
    scripts out of the guide because the generator cannot harvest them would document only half the lifecycle.
    Keep them as explicit non-manifest dependencies here.

24. **`imdb_catalog.db` is NOT in the vault ship/pull artifact list (as of this SHA).** `publish_release.py`
    hardcodes `ARTIFACTS = ["comics_catalog.db", "mal_catalog.db"]`, and `pull_data.py`'s `COLOSSEUM_VAULT_HOST`
    branch fetches the same two names. The IMDb baker writes `data/imdb_catalog.db` locally and the C++ store
    opens it read-only, but no release/pull script currently stages it to or from the vault.
    **WHY:** assuming “IMDb ships the same way MAL ships” hides a real deployment gap. Either IMDb reaches
    installed machines by a separate mechanism that should be documented here, or IMDb-backed deep Theatre shelves
    are currently only available on machines that ran the baker. Before trusting IMDb in a release, confirm the
    real path and either add `imdb_catalog.db` to both vault lists or record the alternative mechanism here.

## 6. How to test it

There are four different things to prove:

1. the bake logic;
2. the native read/query contracts;
3. the product fallback/adaptation paths;
4. the release actually shipped the databases.

They are not interchangeable.

**IMDb bake logic without downloading the full dataset:**

```bat id="ofynkz"
python scripts\theatre_brain\build_imdb_db.py --selftest
```

This exercises the pure language derivation, type/adult/vote-floor filters, and anime-identity rule.

A real bake is:

```bat id="edxt1l"
python scripts\theatre_brain\build_imdb_db.py
```

That can use already-cached inputs under `enrichment/imdb/`. If the test objective is source freshness, inspect or
refresh that cache first rather than reading the new `baked_at` timestamp as proof.

**MAL bake:**

```bat id="hqtuqb"
python scripts\anime_brain\build_mal_db.py
```

A useful post-bake inspection must prove both worlds exist:

- substantial `anime` rows;
- substantial `manga` rows;
- populated tags for both;
- populated manga classification facets;
- explicit manga rows where the current source dataset contains them.

The existing focused gate does that when a local MAL database is present:

```powershell id="xsl48m"
powershell -NoProfile -ExecutionPolicy Bypass -File tests/test_mal_genre_catalog_p0.ps1
```

When the DB is absent, that script intentionally checks the source/fallback contract and reports that the baked
artifact is absent; it does not manufacture one.

**Native read-only query contracts:**

After the normal native build:

```bat id="4ehrtl"
native\build-msvc\imdb_catalog_harness.exe
native\build-msvc\mal_catalog_rows_harness.exe
native\build-msvc\mal_catalog_discover_harness.exe
```

These use temporary SQLite fixtures rather than the huge production artifacts.

`imdb_catalog_harness` proves:

- read-only opening;
- movie/series/mini typing;
- rating/vote/year/runtime/genre/language filters;
- anime exclusion;
- `notGenre`;
- paging and the 100-row clamp;
- strict unknown-key/order rejection;
- bound values;
- `titleFacts()`.

`mal_catalog_rows_harness` proves Theatre's paged anime contract:

- score/members ordering;
- vote floors;
- status/type/year/member bands;
- bound tag filter;
- paging;
- clamping;
- strict allowlist.

`mal_catalog_discover_harness` proves Tankoban's manga contract:

- catalogue ordering;
- case-insensitive stable facet keys;
- explicit-content gating;
- facet counts;
- paging/exhaustion;
- honest Trending→Popular fallback;
- binding/allowlisting;
- missing-db behavior.

Those three targets are defined in `native/CMakeLists.txt` (as `add_executable`), but they are **not** registered
with `add_test`, so **a normal CTest run does not prove these contracts ran**. Invoke the `.exe` targets directly
as above; treat their self-check output as the contract proof.

**Theatre catalogue product contract:**

```powershell id="1ztkc7"
powershell -NoProfile -ExecutionPolicy Bypass -File tests/test_theatre_deep_catalogue.ps1 -Stage All
```

This is primarily the deep-catalogue QML/rules/page acceptance runner. It checks shelf rules, API row adaptation,
See All, the real page, preferences, and Discover regressions. It does not replace the native IMDb/MAL harnesses
above.

For changes scoped to rules/API/page, its named `-Stage` slices are useful, but run `All` before adopting a
cross-cutting catalogue change.

**Tankoban Discover product contract:**

```powershell id="ycmkel"
powershell -NoProfile -ExecutionPolicy Bypass -File tests/test_tankoban_discover.ps1
```

This composes the explicit-content, shared Discover, Tankoban routing, MAL baked-catalogue, native
`mal_catalog_discover_harness`, and offscreen adapter/page gates. It also runs the sibling Comics catalogue checks
because the Tankoban Discover adapter serves both baked stores.

That is expected cross-subsystem verification; it does **not** transfer `ComicsCatalog.cpp/.h` ownership into this
guide.

**The release/deployment gate is separate.**

The release boundary is the `Colosseum-Data` vault. On a clean machine that did not run the bakers, require:

```text id="btxkjj"
python scripts/data_vault/pull_data.py
  -> populates data/comics_catalog.db, data/mal_catalog.db
  -> (imdb_catalog.db is NOT pulled — see trap 24)
```

Then launch the application and prove:

- `ImdbCatalog.ready()` is true through real Theatre behaviour (only on machines that ran the IMDb baker — see trap 24);
- `MalCatalog.ready()` is true through both Theatre **and Tankoban**;
- an IMDb-backed deep Theatre shelf returns rows;
- a Theatre anime genre page can render from the baked MAL lane;
- a Tankoban manga genre page can render from the baked MAL lane;
- Tankoban Discover returns its bundled first page and facets;
- disabling network still leaves the intended baked surfaces usable;
- restoring network exercises the appropriate live fallback/enrichment lane without replacing local truth
  incorrectly.

Then remove/park each DB deliberately and check the **actual surface-specific degradation**:

```text id="x5i5u8"
mal_catalog.db absent
  -> Theatre anime genre: live Jikan/AniList path
  -> Tankoban manga genre: live Jikan/Kitsu path
  -> Tankoban Discover: bundled wall unavailable; do NOT call this a transparent live fallback

imdb_catalog.db absent
  -> IMDb-backed index shelves unavailable
  -> unrelated live Theatre shelves remain capable of loading
```

### What the automated/source gates cannot prove

- that `pull_data.py` was actually run on the target installed machine (it is a manual/scripted step, not automatic);
- that the database shipped to users was baked from the intended upstream snapshot;
- that an IMDb bake used freshly fetched inputs rather than the local raw-data cache;
- that Kagglehub resolved the intended MAL dataset revision;
- that the packaged Qt runtime can load `QSQLITE` on a clean machine;
- that a currently live Jikan/AniList/Kitsu/Cinemeta endpoint still behaves as its adapter expects;
- that a stale but openable database has the schema generation the current application expects;
- that `imdb_catalog.db` reaches installed machines at all today (see trap 24);
- that future “Trending” semantics are legitimate until comparable snapshots actually exist.

Those are Brotherhood's ground-truth/release checks, not facts this source-read draft can promote to “verified.”

## Keeping this page honest

```bash id="can1re"
# refresh the generated index after changing a covered source file
python scripts/code_encyclopedia.py --paths docs/encyclopedia/catalogs.paths \
  --output docs/encyclopedia/catalogs-index.md --state docs/encyclopedia/catalogs-state.json

# gate: fails when a covered file changed since its description was accepted
python scripts/code_encyclopedia.py --paths docs/encyclopedia/catalogs.paths \
  --output docs/encyclopedia/catalogs-index.md --state docs/encyclopedia/catalogs-state.json --check

# after reviewing a changed description, ratify that file
python scripts/code_encyclopedia.py --paths docs/encyclopedia/catalogs.paths \
  --output docs/encyclopedia/catalogs-index.md --state docs/encyclopedia/catalogs-state.json --accept <path>
```

When changing this subsystem:

1. keep `ComicsCatalog.cpp/.h` in `comics.paths` only;
2. keep `native/main.cpp`, `qml/TheatreApi.js`, and the already-owned Tankoban QML files with their current owners;
3. if the vault ship/pull artifact lists change, update §4 and trap 24 together;
4. if the `ImdbCatalog.h` `notGenre` comment is fixed, update trap 10;
5. only then regenerate and accept `catalogs-index.md`.
