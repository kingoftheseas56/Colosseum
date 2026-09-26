# `detail.manga` record agreement — review with Claude before implementation

Subscribe with native route `params: { id: string, title?: string, cover?: string }`. `id` is the canonical `mal:<number>` or native series identity. Native validates it; ambiguous title matches yield an honest empty state. Every section uses the standard `{id,index,title,layout,state,error?,items}` envelope, including loading, empty, and error states. The following `data` records use `layout:"custom"`; all fields are JSON values and optional display text is an empty string when unknown.

| Section id | `data.schema` | Fields |
|---|---|---|
| `header` | `manga.header` | `id`, `malId`, `title`, `banner`, `cover`, `author`, `status`, `year`, `synopsis`, `genres:string[]`, `score`, `saved:boolean`, `primaryLabel`, `primaryState:"open"|"get"|"search"` |
| `modes` | `manga.modes` | `selected:"volumes"|"chapters"`, `chapterEnabled:boolean`, `languages:{code,label,providerCount}[]`, `selectedLanguage` |
| `volumes` | `manga.volumes` | `rows:{id,number,title,cover,startChapter,endChapter,owned,downloadState,progress,read}[]`; rows retain the QML shelf order |
| `chapters` | `manga.chapters` | `sourceSeriesId`, `language`, `rows:{id,number,title,cover,sourceLabel,downloadState,progress,read}[]`; chapter identity includes its provider and language |
| `sources` | `manga.sources` | `rows:{key,label,language,availability}[]`; `key` is opaque and local to the current feed generation |
| `downloads` | `manga.downloads` | `rows:{unitKind:"volume"|"chapter",unitId,state,done,total,error}[]` |

Chapter and volume records contain no page URL, provider endpoint, local path, credential, or token. `chapters` reports a plain error when Tankoyomi is disabled or the selected language has no source; `volumes` reports empty when the catalogue has no proven shelf.

| Action | Payload | Completion rule |
|---|---|---|
| `detail.manga.selectMode` | `{id, mode:"volumes"|"chapters", language?:string}` | Resolve after the mode/language selection is applied and the feed refresh is scheduled. |
| `detail.manga.read` | `{id, unitKind:"volume"|"chapter", unitId:string}` | Resolve after the native reader handoff succeeds; if a download is required, resolve only after readiness or return a plain error. |
| `detail.manga.markRead` | `{id, unitKind:"volume"|"chapter", unitId:string, read:boolean}` | Resolve after ProgressStore persists the mark. |
| `detail.manga.download` | `{id, unitKind:"volume"|"chapter", unitId:string, sourceKey?:string}` | Resolve after the real download request is accepted or fails; report the job id in `result`. |
| `detail.manga.collection` | `{id, saved:boolean}` | Resolve after CollectionStore persists the entry. |

Every action checks the current native detail identity and rejects a stale or foreign `id`. Native resolves any `sourceKey` within the current feed generation.
