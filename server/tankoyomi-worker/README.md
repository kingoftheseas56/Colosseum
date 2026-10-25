# Tankoyomi Worker

Tankoyomi provider JavaScript normally executes in this Worker. Updated native apps execute user-enabled MangaSin and LectorXD on the device using signed scripts distributed by the Worker. The native app embeds a bootstrap manifest, refreshes the inventory over HTTPS, and keeps its existing language selection, provider ordering, identity, downloads and reader. Images go through the app's existing public-HTTPS media transport.

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
- `GET /addons/v1/es/mangasin.json` and `/addons/v1/es/lectorxd.json`: signed device-addon envelopes.
- `POST /v1/<canonical-language>/<provider-id>/searchSeries`: JSON `{ "query": "title" }`.
- `POST /v1/<canonical-language>/<provider-id>/getChapters`: JSON `{ "series": { ... } }`.
- `POST /v1/<canonical-language>/<provider-id>/getPages`: JSON `{ "chapter": { ... } }`.

POST requests require `Content-Type: application/json`. Success returns `{ "contractVersion": 1, "result": [] }`; failure returns a non-success HTTP status and `{ "contractVersion": 1, "error": "..." }`. A disabled inventory entry remains callable so an explicit client preference can enable it.

Each provider's metadata requests are restricted to its declared HTTPS hosts and their subdomains, including every redirect. Request/response sizes, request duration and upstream request count are bounded. The runtime exposes fixed Base64 and CryptoJS-compatible AES helpers without evaluating upstream scripts.

Device addons are signed offline with a dedicated Ed25519 key. `scripts/tankoyomi-sign-addons.mjs --help` describes publication; only signed public envelopes belong in `src/addons/`. The private key and any signing passphrase must remain outside the repository and Worker. The app verifies the signature, provider identity, script digest and fixed host permissions before execution, and retains a verified cache on refresh failure. Higher cached revisions cannot be replaced with older revisions. Updates are checked when a provider is first used in an app session.

MangaSin and LectorXD remain disabled by default. Device execution does not guarantee source availability: LectorXD still needs a verified title-search implementation and usable upstream access. Legacy clients retain the existing metadata API. See the [device-addon checkpoint](../../docs/tankoyomi-device-addons-checkpoint-2026-10-25.md) for deployment and actual-app qualification.
