# Torrent search — subsystem guide

> **Hand-written. Keep it true.** If you change how Colosseum chooses torrent indexes, fans searches out,
> normalizes tracker answers, records source health, or hands magnets/info-hashes to the owning media lane,
> update this file in the same commit. The per-file index beside it
> ([`torrent-search-index.md`](torrent-search-index.md)) is generated — never edit that one.
>
> Drafted via Preflight-Architect, ground-truthed and adopted by the ZCode-seat session, 2026-08-09.
> Source-read against `master@a149e94f2b32e0b992288a4dd6480072bb651272` (= HEAD at adoption); the draft's original
> pin was one commit earlier (`15798643…`, Vault Slice 11) and the intervening commit touched no torrent-search
> file. Ground-truth pass verified the 13-file count and zero-overlap boundary independently against the 51 cpp/h
> files in `native/torrent/`; the media allowlists (`books`/`audiobooks`/`comics` → the three direct indexers,
> Knaben excluded); the Knaben "never order_by seeders" production rule vs the `knaben_probe` drift; the Knaben
> dormant-vs-implementation mismatch (trap 7) by reading `buildIndexersFor` in full; the PirateBay `601`/`102`
> category routing; the ExtraTorrents six-URL fallback and its `with_adult=1` second candidate; the empty-handle
> and cancellation lifecycle; and the harness-registration claim (built in `native/CMakeLists.txt`, no `add_test`,
> so CTest does not run them). The `comic_torrents_search_harness` file-table row was tightened to state its
> libtorrent link dependency explicitly. Adopt as the current truth; re-verify on the next indexer/source change.

## 1. What this subsystem is for

Turn **several unreliable public torrent indexes into one small, cancellable, source-aware search contract** for
the media systems that actually know what a good book/comic result looks like.

This guide covers the 13-file generic search/indexer layer under `native/torrent/`:

- one normalized result shape;
- one base indexer contract;
- three currently allowlisted direct indexers;
- one retained-but-normally-disabled meta-indexer;
- one headless fan-out service.

It does **not** own the torrent bytes.

That boundary is load-bearing:

```text
this guide
  query -> external indexes -> normalized TorrentResult batches
       -> source health / source identity / magnet / info-hash
                                           |
                                           v
                             media-specific rank / dedup / choose
                                           |
                                           v
                         separate downloader / TorrentEngine layer
                                           |
                                           v
                                      payload bytes
```

`native/torrent/engine/` is a sibling boundary containing the embedded libtorrent session/repository machinery.
Book, Comics, and Tankoban guides already own their media-specific ranking, file picking, download, and ingest
paths. This page must not pretend the generic search service makes those decisions.

Today the normal federated roster for `books`, `audiobooks`, and `comics` is:

```text
PirateBay
ExtraTorrents
Torrents-CSV
```

`KnabenIndexer` is still compiled and directly testable, but is deliberately absent from those three media
allowlists because eyes-on testing found its fuzzy meta-search relevance too willing to return look-alikes.
Its purpose is retained as a possible category-scoped route back to underlying trackers that cannot be asked
directly.

The generic service also owns **partial-failure survival**: one tracker timing out must not erase rows another
tracker already returned.

## 2. The flow

**Application composition:**

```text
native/main.cpp
    |
    +-- CachingNam(..., useCache=false)
    |      |
    |      +-- pinned/UA-stamped network path
    |      +-- NO disk cache: seeder counts and source health stay live
    |
    +-- BookTorrents(searchNam, TorrentEngine)
    |
    +-- ComicDownloader(..., searchNam, TorrentEngine)
    |
    +-- optional COLOSSEUM_TORRENT_SEARCHTEST smoke
           |
           v
    TankorentSearchService(searchNam)
```

The shared search network manager is deliberately uncached. Search rows contain fast-moving availability facts;
a cached successful response can make stale seed counts look current, while a cached failure can make a recovered
indexer look dead.

**One federated search:**

```text
BookTorrents / ComicTorrents / another owning caller
        |
        v
TankorentSearchService.startSearch(
    mediaType,
    sourceFilter,
    query,
    limit,
    categoryId)
        |
        +-- choose media allowlist
        +-- honor tankorent/indexers/<id>/enabled
        +-- explicit source pick bypasses MEDIA allowlist
        |
        v
create a fresh indexer object per selected source
        |
        +----------------+------------------+------------------+
        |                |                  |                  |
        v                v                  v                  v
 PirateBayIndexer   ExtTorrentsIndexer  TorrentsCsvIndexer  KnabenIndexer
        |                |                  |                  |
        v                v                  v                  v
 apibay JSON        extto HTML          torrents-csv JSON   knaben JSON POST
        |                |                  |                  |
        +----------------+------------------+------------------+
                                 |
                                 v
                           TorrentResult[]
                                 |
                    one resultsReady() batch
                       PER successful indexer
                                 |
                    one indexerError() event
                       PER failing indexer
                                 |
                                 v
                searchFinished(handle) once all settle
```

The service does not merge or rank the returned batches. The caller owns that.

For Comics, for example:

```text
TankorentSearchService
    |
    +-- partial batch A
    +-- partial batch B
    +-- source C error
    |
    v
ComicTorrents
    |
    +-- accumulate
    +-- canonical-hash dedup
    +-- edition-aware ranking
    |
    v
user/automatic pick
```

For Biblio, the equivalent responsibility lives in the book torrent facade/ranker already covered by
`biblio.paths`.

**Media/source routing:**

```text
mediaType = books
    -> piratebay + exttorrents + torrentscsv

mediaType = audiobooks
    -> piratebay + exttorrents + torrentscsv

mediaType = comics
    -> piratebay + exttorrents + torrentscsv

sourceFilter = a specific id
    -> bypass media allowlist
    -> still honor that source's enabled setting

unknown / empty mediaType + sourceFilter=all
    -> no media allowlist applies
    -> every enabled concrete indexer is eligible
```

That last branch is easy to miss and matters for Knaben; see the traps.

**Native category routing is indexer-specific, not universal:**

```text
PirateBayIndexer.categoryFor("books")       -> 601
PirateBayIndexer.categoryFor("audiobooks")  -> 102

ExtraTorrents      -> base default: no native category
Torrents-CSV       -> base default: no native category
Knaben             -> base default unless caller explicitly supplies categoryId
```

So “the search service asks every tracker for books only” is too strong. PirateBay gets true source-side scoping
for those two media types; the other direct indexes can return broader rows that the downstream media-specific
ranker must reject or demote.

**PirateBay:**

```text
query
  |
  v
GET https://apibay.org/q.php?q=...
  + optional cat=<native category>
  + 15 s transfer timeout
  |
  v
JSON array
  |
  +-- require non-empty name + hash
  +-- canonicalize 40-hex BTIH
  +-- synthesize magnet + tracker list
  +-- normalize size / seeders / leechers / category / date / details URL
  |
  v
TorrentResult[]
```

The caller's requested `limit` is not sent to or enforced by this adapter.

**ExtraTorrents:**

```text
query
  |
  v
try up to six extto.org URL shapes, in order
  |
  +-- 10 s PER candidate
  +-- HTML list parse
  |
  +-- no rows? try next URL
  |
  +-- rows with magnets -> use them
  |
  +-- rows without magnets
          |
          +-- fetch up to 30 detail pages concurrently
          +-- extract magnet when possible
  |
  v
emit only rows that ended with a magnet
```

The fallback sequence exists because the site's browse/search shape has changed over time. This is a scraper, not
a stable API contract.

**Torrents-CSV:**

```text
query
  |
  v
GET https://torrents-csv.com/service/search
    ?q=<query>
    &size=<1..100>
  |
  +-- 15 s timeout
  +-- object.torrents[] OR bare array
  |
  +-- require name + hash
  +-- synthesize magnet
  +-- normalize size / peers / date
  |
  v
TorrentResult[]
```

The category argument is ignored by this adapter.

**Knaben:**

```text
query
  |
  v
POST https://api.knaben.org/v1
  {
    search_type: "score",
    search_field: "title",
    query: ...,
    hide_unsafe: true,
    hide_xxx: true,
    size: 1..300,
    categories: [...]   // only when explicitly supplied
  }
  |
  +-- deliberately NO order_by=seeders
  |
  v
hits[]
  |
  +-- require title
  +-- require canonical 40-hex hash
  +-- keep returned magnet or synthesize one
  +-- preserve underlying tracker name as provenance
  |
  v
TorrentResult[]
```

Knaben is a meta-indexer: `sourceKey` remains `knaben`, while `category` carries the underlying origin such as
`1337x` or `The Pirate Bay`.

## 3. The files that matter

Full per-file descriptions belong in [`torrent-search-index.md`](torrent-search-index.md).

| File | Role |
|---|---|
| `native/torrent/TorrentResult.h` | normalized cross-indexer row plus canonical BTIH, magnet-builder, tracker-list, and human-size helpers |
| `native/torrent/TorrentIndexer.h` | abstract indexer/search/health contract, health vocabulary, QSettings telemetry seam |
| `native/torrent/TorrentIndexer.cpp` | request timing, success/error classification, persisted last-success/error/latency telemetry |
| `native/torrent/TankorentSearchService.h` | concurrent headless fan-out contract: handles, partial batches, per-indexer errors, terminal completion |
| `native/torrent/TankorentSearchService.cpp` | media allowlists, enabled-source filtering, concrete indexer construction, category dispatch, cancellation, settlement |
| `native/torrent/PirateBayIndexer.h` | PirateBay adapter contract and its only built-in media-category mapping (`books`→601, `audiobooks`→102) |
| `native/torrent/PirateBayIndexer.cpp` | apibay JSON request/parser and normalized magnet/result construction |
| `native/torrent/ExtTorrentsIndexer.h` | state machine for the ExtraTorrents list/detail scraper |
| `native/torrent/ExtTorrentsIndexer.cpp` | six-shape extto search fallback (2nd candidate appends `&with_adult=1`), HTML parsing, detail magnet retrieval, normalized rows |
| `native/torrent/TorrentsCsvIndexer.h` | Torrents-CSV keyless JSON adapter contract |
| `native/torrent/TorrentsCsvIndexer.cpp` | Torrents-CSV request/parser and normalized rows |
| `native/torrent/KnabenIndexer.h` | retained meta-search adapter plus pure request-body/parser seams used by its deterministic harness |
| `native/torrent/KnabenIndexer.cpp` | relevance-first Knaben JSON POST (deliberately no `order_by`), unsafe/adult source filters, origin-provenance normalization |
| `native/torrent/BookTorrents.cpp/.h` | **owned by `biblio.paths`** — consumes this service, accumulates/ranks results, then hands the chosen item to the book downloader |
| `native/torrent/ComicTorrents.cpp/.h` | **owned by `comics.paths`** — consumes this service for automatic/manual source search and owns comic-specific merge/rank behavior |
| `native/torrent/ComicTorrentRanker.cpp/.h` | **owned by `comics.paths`** — identity/edition relevance; generic search must not recreate it |
| `native/torrent/BookTorrentRanker.cpp/.h` | **owned by `biblio.paths`** — book identity/relevance; generic search must not recreate it |
| `native/torrent/MangaNyaaSource.cpp/.h` | **owned by `tankoban.paths`** — manga source path with its own semantics, not part of this generic federation |
| `native/torrent/engine/` | **sibling boundary, not this guide's 13-file manifest** — embedded libtorrent session/repository/download machinery |
| `native/main.cpp` | **owned by `shell.paths`** — shell composition boundary: creates the uncached search NAM, wires `COLOSSEUM_TORRENT_SEARCHTEST` → `selfTest()`, and sets the 45 s hard backstop |
| `tests/knaben_indexer_harness.cpp` | deterministic no-network proof of the retained Knaben request/parser contract; links only Qt Core/Network (no libtorrent) |
| `tests/knaben_probe.cpp` | live Qt-network reachability/Cloudflare probe for Knaben; **not** a production-relevance test — still sends `order_by=seeders` (trap 20) |
| `tests/comic_torrents_search_harness.cpp` | cross-subsystem deterministic proof that the fan-out seam survives partial failure and stale/cancelled handles; links `colosseum_libtorrent` so it is **not** a lightweight no-libtorrent seam (unlike the Knaben harness) |

Proposed `torrent-search.paths`:

```text
# Generic federated torrent search/indexer layer.
# Media-specific rank/download/ingest files stay in their existing guides.
# native/torrent/engine is a separate sibling boundary.

native/torrent/ExtTorrentsIndexer.cpp
native/torrent/ExtTorrentsIndexer.h
native/torrent/KnabenIndexer.cpp
native/torrent/KnabenIndexer.h
native/torrent/PirateBayIndexer.cpp
native/torrent/PirateBayIndexer.h
native/torrent/TankorentSearchService.cpp
native/torrent/TankorentSearchService.h
native/torrent/TorrentIndexer.cpp
native/torrent/TorrentIndexer.h
native/torrent/TorrentResult.h
native/torrent/TorrentsCsvIndexer.cpp
native/torrent/TorrentsCsvIndexer.h
```

That is exactly 13 source files. Compare the list against the live local manifests before creating it; do not
infer ownership from GitHub visibility of `docs/encyclopedia/`.

## 4. Where state lives

- **Search results are ephemeral.** `TankorentSearchService` does not persist a search result cache. Each caller
  accumulates the batches it needs for the lifetime of its own search/session.

- **`TankorentSearchService::m_contexts` is in-memory concurrency state.** Every live handle owns a list of fresh
  indexer objects plus a pending count. A handle disappears when every source settles or when the caller cancels.

- **The handle sequence is process-local.** `search-1`, `search-2`, … are correlation tokens, not durable
  identities. Never persist them as job ids.

- **Per-indexer enable state is QSettings-backed:**

  ```text
  tankorent/indexers/<id>/enabled
  ```

  Default is `true`. Disabled sources are not instantiated for a search.

- **Per-indexer health telemetry is QSettings-backed:**

  ```text
  tankorent/indexers/<id>/health/lastSuccess
  tankorent/indexers/<id>/health/lastError
  tankorent/indexers/<id>/health/lastResponseMs
  ```

  The current `IndexerHealth` enum value itself is **not** restored as current truth on boot. A new indexer starts
  `Unknown` and may show prior telemetry until a live query establishes today's state.

- **`TorrentIndexer::m_requestTimer` is per-instance request timing.** The service creates new adapter objects for
  each search, so live health in an instance is short-lived; persisted telemetry is the cross-instance bridge.

- **The shared search `QNetworkAccessManager` is uncached.** In the normal app it is a pinned, browser-UA-stamped
  `CachingNam` constructed with `useCache=false`. That is part of correctness: source results and seeder counts
  are live facts, not image-cache material.

- **`TorrentResult::infoHash` is the canonical cross-source identity when available.** It is lowercase 40-hex
  BTIH or empty. Consumers may fall back to extracting a hash from `magnetUri`; they must not invent a hash.

- **`sourceKey` means the adapter that answered.** For Knaben, the actual underlying tracker is additionally kept
  in `category` as provenance.

- **No torrent payload, resume data, file priorities, DHT state, or seeding state lives here.** Those belong to
  the separate download engine and the media-specific downloader paths.

## 5. Traps

1. **This is torrent SEARCH, not the libtorrent engine.** The 13 files stop at normalized rows, magnets, source
   health, and search lifecycle. They do not create a libtorrent session, choose files inside a torrent, write
   payload bytes, resume jobs, or seed.
   **WHY:** putting byte-engine behavior in this guide creates the exact “58 files = one subsystem” mistake the
   coverage pass corrected. Keep search, media-specific selection, and transport ownership separate.

2. **The service returns per-indexer batches, not one ranked answer.** `resultsReady(handle, rows)` fires once for
   each successful source. There is no generic cross-source dedup or relevance ranking here.
   **WHY:** book/comic identity is domain knowledge. A 900-seed look-alike can be worse than a 5-seed exact ISBN
   or edition match; generic “highest seed wins” logic would silently override the owning rankers.

3. **Partial source failure is not whole-search failure.** `indexerError()` decrements the same pending counter as
   `resultsReady()`, and `searchFinished()` waits until every selected source has settled.
   **WHY:** public indexes rot independently. Treating one HTTP error as terminal would throw away healthy rows
   already returned by the other sources.

4. **An empty handle is already terminal — and emits no signal.** If filtering/enable state leaves zero indexers,
   `startSearch()` returns an empty string and no `resultsReady`, `indexerError`, or `searchFinished` follows.
   **WHY:** callers that always wait for `searchFinished()` can leave a spinner running forever when every source
   is disabled or a specific disabled source was requested.

5. **Cancellation deliberately emits no completion signal.** `cancelSearch(handle)` disconnects/cleans the
   context and erases it without `searchFinished`.
   **WHY:** replacement searches must retire the old handle themselves. A caller that interprets cancellation as
   “wait for the normal final signal” will retain stale UI state.

6. **Explicit source selection bypasses the media allowlist, but not the user's enabled flag.** Asking for
   `"knaben"` explicitly may therefore reach a source not used by normal Books/Comics federation, unless that
   source is disabled in QSettings.
   **WHY:** an explicit source picker is an operator/user override of media routing, not an override of the
   source's off switch.

7. **The “Knaben is never instantiated” comment is narrower than the implementation.** Knaben is absent from the
   `books`, `audiobooks`, and `comics` allowlists. But `buildIndexersFor()` treats an unknown or empty media type as
   “no allowlist,” which makes every enabled concrete adapter eligible; an explicit `sourceFilter="knaben"` also
   bypasses the allowlist.
   **WHY:** the public header explicitly permits `"videos"` and `""`. Brotherhood should either ratify those
   escape hatches as intentional or tighten the implementation/comment before this page promises Knaben is fully
   dormant.

8. **Source-side media filtering is only partially implemented.** PirateBay maps Books to category `601` and
   Audiobooks to `102`. ExtraTorrents and Torrents-CSV keep the base no-category behavior, and Comics receives no
   PirateBay category either.
   **WHY:** broad tracker results can still reach the owning ranker. Do not remove downstream type/relevance
   checks because the service comment says it restored source-side filtering.

9. **ExtraTorrents' second fallback explicitly adds `with_adult=1`.** The adapter can also label `XXX` rows and
   contains no local product-policy rejection before emitting them.
   **WHY:** generic tracker output is not automatically content-policy-clean. Any product surface that must refuse
   adult material needs an explicit downstream rule; do not assume the source federation enforced it. (Colosseum's
   product rule is no adult content and no acquisition source enabled by default — so the downstream filter is
   load-bearing here, not a nicety.)

10. **ExtraTorrents can take longer than the app's search smoke backstop.** It has six candidate URL shapes with a
    10-second transfer timeout each — up to roughly 60 seconds before detail-page work — while
    `COLOSSEUM_TORRENT_SEARCHTEST` has a 45-second app-level hard backstop.
    **WHY:** a smoke process that exits at 45 seconds is not proof that the adapter exhausted its own fallback
    state machine. Treat timeout as an observation requiring source-level diagnosis, not automatic “dead indexer.”

11. **ExtraTorrents can record an error but still settle as an empty success.** Network failures advance to the
    next candidate; when the candidate list is exhausted, `tryNextUrl()` emits `searchFinished({})` rather than
    `searchError()`.
    **WHY:** the fan-out caller may see no `indexerError` even though persisted health records the last network
    failure. “Zero rows” and “every URL failed” are not always distinguishable from service signals alone.

12. **ExtraTorrents marks success before every required detail magnet has landed.** A non-empty list page calls
    `markSuccess()`, then missing magnets may require separate detail requests; failed detail requests are simply
    omitted when the final result list is assembled.
    **WHY:** `health=Ok` means the index/search page parsed successfully. It does not prove every listed candidate
    produced a usable magnet or that the final result count is non-zero.

13. **ExtraTorrents is an HTML parser over a changing site.** It carries a modern title pattern plus a legacy
    fallback and six endpoint shapes.
    **WHY:** a perfectly healthy HTTP 200 can become “zero results” when markup drifts. Monitor parser fixtures or
    eyes-on source output; HTTP reachability alone is weak evidence for scraper health.

14. **PirateBay ignores the caller's `limit`.** `search()` marks the parameter unused and accepts the upstream API
    array as returned.
    **WHY:** callers must not depend on the generic `limit` as a hard memory/UI bound for every adapter. If a hard
    cap becomes required, enforce it consistently at a deliberate shared seam.

15. **PirateBay's human category label is broad, not the routing id.** Category `601` is correctly sent to apibay
    for e-books, but `categoryLabel()` maps every `6xx` id to `"Other"`.
    **WHY:** `categoryId` is tracker routing metadata; `category` is coarse display metadata. Do not use the word
    `"Other"` to conclude the row was not a book.

16. **A syntactically valid JSON response marks PirateBay/Torrents-CSV healthy before row usability is known.**
    Rows with missing names/hashes can then all be dropped.
    **WHY:** “source answered valid JSON” and “source returned usable torrent identities” are different health
    questions. If product health needs both, add a separate metric instead of redefining the network health flag.

17. **A parse failure over HTTP 200 falls into a coarse `Unreachable` health bucket.** `markError()` classifies
    by QNetworkReply error/status; it has no distinct “schema/parse drift” state.
    **WHY:** an API shape regression can look like a network outage in persisted health. Preserve the emitted parse
    error text when diagnosing; do not diagnose from the enum alone.

18. **Cloudflare classification is header/status-based.** The shared health helper recognizes a 403 with
    Cloudflare headers. Knaben additionally treats non-JSON as possible CF/outage, but a 200 HTML challenge may
    still persist as coarse `Unreachable`.
    **WHY:** the human-readable error and health bucket can legitimately disagree in specificity.

19. **Knaben's production relevance rule is “never order by seeders.”** Its request builder intentionally omits
    `order_by`; the pure harness pins this because seeder ordering caused the meta-indexer to ignore the query and
    return global SEO-spam results.
    **WHY:** seeder count is a tie-break, not identity. Do not “optimize” this request to sort by popularity.

20. **The live `knaben_probe` is NOT a production-query-shape test.** At this SHA the probe still sends
    `order_by=seeders`, while production deliberately does not.
    **WHY:** the probe's purpose is Qt/TLS/Cloudflare reachability. Passing it proves the app's network fingerprint
    can reach JSON; it does not prove production relevance semantics. Use `knaben_indexer_harness` for that.

21. **Knaben provenance has two levels.** `sourceKey="knaben"` identifies the adapter; `category` may contain
    `"1337x"`, `"The Pirate Bay"`, or another underlying tracker.
    **WHY:** collapsing both to “Knaben” hides where a selected row actually originated; treating the origin as
    the adapter breaks source-enable and health semantics.

22. **Torrents-CSV ignores `categoryId`.** Even if the service or caller supplies one, this adapter searches by
    query and size only.
    **WHY:** a future caller cannot make Torrents-CSV media-safe merely by filling the category argument. Its rows
    still require downstream classification/relevance checks.

23. **Canonical BTIH is the dedup key when available; titles are not identity.** `canonicalizeInfoHash()` accepts
    exactly 40 hexadecimal characters and lowercases them.
    **WHY:** tracker titles are noisy and mutable. Title-based dedup can merge different torrents or keep the same
    torrent twice under different names.

24. **Do not “repair” an invalid hash by guessing.** Knaben drops rows without a valid 40-hex hash. Other adapters
    can leave `infoHash` empty while retaining a usable magnet from which a downstream owner may extract it.
    **WHY:** fabricating identity turns an uncertain search row into the wrong download. Preserve uncertainty until
    a real magnet/hash proves it.

25. **The search NAM must stay uncached unless the contract changes intentionally.** The app constructs this lane
    with `useCache=false`.
    **WHY:** stale seeder counts distort ranking, and cached transient failures can make an indexer appear dead
    after it recovers. This traffic is live availability state, not poster/media metadata.

26. **A build dependency on libtorrent does not mean these 13 files use libtorrent.** The shipping app requires
    the embedded download engine elsewhere, but the Knaben parser harness links only Qt Core/Network.
    **WHY:** use the smallest test seam possible. A tracker parser/request change should not require a live DHT,
    payload download, or seeding session to prove its deterministic contract. Note: the *cross-subsystem*
    `comic_torrents_search_harness` does link `colosseum_libtorrent` (it exercises the comics fan-out seam), so the
    "no libtorrent" property holds for the per-adapter Knaben harness only.

## 6. How to test it

Start with deterministic seams. Do not make public tracker availability the unit test.

**Knaben request + parser contract (no network):**

```bat
cmake --build native\build-msvc --target knaben_indexer_harness
native\build-msvc\knaben_indexer_harness.exe
```

This is the strongest current direct test of an individual generic adapter. It proves:

- relevance search (`search_type:"score"`);
- no `order_by`;
- unsafe/adult source filters;
- optional numeric category shape;
- canonical 40-hex hash requirement;
- origin-tracker provenance;
- returned-magnet preservation and synthesized-magnet fallback.

At this SHA `knaben_indexer_harness` is a CMake target but is **not registered in `tests/CMakeLists.txt`**, so a
normal `ctest` run does not prove it executed.

**Fan-out / partial-failure contract through a real consumer (no network):**

```bat
cmake --build native\build-msvc --target comic_torrents_search_harness
native\build-msvc\comic_torrents_search_harness.exe
```

This belongs to the Comics harness estate, but it is the best deterministic proof of the generic service seam:
the test injects one successful async indexer and one failing indexer, verifies useful rows survive partial
failure, and verifies replaced/cancelled handles cannot leak stale results into the newer search.

It is also not registered in the current CTest pilot set. It links `colosseum_libtorrent` (it exercises the comics
fan-out), so it is a heavier build than the pure Knaben parser harness.

**Knaben network/Cloudflare probe:**

```bat
cmake --build native\build-msvc --target knaben_probe
native\build-msvc\knaben_probe.exe "<lawful test query>"
```

Use this only when diagnosing reachability through Qt's actual TLS/HTTP stack. It is live-network and deliberately
not an ordinary unit test.

Important: at this SHA the probe still sends `order_by=seeders`, unlike production. A passing probe means “Qt got
real JSON with usable hashes,” **not** “production search relevance is correct.”

**Full current book-indexer smoke through production composition:**

```bat
set "COLOSSEUM_TORRENT_SEARCHTEST=<lawful test query>"
dev.bat
```

The app creates the same pinned, UA-stamped, uncached network manager used by production book/comic search, then
runs `TankorentSearchService::selfTest()` for `mediaType="books"`, `sourceFilter="all"`, limit 30.

Read the log for:

```text
[torrent-smoke] indexer returned <n> rows
[torrent-smoke] ERROR <source> <reason>
[hit] <sourceKey> ...
[torrent-smoke] finished
```

Do not download a result merely to prove search. Use a query/content case you are authorized to test.

The smoke has a 45-second process backstop while ExtraTorrents' own six-URL fallback can exceed that. A timeout
therefore requires source-by-source follow-up.

**Manual source-state checks Brotherhood should add to the adoption pass:**

1. Disable one source through the existing QSettings/Sources control and prove a normal federated search does not
   instantiate/query it.
2. Disable every normal source and prove the caller handles the empty `startSearch()` handle without waiting for a
   signal that will never come.
3. Start two concurrent handles and prove batches never cross.
4. Replace/cancel a search while replies are in flight and prove stale replies are ignored.
5. Let one source fail while another returns rows and prove the owning page still offers the good rows.
6. Confirm Books vs Audiobooks PirateBay requests carry `601` vs `102`.
7. Confirm ExtraTorrents/Torrents-CSV remain unscoped and that the owning ranker rejects obvious off-type rows.
8. Exercise ExtraTorrents with a fixture or captured HTML for both its modern and legacy title layouts; avoid
   making extto.org itself the only parser regression test.
9. Decide whether the `with_adult=1` ExtraTorrents fallback is still intended. If it is, ground-truth the downstream
   product-policy filter before calling the surfaced rows safe.
10. Exercise a valid-JSON response containing no usable hashes and verify health/UI wording does not overclaim
    successful result availability.
11. Ground-truth the Knaben dormant-source contract for `mediaType=""`, `"videos"`, and explicit `"knaben"`; fix
    either comments or routing if “never instantiated” is intended literally.
12. Verify the live source-status tooling distinguishes current `Unknown` from persisted last-success/error data.

**Do not use the embedded TorrentEngine download harnesses as the first proof of this guide.** They are valuable
for the sibling byte engine, but a DHT/download green does not prove source routing, tracker parsing, cancellation,
health classification, or federation semantics.

### What the current automated/source gates cannot prove

- that PirateBay, ExtraTorrents, or Torrents-CSV are reachable today;
- that extto.org's current HTML still matches the scraper;
- that a public tracker's category taxonomy has not changed;
- that a live indexer's returned rows are relevant to a particular edition/book beyond the owning ranker's rules;
- that persisted source health accurately describes the present network before a live refresh;
- that the ExtraTorrents adult-enabled fallback is compatible with the current product-content policy;
- that an unknown/empty media type should intentionally enable Knaben;
- that the 45-second production smoke is long enough for every configured source fallback;
- that a normalized magnet subsequently resolves to the expected payload;
- that libtorrent download/resume/seeding behavior works — that belongs to the sibling engine boundary.

Those are Brotherhood ground-truth items, not facts this source-read draft should upgrade to “verified.”

## Keeping this page honest

```bash
# refresh the generated index after changing a covered source file
python scripts/code_encyclopedia.py --paths docs/encyclopedia/torrent-search.paths \
  --output docs/encyclopedia/torrent-search-index.md --state docs/encyclopedia/torrent-search-state.json

# gate: fails when a covered file changed since its description was accepted
python scripts/code_encyclopedia.py --paths docs/encyclopedia/torrent-search.paths \
  --output docs/encyclopedia/torrent-search-index.md --state docs/encyclopedia/torrent-search-state.json --check

# after reviewing a changed description, ratify that file
python scripts/code_encyclopedia.py --paths docs/encyclopedia/torrent-search.paths \
  --output docs/encyclopedia/torrent-search-index.md --state docs/encyclopedia/torrent-search-state.json --accept <path>
```

When changing this subsystem:

1. keep Book/Comic/Tankoban rank/download files with their existing owners (`biblio.paths` / `comics.paths` /
   `tankoban.paths`);
2. keep `native/torrent/engine/` outside this manifest unless a separate ownership review explicitly assigns it;
3. if the Knaben allowlist/comment or media-routing logic changes, re-derive trap 7 from `buildIndexersFor()`;
4. if an indexer's adult-content behavior changes, update trap 9 and the content-policy note together;
5. only then regenerate and accept `torrent-search-index.md`.
