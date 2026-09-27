# page.downloads schema

Owner: W2-4 (GLM). Source parity authority: `qml/DownloadsPage.qml` + `qml/BackgroundActivitySection.qml`.

Route: `{ name:"page", page:"downloads" }` (shared TopBar door / `open.native` door "downloads" lands here, CONTRACT §12.4).
Feed: `page.downloads`. Params (optional, both strings, native-validated):

```js
{ ledgerWorld: "tankoban"|"biblio"|"theatre"|"", ledgerKey: "<series key>" }
```

The web surface resubscribes with the open ledger (the QML's `toggleLedger`); empty values = no
ledger open. All grouping, season derivation, counts and live/attention classification happen
native (QML `groupJobs` / `computeLedgerSeasons` / `isLiveState` moved). Exact display copy is
composed web-side from these fields with the QML's own templates.

All sections use `layout:"custom"` and are drawn by the surface.

## `downloads.header` (index 0) — `downloads.header`
`{ items, bytes, tankoban, biblio, theatre, audiobook, active, attention }` — QML:470-489 totals
(LocalDownloads.totals + audiobook done bytes/count; `active` = live jobs + live audiobooks).

## `downloads.now` (index 1) — `downloads.now`
`{ liveCount, attentionCount, groups: [...], audiobookActive: [...] }`

group (QML groupJobs): `{ key, world, single, title, groupUnit, count, doneCount, liveCount,
received, total, hasKnownTotal, ratio, speed, eta (sec, −1 none), season, seriesTitle,
coolResumeAt (epoch ms, 0 none — group rows share the QML single-row cooldown display),
rows: [...] }`

row (LocalDownloads.activeJobs() record, verbatim fields the page reads + routing identity):
`{ id, title, world, state, detail, error, received, total, speed, etaSec, ratio, episode, badge,
season, seriesTitle, groupKey, groupUnit, canPlay, canRetry, canPause, canResume, canCancel,
canDismiss }` — `url`/`partPath`/`headers`/`path`/`art`/`kind` never leave native; `playArriving`
resolves them by `{world,id}`.

audiobookActive (Audiobooks.activeDownloads()): `{ id (pairKey), state, title, author, error,
received, total }`.

## `downloads.activity` (index 2) — `downloads.activity`
`{ rows: [{ id, title, stage, progress (0..1), paused, canPause }] }` — BackgroundActivityRegistry
(BackgroundActivitySection.qml). Empty rows = section hidden.

## `downloads.remote` (index 3) — `downloads.remote`
`{ items: [<LocalDownloads.availableElsewhere() row verbatim: title, subtitle, seriesTitle,
canRedownload, world, id, …>] }` — the row IS the redownload identity; the action re-sends
`{world,id}` and native re-resolves the item.

## `downloads.shelf.tankoban` / `.biblio` / `.theatre` (indexes 4-6) — `downloads.shelf`
`{ world, title, unit, series: [{ key, title, art, itemCount, bytes, unitText }] }`
(LocalDownloads.series(world), newest-first). When `params.ledgerWorld/ledgerKey` selects this
shelf, it additionally carries:

`{ ledger: { key, seasons: [{ season, items, bytes, newest, arriving, defaultOpen }] , flat: [...] } }`
— `seasons` non-empty only for theatre episode ledgers (QML computeLedgerSeasons incl. the live
arriving cross-reference); otherwise `flat` holds LocalDownloads.items(world,key) rows verbatim
(`{ id, title, subtitle, seriesTitle, world, kind, bytes, addedAt, missing, path, art, author,
bookId, packRole }` — paths stay opaque to web; `openItem` routes by `{world,id}` native-side).
`defaultOpen` marks the newest season (QML's first-open default).

## `downloads.audiobooks` (index 7) — `downloads.audiobooks`
`{ done: [{ id, title, author, bytes, addedAt, missing, fileCount, bookPath (bool-ready flag only:
`bookReady`), bookId }], visibleWhenIdle }` — `bookPath` itself never crosses; the boolean
`bookReady` carries the QML's "ready to listen" gate. `visibleWhenIdle` = QML:1412 visibility rule.

## State rule
`state:"loading"` for the initial paint; `ready` when built; `error` with plain words when the
read-model is unavailable. Progress updates arrive as `section` events on `downloads.now`
(bridge coalesces ≤1/s per CONTRACT §3.2).

## Actions (`page.downloads.<verb>`, ActionRegistry)
| Action | Payload | Completes when | QML source |
|---|---|---|---|
| `pause` / `resume` / `retry` | `{world,id}` | LocalDownloads call returned | :684,:714-716,:924 |
| `groupToggle` | `{world, groupKey, pause:true\|false}` | every pausable/resumable row was paused (reverse order) / resumed | :708-717 |
| `cancel` | `{world,id}` | job cancelled | :949 |
| `cancelGroup` | `{world, groupKey}` | every cancelable row of the CURRENT group cancelled (re-resolved at commit) | :764-780 |
| `dismissFailure` | `{world,id}` | failure dismissed | :752,:936 |
| `remove` | `{world,id}` | remove() returned; result `{success,message}` maps to plain words | :1804 |
| `playArriving` | `{world,id}` | the live session opened (native resolves url/part) | Main.qml:2056 |
| `openItem` | `{world,id}` (ledger row) | the destination opened (video/book/comic/manga routing) | Main.qml:2019 |
| `redownload` | `{world,id}` (remote row) | redownload() queued; result mapped | Main.qml:4324 |
| `openAudiobook` | `{id}` | the reader opened with audio | Main.qml:2089 |
| `cancelAudiobook` | `{id}` | partial files deleted | :1039 |
| `dismissAudiobookFailure` | `{id}` | dismissed | :1024 |
| `deleteAudiobook` | `{id}` | deleteAudiobook() returned; result mapped | :1556 |
| `background.pause` / `background.resume` | `{id}` | request issued | BackgroundActivitySection.qml:68-70 |

`openWorld` (empty-lane CTA, QML:730/:1197) is web-internal router navigation (§3.6), not an
action. No secrets exist on this page; no file path or URL crosses into feed data.
