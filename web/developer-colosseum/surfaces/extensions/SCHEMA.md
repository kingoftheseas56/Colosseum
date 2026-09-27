# Extensions surface schema

Feed: `page.extensions`

Params:
- `view`: `chain | house | explore`; defaults to `chain`.
- `query`: optional user-visible search string, maximum 200 characters.

All configured-instance identity is opaque. `token` is either the stable local extension id or a one-way hash of a remote transport URL. A configured transport URL, token-bearing URL, password, or other secret is never emitted in feed data.

## `extensions.chain`

`Section.data = { schema:"extensions.chain", installedCount, enabledCount, worlds }`

Each world is `{ key, title, sourceCount, rows }`. Rows are ordered exactly as native asks them. A row contains `token, id, name, enabled, core, catalogue, source, house, worlds, configurable, configurationRequired`, plus optional `description, logo, rank, tie`. Catalogues are unnumbered and precede sources. Sources are numbered after the divider.

## `extensions.house`

`Section.data = { schema:"extensions.house", installedCount, enabledCount, rows }`

Rows are Colosseum-owned `colosseum://well/*` sources. Each row contains the common chain row fields plus `job` and a short description. The web filter only chooses which already-native rows are visible.

## `extensions.explore.universes`

This custom section uses normal contract `Item` records in `Section.items`. Each item is `world:"Colosseum", kind:"universe"` and opens through `env.open`. `Section.data` contains only `schema, installedCount, enabledCount, label`.

## `extensions.explore.jobs`

`Section.data = { schema:"extensions.explore.jobs", installedCount, enabledCount, rows }`

Each job row is `{ name, count }`. The count is derived natively from the current community response plus currently installed House wells.

## `extensions.explore.cards`

`Section.data = { schema:"extensions.explore.cards", installedCount, enabledCount, rows }`

Community rows contain `id, name, description, kind, installUrl, stars, installed, configurable, configurationRequired`, plus optional public `logo`. `installUrl` comes from the public community registry or official fallback. Private configured instance URLs never use this schema.

## Actions

- `page.extensions.enable { token, enabled }`
- `page.extensions.remove { token, confirm? }`. A multi-world source fails the first call with plain confirmation text; retry with `confirm:true`.
- `page.extensions.reorder { token, world, delta }`, where `delta` is -1 or +1. Native resolves world-relative order onto the global ExtensionsStore array.
- `page.extensions.preview { url }` returns a slim manifest summary only after native preview finishes.
- `page.extensions.install { url, id }` resolves only after `ExtensionsStore::installFinished` or `installFailed`.
- `page.extensions.configure { token?|url?, op?, ... }`. Remote extensions open their native-derived configure URL. Tankoyomi returns or mutates native chapter configuration with `op = state | master | defaultLanguage | providerEnabled | providerMove | resetOrder`.

A URL typed into the install panel is sent only in the action payload. It is not copied into feed state.
