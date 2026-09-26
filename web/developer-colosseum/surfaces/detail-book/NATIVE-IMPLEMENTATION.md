# W2-9 native implementation notes

Temporary GitHub-side implementation record while the shared local checkout is unavailable. The final implementation lives only in `native/webui/feeds/BookDetailFeed.cpp`.

## Threading shape

Use the current Arc 54 two-pass feed seam:

1. `capture` (GUI thread)
   - no provider HTTP
   - copy only route/runtime facts that are already safe
2. `build` (worker)
   - Apple ebook metadata
   - LibGen scrape + renderable-format cascade
   - AudioBookBay search + relevance filtering
   - return the first complete provider-shaped page
3. `enrichCapture` (GUI thread)
   - read the rich book from `baseSections`
   - query `Progress`, `Collection`, `Books`, `BookTorrents`, `Audiobooks`, `Reader2Bridge`
   - start/adopt the one app-lifetime `BookTorrents` search
   - snapshot local-copy and download states
4. `enrich` (worker)
   - merge GUI snapshots into the page records
   - no QObject access

Provider results are cached per `subscriptionId:generation:id`, so download/progress invalidations do not refetch Apple/LibGen/ABB while reopening the page still performs a fresh source pass.

## Feed sections

All sections are `layout:"custom"`.

- `hero` -> `book.hero`
- `torrents` -> `book.torrents`
- `editions` -> `book.editions`
- `audiobooks` -> `book.audiobooks`
- conditional `read-choice` -> `book.readChoices`

Use real feed states in native. `surface.js.draft` carries the feed state into `data.feedState` before calling the custom renderer so the QML-specific loading/empty copy is drawn instead of the shared skeleton.

## Native owners

No second download/reader authority.

- ebook bytes: `BookDownloader`
- ranked ebook torrents: `BookTorrents`
- audiobook bytes/pairing: `AudiobookDownloader`
- torrent engine warm-up: `StreamServer`
- local reader identity: `Reader2Bridge::bookKey`
- reading destination: delegated existing native `open` resume action -> `Main.qml::openBookSession`
- Collection: current profile `CollectionStore`
- Continue/read progress: current profile `ProgressStore`

## Reactivity

Register `FeedRegistry::OwnerSignal` binders for the current context-property owners.

Profile bound:
- `Progress.changed`
- `Collection.changed`

App lifetime download/search owners:
- `Books.resolving/progress/finished/failed/removed`
- `BookTorrents.resultsReady/searchFinished/resolving/progress/finished/failed/removed`
- `Audiobooks.resolving/progress/finished/failed/removed`

`resultsReady` stores the current ranked torrent rows for the active book before requesting a refresh. `searchFinished` clears its loading flag.

The explicit owner-signal list is important: `ColosseumWebBridge::storeChanged` does not currently refresh `detail.book` for Progress/Collection changes by itself.

## Foreground Read state

Keep page-local intent in native runtime state, not in web.

States:
- `lookup`
- `choice`
- `books`
- `torrent`

Order copied from `BiblioBook.qml:437-486`:

1. already-local copy
2. exactly one already-in-flight tracked copy -> adopt
3. multiple in-flight copies -> source choice
4. wait for source inventories to settle
5. preferred LibGen tracked edition
6. first ranked readable torrent
7. remaining tracked-choice fallback
8. plain failure

A new acquisition first removes the previous local copy for this book. Adopting an already-running acquisition never does.

The original `detail.book.read` action remains pending until Reader2 is actually delegated, a plain failure occurs, or the foreground intent is cancelled. Source-choice actions resolve independently and immediately.

## Leaving the page

`surface.js.draft` sends `detail.book.cancelRead` on route replacement/unmount.

Cancel only retires the foreground auto-open intent. It never cancels the native download. Resolve the original pending Read action successfully with a cancelled result so leaving the page does not create an error toast.

This matches `BiblioBook.qml` Back/book-change invalidation.

## Reader handoff

Do not add a Main.qml action.

Delegate the existing `open` action with a Biblio Continue-shaped resume payload:

- `intent: "resume"`
- `item.world: "Biblio"`
- `item.kind: "book"`
- `ref.continueGroupKey` non-empty
- `ref.resume.path` local file
- `ref.resume.book` rich book map

Current `Main.qml` routes that payload through `resumeContinue` -> `openBookSession` -> Reader2.

## Collection identity

Preserve the QML `pairKey(title, author)` normalization, including:
- parenthetical stripping
- abridged/unabridged stripping
- `: A Novel`-style marketing-tail stripping
- `&` -> `and`
- punctuation/whitespace fold

Collection entry:
- world `biblio`
- id = pairKey
- type `book`
- title/cover
- payload.book = rich book

## Provider parity

### Apple

Copy `BiblioApi.fullBook`:
- title/author/year/genres
- high-res cover transform
- marketing-noise cleanup
- high-precision tagline split
- synopsis clamp
- rating/rating count

Prefer the requested numeric Apple id when present in search results, otherwise keep the old first-useful-title behavior.

### LibGen

Copy `BiblioApi.searchLibgen`:
- libgen.li
- 25 s timeout
- one retry
- cache-buster
- renderable tiers: EPUB > MOBI/FB2/AZW3 > PDF
- hide unreadable formats
- max 12 rows
- first surviving row is `best`

Rows carry a `key/sourceKey` used as an action handle. Web does not interpret it.

### AudioBookBay

Copy `AbbApi.js`:
- lowercase search term
- exact `<div class="post">` blocks to exclude honeypots
- relevance-word gate
- resolve selected slug to 40-hex info hash
- prewarm the top result without blocking page rendering

## Actions

- `detail.book.read {id}`
- `detail.book.chooseRead {id, transport, sourceKey}`
- `detail.book.cancelRead {id}`
- `detail.book.downloadEdition {id, sourceKey}`
- `detail.book.downloadTorrent {id, sourceKey}`
- `detail.book.downloadAudiobook {id, sourceKey}`
- `detail.book.collection {id, saved}`

Every action first proves `bridge.detailActive("detail.book", id)`. Source actions also prove the current source row still exists.

## Promotion sequence

1. Finish native file as `BookDetailFeed.cpp.draft`.
2. Compile it out-of-band or rename only immediately before the locked milestone build.
3. Run `native\\build-target.bat colosseum`.
4. When native feed compiles, promote `surface.js.draft` to `surface.js` in the same integration window so the shell never advertises a feed that does not exist.
5. Run web selftest + `dev/e2e/walk.mjs`.
