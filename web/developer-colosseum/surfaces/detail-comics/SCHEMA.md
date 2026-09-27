# detail-comics schema

`detail.comic` is the W2-8 feed. Route params are native-issued. The accepted identities are `gc:<slug>`, `gcd:<id>`, `locg:<id>`, `comic:archives`, `gcbox:<tagId>`, and `publisher:<percent-encoded-name>`. Optional `view` accepts native-issued `sort` (`new`, `old`, `az`) and the one web-authored field allowed by Contract §13.1, `query`.

All non-media records use `layout:"custom"` and `Section.data.schema`. Provider URLs, signed download URLs, archive paths and reader store objects never enter feed data.

## `comic.hero`

Fields: `id`, `title`, `cover`, `publisher`, `year`, `synopsis`, `sourceLabel`, `saved`, `releaseCount`, `resumeUnitId`, `resumeUnitLabel`.

`resumeUnitId` is the native Progress record for the reader series identity. The web sends only that id back to `detail.comic.read`.

## `comic.filter`

Fields: `query`, `matchCount`.

The input is the only place web authors `view.query`. Native filters and recomputes the release window.

## Sort choices

Section `sort` uses Contract `Choice` records, not custom data. Each target is `{view:{sort}}`; web merges the native-issued patch and resubscribes.

## `comic.releases`

Fields: `scope`, `windowStart`, `total`, `rows`.

Each row: `id`, `title`, `cover`, `year`, `sizeMB`, `date`, `format`, `pages`, `description`, `group`, `downloadState`, `available`, `readingProgress`.

`group` preserves the QML grouping: GetComics/GCD split into `Collected editions` and `Issues`; curated LOCG rows use their format. A window contains at most 100 rows. `hasMore` asks native for the next window. The surface accumulates windows only while `scope` is unchanged.

## `comic.archiveHero`

Fields: `title`, `subtitle`.

## `comic.archiveBoxes`

Fields: `rows`. Each row: `id` (`gcbox:<tagId>`), `title`, `count`, `tag`, `tagId`, `cover`.

## `comic.archiveIndexHero`

Fields: `title`, `count`, `seriesCount`.

## `comic.archiveSeries`

Fields: `rows`. Each row: `id` (`gc:<slug>`), `title`, `count`, `frequency`, `cover`.

## `comic.publisherHero`

Fields: `title`, `count`.

## `comic.publisherSeries`

Fields: `rows`, `windowStart`. Each row: `id` (`locg:<id>`), `title`, `cover`, `year`, `publisher`. The section may page with `hasMore`.

## Actions

- `detail.comic.navigate {id,targetId}` validates the visible native row and returns a native-issued `detail.comic` route.
- `detail.comic.collection {id,saved}` adds/removes the series in Tankoban Collection.
- `detail.comic.download {id,unitId}` validates the visible row, keeps the provider URL native-private, and queues `ComicDownloader`.
- `detail.comic.read {id,unitId}` is the `ComicSeries.qml` consumption-intent behavior: if needed it acquires once, waits for completion, then delegates to the native comic reader.
- Shared-core dependency `detail.comic.openReader` is tracked in `REQUEST-W2-8-COMIC-CORE-SEAMS.md`; W2-8 never sends the reader chain through web state.

## `comic.sources`

This is the web replacement for `ComicTorrentSourcesPage.qml` and `ComicTorrentArchivePicker.qml`, surfaced only for idle LOCG collected editions.

Fields: `open`, `issueId`, `editionTitle`, `cover`, `identityLine`, `query`, `loading`, `complete`, `confirmingWeak`, `selectionState`, `pendingTitle`, `rows`, `archiveFiles`, `missingIssues`, `combinedCount`, `error`.

`selectionState` is one of `results`, `inspecting`, `ambiguous`, `incomplete`, `combined`.

Public source rows contain only display/ranking evidence: `id`, `title`, `sizeText`, `seeders`, `leechers`, `sourceName`, `confidence`, `matchTier`, `evidence`, `archiveHint`, `coverage`, `uploader`, `trustTier`. The magnet URI and info hash remain native-private.

Archive-choice rows expose `index`, `name`, `extension`, `sizeText`; native validates the selected index before resuming acquisition.

Additional source actions:
- `detail.comic.openSources {id,unitId}`
- `detail.comic.searchSources {id,query}`
- `detail.comic.selectSource {id,sourceId}`
- `detail.comic.confirmWeakSource {id}`
- `detail.comic.cancelWeakSource {id}`
- `detail.comic.chooseArchive {id,fileIndex}`
- `detail.comic.rejectIncomplete {id,manual}`
- `detail.comic.confirmCombined {id}`
- `detail.comic.rejectCombined {id}`
- `detail.comic.closeSources {id}`

The action handlers call the existing `ComicDownloader` torrent APIs. Provider URLs, magnets, info hashes, filesystem paths and native reader chains never enter web state.
