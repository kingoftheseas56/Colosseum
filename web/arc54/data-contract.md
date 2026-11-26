# Browser adapter · v1
The same-origin API is /api/v1. Browser never receives the Worker guest lease or any admin key. Loopback-only serve.py is a bounded development adapter, not a production gateway. Run python3 web/arc54/serve.py and open /web/arc54/#/data. Explicit fixture mode runs without the adapter.
 
| Browser operation | Existing ownership | Status |
| --- | --- | --- |
| GET /capabilities | v1 metadata from compatible host | Available in dev host |
| GET /theatre/browse?tab=discover or movies or shows | Content API POST /v1/catalogue/imdb/titleCatalog | Bounded real catalogue; not native curation |
| GET /theatre/title/:id | Content API POST /v1/catalogue/imdb/titlePresentation | Title/type only; no synopsis, seasons, source list or art |
| Anime / personal Library | Native provider/account/progress owners | No compatible hosted route in this slice |
| Accounts, collections, Vault, downloads, progress | Native/server owner | Host-required; no shadow state |
| Playback and readers | Native engines | Deferred |
 
Pinned contracts: server/content-api/src/index.ts and catalogue-semantic.ts. The dev bridge uses a short-lived guest read lease and fixed operations; callers cannot supply an upstream URL, API path, raw RPC arguments, or authorizations. Failure is never replaced with fixture data. Requests abort on navigation.
 
Refresh compatible capability metadata on window focus without changing route. Consumers update affected controls, not the whole view. Unknown major schemas fail explicitly. A new bundle version offers an explicit reload; record route, focus and scroll in browser history before reloading the same URL. A deployed host must version asset filenames, retain old bundles across rollout, and roll back assets and manifest together. Production rollout and rollback were not run.
