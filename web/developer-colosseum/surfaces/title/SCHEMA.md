# `detail.theatre` record agreement

Subscribe with native route `params: { id: string, type: "movie"|"series", title?: string, cover?: string }`. `id` is the requested IMDb or `mal:`/`kitsu:` source identity; native resolves it and retains the original identity for Collection. Native validates the route and issues all media `Item` records. Every section uses the standard `{id,index,title,layout,state,error?,items}` envelope, including loading, empty, and error states. The following `data` records use `layout:"custom"`; all fields are JSON values and optional display text is an empty string when unknown.

| Section id | `data.schema` | Fields |
|---|---|---|
| `hero` | `theatre.hero` | `id`, `resolvedId`, `type`, `kind:"movie"|"series"|"anime"`, `title`, `banner`, `cover`, `logo`, `year`, `genres: string[]`, `rating`, `scores: {provider:"mal"|"imdb",value:number,scale:10,votes?:number}[]` (MAL first), `runtime`, `synopsis`, `saved: boolean`, `notify: boolean`, `watchedMark: -1|0|1`, `primaryLabel`, `primaryTargetId` |
| `seasons` | `theatre.seasons` | `selected: number`, `order: "seasons"|"absolute"` (shown as Aired/Absolute), `absoluteAvailable: boolean` only when AnimeOrder has a complete mapping for this anime, `totalCount: number`, `rows: {number,label,count,from?,to?}[]`; season 0 is Specials and is last. DVD and TVDB Absolute require a separate TVDB data source and are not offered by this feed. |
| `episodes` | `theatre.episodes` | `season: number`, `nextUpId`, `windowStart: number`, `totalCount: number`, `rows: {id,season,number,displayNumber,title,overview,thumbnail,airDate,duration,progress,watched,downloadState,downloadProgress,sourceState}[]`; `id` is the native stream/unit id, `windowStart` is this window's zero-based position, and progress values are 0..1 |
| `cast` | `theatre.cast` | `people: {name,role,image}[]` |
| `related` | standard `rail` | Native issued `Item` records; no `data` field. |
| `facts` | `theatre.facts` | `rows: {label,value}[]`; only present Status, Released, Runtime, Genres, MAL and IMDb values, with score providers named. |
| `sources` | `theatre.sources` | `targetId`, `rows: {key,label,quality,provider,availability}[]`; `key` is opaque and local to the current feed generation |

A film has `seasons` and `episodes` in `empty` state. Episode `watched` is a read-only indicator from the exact ProgressStore entry: `entry.watched === true` or progress ratio ≥ 0.85, as in `TheatreSeries.qml`. Source records contain no provider URL, torrent hash, file path, credential, or token.

`episodes` carries at most 100 rows per event. In absolute order its first window contains `nextUpId`; `port.more` pages forward and then wraps to earlier windows so every episode remains reachable. `windowStart` is the window's position in the chronological list. Progress changes send at most one section event per second per subscription, and only the changed section is re-sent.

| Action | Payload | Completion rule |
|---|---|---|
| `detail.theatre.selectSeason` | `{id, season:number, order?:"seasons"|"absolute"}` | Resolve after selection is stored and the feed refresh is scheduled. |
| `detail.theatre.loadSources` | `{id, episodeId:string, intent?:"play"|"download"|"season"}` | In play intent, open a completed local download first or an in-progress download with a resolved URL; return `result.openedPlayback:true`. Otherwise resolve after the `sources` section refreshes with `targetId` equal to this episode id. Download and season intents always open the picker. |
| `detail.theatre.play` | `{id, episodeId?:string, sourceKey?:string}` | Resolve after native player handoff succeeds or returns a plain error. Missing `episodeId` means movie/hero play. |
| `detail.theatre.download` | `{id, episodeId?:string, sourceKey?:string}` | Resolve after the download request is accepted or fails; report the actual job id in `result`. |
| `detail.theatre.downloadSeason` | `{id, season:number, sourceKey?:string}` | Queue each missing episode in season order; a picked torrent hash is pinned to the season jobs, while no key uses automatic per-episode resolution. Resolve with the accepted count. |
| `detail.theatre.collection` | `{id, saved:boolean, notify?:boolean}` | Resolve after CollectionStore persists the entry and notification preference. |
| `detail.theatre.markWatched` | `{id, watched:boolean}` | Reject a stale title. When marking watched, forget its Continue progress, then persist the manual watched mark through ProgressStore; when marking unwatched, persist the manual unwatched mark. Resolve after the feed refresh is scheduled. |

Every action checks the current native detail identity and rejects a stale or foreign `id`. A source pick is passed only by its opaque `sourceKey`; native resolves it against its current generation.
