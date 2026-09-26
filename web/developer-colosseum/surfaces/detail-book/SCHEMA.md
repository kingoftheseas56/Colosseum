# detail.book schema

Owner: W2-9. Source parity authority: `qml/BiblioBook.qml`.

The route is:

```js
{ name: "detail", kind: "book", params: { id, title?, cover?, world? } }
```

The native feed is `detail.book`. Every section uses `layout:"custom"`; the surface renders it through `ctx.custom(section)`.

## Sections

### `hero`

`data.schema = "book.hero"`

Fields:

- `id` string
- `title` string
- `author` string
- `cover` string
- `year` string
- `genres` array of strings
- `genreLine` string
- `tagline` string
- `synopsis` string
- `rating` number
- `ratingCount` number
- `saved` bool
- `local` bool
- `progress` number in [0,1], or negative when absent
- `primaryLabel` exact Read CTA copy from `BiblioBook.qml`
- `primaryStatus` exact status copy from `BiblioBook.qml`
- `primaryEnabled` bool
- `readError` string

Actions:

- `detail.book.read { id }`
- `detail.book.collection { id, saved }`

Reading never moves into web. A successful foreground Read ultimately delegates to the existing native Reader2 path.

### `torrents`

`data.schema = "book.torrents"`

Fields:

- `loading` bool
- `rows` array
- `count` integer
- `collapsedCount` integer, currently 5

Each row:

- `sourceKey` opaque string used only in the action
- `title` string
- `seeders` integer
- `size` string
- `pack` bool
- `state` one of `none|resolving|queued|downloading|done|failed`
- `received` number
- `total` number

Action:

- `detail.book.downloadTorrent { id, sourceKey }`

The native feed preserves `BookTorrentRanker` ordering. The web surface never re-ranks.

### `editions`

`data.schema = "book.editions"`

Fields:

- `loading` bool
- `rows` array
- `count` integer

Each row:

- `sourceKey` opaque string used only in the action
- `format` string
- `formatLabel` string
- `size` string
- `source` string
- `year` string
- `language` string
- `meta` exact display metadata string
- `best` bool
- `state` one of `none|resolving|queued|downloading|done|failed`
- `received` number
- `total` number
- `external` bool

Action:

- `detail.book.downloadEdition { id, sourceKey }`

The feed performs the same renderable-format cascade as `BiblioApi.searchLibgen`.

### `audiobooks`

`data.schema = "book.audiobooks"`

Fields:

- `loading` bool
- `rows` array
- `activeSlug` string
- `state` one of `none|resolving|downloading|done|failed`
- `received` number
- `total` number
- `local` bool

Each row:

- `sourceKey` opaque string used only in the action
- `slug` string display identity
- `format` string
- `size` string
- `language` string
- `posted` string

Action:

- `detail.book.downloadAudiobook { id, sourceKey }`

The native side retains title/author pairing, AudioBookBay relevance filtering, info-hash resolution, Stream prewarm, and `AudiobookDownloader`.

### `read-choice`

Present only while a foreground Read needs the user to pick between tracked sources.

`data.schema = "book.readChoices"`

Each row:

- `transport` = `books` or `torrent`
- `id` opaque source identity
- `label` exact source label
- `meta` exact source metadata

Actions:

- `detail.book.chooseRead { id, transport, sourceKey }`
- `detail.book.cancelRead { id }`

This section renders as a focus scope over the page. Escape cancels the Read intent. Tab is contained. Home/End move to the first/last focusable control, matching the QML modal.

## State rule

The visual surface owns the QML-specific loading/empty copy, so the feed should include `data.loading` and rows rather than relying on the shared skeleton renderer for these source panels. Plain failure text may additionally ride in `section.error`.

## Security / ownership

No provider credential, raw account token, or private filesystem authority is sent to web. Download and reader ownership remains native. `sourceKey` values are opaque action handles; the browser does not inspect native source payloads.
