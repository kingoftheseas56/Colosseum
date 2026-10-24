# Tankoyomi Worker

Tankoyomi provider JavaScript executes in this Worker. The native app embeds a bootstrap manifest, refreshes the inventory over HTTPS, and keeps its existing language selection, provider ordering, identity, downloads and reader. Images go through the app's existing public-HTTPS media transport; this Worker returns metadata only.

From this directory:

```sh
npm ci
npm test
npm run build
npx wrangler deploy
```

The Wrangler configuration targets the dedicated `colosseum-tankoyomi` service. Deploying changes provider behavior for clients using its endpoint; run fixture checks and live qualification before enabling a new source by default.

Endpoints:

- `GET /healthz`: contract version and manifest revision.
- `GET /manifest.json`: current provider inventory.
- `POST /v1/<canonical-language>/<provider-id>/searchSeries`: JSON `{ "query": "title" }`.
- `POST /v1/<canonical-language>/<provider-id>/getChapters`: JSON `{ "series": { ... } }`.
- `POST /v1/<canonical-language>/<provider-id>/getPages`: JSON `{ "chapter": { ... } }`.

POST requests require `Content-Type: application/json`. Success returns `{ "contractVersion": 1, "result": [] }`; failure returns a non-success HTTP status and `{ "contractVersion": 1, "error": "..." }`. A disabled inventory entry remains callable so an explicit client preference can enable it.

Each provider's metadata requests are restricted to its declared HTTPS hosts and their subdomains, including every redirect. Request/response sizes, request duration and upstream request count are bounded. The runtime exposes fixed Base64 and CryptoJS-compatible AES helpers without evaluating upstream scripts.

MangaSin and LectorXD are disabled candidates. MangaSin is blocked by upstream HTTP 403 from the deployed Worker. LectorXD has fixture-supported direct-series parsing, but no verified title-search route and direct upstream access is blocked. See the [checkpoint receipt](../../docs/tankoyomi-worker-checkpoint-2026-10-23.md) for the qualification boundary.
