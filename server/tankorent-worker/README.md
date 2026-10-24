# Tankorent search Worker

The app delegates source HTTP requests and parsing to this Worker. Source selection, user settings, canonical identities, ranking, torrent metadata/file selection and transfers remain in the native app. This service does not join torrent swarms or relay file bytes.

```sh
npm ci
npm test
npm run build
npx wrangler deploy
```

The configuration targets the dedicated `colosseum-tankorent` service. Provider hosts and parsers follow the existing native adapters, which remain available to legacy fixture harnesses. The production executable uses `TankorentWorkerIndexer`.

- `GET /healthz`: contract version and manifest revision.
- `GET /manifest.json`: stable extension ID `colosseum.well.indexers`, source inventory and configurable capability.
- `POST /v1/<sourceId>/search`: JSON `{ "query": "title", "limit": 30, "categoryId": "" }`.

Supported source IDs are `piratebay`, `torrentscsv`, `exttorrents` and `knaben`. Success returns `{ "contractVersion": 1, "sourceId": "...", "result": [] }`, using the existing TorrentResult field names. Failure returns a non-success HTTP status and an error envelope, with source health when available. User enablement and media filters remain local; adding a server route does not enable a source in the app's normal ladder.

Requests require JSON content type. Input/body sizes, HTTPS destination hosts, every redirect, upstream body size, result size, request count, concurrency and total search duration are bounded. Ordinary individual EXT detail failures preserve usable rows; deadline/budget exhaustion fails the search. Magnets are rebuilt from valid hashes with reviewed tracker parameters. Source queries and credentials are not logged by application code.

Deployment is distinct from source availability. See the [checkpoint receipt](../../docs/tankorent-worker-checkpoint-2026-10-23.md) for actual app evidence and remaining source limitations.
