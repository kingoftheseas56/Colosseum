# Connections feed schemas

Feed: `page.connections`, with empty params. Records are `layout: "custom"`, contain no media `Item`, and use the shared four section states. `loading` carries no `data`; `error` carries a plain `error`; `empty` is used only for an absent native tracker owner or an empty provider catalogue. All other data is a GUI-thread capture of `TrackerSyncCenterModel`'s safe presentation projection. The worker copies and arranges it; it never calls the model.

| Schema | Fields | Source |
|---|---|---|
| `connections.header` | `revision` (current owner revision), `aggregate` (status, counts, `canSyncAll`), `globalSettings`, `summary` display text | `TrackerSyncCenterModel::aggregateState`, `globalSettings` |
| `connections.native` | `name`, `statement`, `detail`, `badge` display text | QML native library card |
| `connections.relay` | `summary` display text, `providers` safe connection cards | `connectedTrackers`, aggregate |
| `connections.attention` | `unresolvedCount`, `attentionProviderCount` | aggregate |
| `connections.connected` | `providers` safe connection cards | `connectedTrackers` |
| `connections.stremio` | `name`, `capabilities`, `status`, `panelAvailable` | Stremio status seam, when supplied; until then status says unavailable and panel action remains disabled |
| `connections.catalogue` | `providers` safe catalogue cards | `catalogue` |

Connection cards carry only public provider key/name, generic account label, status, last-success time, counts, verified capability keys, and native-issued booleans. The feed omits remote account IDs, credentials, canonical keys, provider payloads, and opaque backing-store paths. The web surface does not infer a provider capability. Provider icons are static bundled assets selected from a fixed provider-key table in the surface.

The surface-owned `icons/` copies the approved Colosseum, Stremio and tracker SVGs from the repository's `assets/icons/`. Keeping these under `developer-webui/surfaces/connections/` lets the same relative URL resolve in the browser and in `qrc:///developer-webui/`; the source assets remain the visual reference and provider/icon clearance remains a release gate.

Later action slices will extend this schema with dossier and review records. Native owns every choice and validates the current revision before mutation.

Action `page.connections.dossier` takes `{providerKey, revision}` and returns `{revision, dossier, deliveryRows, importReviews}`. `global` is the native global-settings projection presented as a dossier; other provider keys come from the feed's cards. The optional preference actions take a native-issued revision and boolean. They return a new revision and plain notice only after the native model finishes its persistence decision. The surface re-subscribes after acceptance and reloads the open dossier, preserving the native model as the owner.

## Page actions

Every mutation carries the currently displayed model revision. Native checks it again before changing anything. Failures return `{ok:false,error}` in plain words; the shell shows that error. A successful mutation returns `{ok:true,result:{revision,notice}}`. Import confirmation and imported-data removal complete only after their asynchronous native operation settles.

| Action | Payload | Successful result |
|---|---|---|
| `page.connections.globalSetting` | `{key,enabled,revision}` | Saved global preference |
| `page.connections.providerSetting` | `{providerKey,setting,enabled,revision}` | Saved provider preference |
| `page.connections.syncAll` | `{revision}` | Native request accepted |
| `page.connections.importReview` | `{batchId,revision}` | Public `TrackerSyncCenterModel::importReviewSnapshot` |
| `page.connections.resolveImport` | `{batchId,itemId,choice,revision}` | One native-issued choice saved |
| `page.connections.resolveImportMany` | `{batchId,itemIds,choice,revision}` | Common choice saved for selected items |
| `page.connections.confirmImport` | `{batchId,revision}` | Native import application settled |
| `page.connections.titleMatches` | `{batchId,itemId,query,revision}` | Up to 50 public candidate handles |
| `page.connections.confirmTitleMatch` | `{batchId,itemId,candidateId,revision}` | Exact match saved; import choice remains separate |
| `page.connections.beginExport` | `{providerKey,revision}` | Current native export review with public item handles |
| `page.connections.confirmExport` | `{reviewId,itemIds,revision}` | Selected native changes queued |
| `page.connections.diagnose` | `{providerKey,revision}` | Native-issued dossier/focus route |
| `page.connections.disconnect` | `{providerKey,choice,revision}` | Connection action settled; uncertain outcomes retained |
| `page.connections.removeImported` | `{providerKey,revision}` | Eligible imported Progress and source History evidence removal settled |

`page.connections.titleMatches` is still gated by the native title-matching readiness seam described in `REQUEST-CONNECTIONS-SHARED-SEAMS.md`. Stremio status and its panel handoff also await that shared seam. The web page neither invents an authentication capability nor treats a provider catalogue card as a connected account.
