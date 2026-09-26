# Update page schema

Feed: `page.update`. Params: none.

The feed emits one `layout:"custom"` section, `update.chronicle`, with `Section.data.schema = "update.state"`.

`update.state` fields:

- `state: number`, `stateName: string`
- `installedVersion: string`, `latestVersion: string`
- `updateAvailable: boolean`, `unseenUpdate: boolean`
- `receivedBytes: number`, `totalBytes: number`, `progress: 0..1`
- `progressVisible: boolean`, `progressIndeterminate: boolean`, `progressText: string`
- `statusText: string`, `metadataText: string`
- `release: object`
- `chapters: array` of verified release-highlight records
- `primary: { label, action, visible, enabled }`

The release/highlight records come from `UpdateService`; updater secrets, signing material and cache internals never enter feed data.

Page actions, all with empty payloads:

- `page.update.seen` marks the offered release seen when the web page opens.
- `page.update.check` runs an update check.
- `page.update.download` starts, resumes or retries the installer download.
- `page.update.pause` pauses an active download.
- `page.update.install` launches the verified ready installer and requests restart.

The feed binds `UpdateService::changed` through `FeedRegistry::ownerSignals`; updater changes refresh the existing subscription directly, without polling or a page action.

Action failures are returned in plain human language.
