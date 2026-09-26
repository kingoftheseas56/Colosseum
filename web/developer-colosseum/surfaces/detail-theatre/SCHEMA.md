# `detail.theatre` record agreement — review with Claude before implementation

Subscribe with native route `params: { id: string, type: "movie"|"series", title?: string, cover?: string }`. `id` is the requested IMDb or `mal:`/`kitsu:` source identity; native resolves it and retains the original identity for Collection. Native validates the route and issues all media `Item` records. Every section uses the standard `{id,index,title,layout,state,error?,items}` envelope, including loading, empty, and error states. The following `data` records use `layout:"custom"`; all fields are JSON values and optional display text is an empty string when unknown.

| Section id | `data.schema` | Fields |
|---|---|---|
| `hero` | `theatre.hero` | `id`, `resolvedId`, `type`, `title`, `banner`, `cover`, `logo`, `year`, `genres: string[]`, `rating`, `runtime`, `synopsis`, `saved: boolean`, `notify: boolean`, `primaryLabel` |
| `facts` | `theatre.facts` | `rows: {label,value}[]` (only facts the native metadata actually has) |
| `seasons` | `theatre.seasons` | `selected: number`, `order: "seasons"|"absolute"`, `absoluteAvailable: boolean`, `rows: {number,label,count}[]`; season 0 is Specials and is last |
| `episodes` | `theatre.episodes` | `season: number`, `nextUpId`, `rows: {id,season,number,displayNumber,title,overview,thumbnail,airDate,duration,progress,watched,downloadState,sourceState}[]`; `id` is the native stream/unit id, and `progress` is 0..1 |
| `cast` | `theatre.cast` | `people: {name,role,image}[]` |
| `sources` | `theatre.sources` | `targetId`, `rows: {key,label,quality,provider,availability}[]`; `key` is opaque and local to the current feed generation |

`related` is a standard `rail` of native issued `Item` records; it has no `data`. A film has `seasons` and `episodes` in `empty` state. Source records contain no provider URL, torrent hash, file path, credential, or token.

`episodes` carries at most 100 rows per event. In absolute order its first window contains `nextUpId`; `hasMore:true` exposes later windows through `port.more`. Progress changes send at most one section event per second per subscription, and only the changed section is re-sent.

| Action | Payload | Completion rule |
|---|---|---|
| `detail.theatre.selectSeason` | `{id, season:number, order?:"seasons"|"absolute"}` | Resolve after selection is stored and the feed refresh is scheduled. |
| `detail.theatre.loadSources` | `{id, episodeId:string}` | Resolve after the `sources` section refreshes with `targetId` equal to this episode id. |
| `detail.theatre.play` | `{id, episodeId?:string, sourceKey?:string}` | Resolve after native player handoff succeeds or returns a plain error. Missing `episodeId` means movie/hero play. |
| `detail.theatre.markWatched` | `{id, episodeId:string, watched:boolean}` | Resolve after ProgressStore persists the mark. |
| `detail.theatre.download` | `{id, episodeId?:string, sourceKey?:string}` | Resolve after the download request is accepted or fails; report the actual job id in `result`. |
| `detail.theatre.collection` | `{id, saved:boolean, notify?:boolean}` | Resolve after CollectionStore persists the entry and notification preference. |

Every action checks the current native detail identity and rejects a stale or foreign `id`. A source pick is passed only by its opaque `sourceKey`; native resolves it against its current generation.
