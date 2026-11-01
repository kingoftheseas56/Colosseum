# Nyaa Worker

A bounded metadata search Worker for Nyaa. It exposes `/healthz`, `/manifest.json`, and `POST /v1/nyaa/search` with `{ "query": "..." }`. Search results come from Nyaa's RSS feed and contain titles, info hashes, uploader names, seed and leech counts, sizes, and a validated Nyaa download URL.

The Worker fixes its upstream origin and RSS search parameters. It does not fetch torrent files, media, or magnets. Query provenance, ranking, trust, and coverage remain native responsibilities.

Run `npm test` for fixture and API checks. Wrangler configuration is in `wrangler.jsonc`.

For a portable Node.js 22+ host, run `npm start`. It listens on `0.0.0.0` at numeric `PORT` (default `3000`), uses the same Worker handler and response contract, and drains active requests on `SIGTERM`/`SIGINT`. The host still fetches metadata from Nyaa's fixed RSS endpoint; downloads and all library, hash, and local state remain owned by the native app.
