# VidKing source-resolver probe — finding

> **Intern probe, commissioned by Agent 4, 2026-08-02.** Answers the question Hemanth
> raised: *the VidKing row shows for every title and often dead-ends — can we pre-check
> availability so it only appears where VidKing actually has a source?* Evidence for the
> VidKing → VidLink bake-off note. Reproducible; every claim below was run live.

## Verdict

**VidKing's source resolver is reachable keylessly, but it is a deliberately-obfuscated
anti-scraping protocol, not a usable availability API. Do not build on it.** Accurate
pre-filtering would require cracking and permanently maintaining a decoder for their
rotating encryption — fragile, high-maintenance, and outside the "documented iframe
interface only, no scraping" boundary the VidKing plan drew. The right answers stay:
(1) confirmed-failure negative cache with expiry, (2) a provider fleet so something usually
plays. Both are already folded into the VidLink Provider-B plan.

## What VidKing's own player does

Traced from VidKing's production bundle `VideoPlayer-*.js` (lazy chunk off
`/assets/index-*.js`, fetched live 2026-08-02). When the embed resolves a source it:

1. **Proxies TMDB metadata** — `GET https://db.speedracelight.com/3/{movie|tv}/{tmdbId}?append_to_response=external_ids`. This is just a TMDB-v3 passthrough (title, year, `imdb_id`); *not* an availability signal.
2. **Fans out across nine codenamed source engines**, all under `https://api.speedracelight.com`:

   | Codename | Endpoint | Note |
   |---|---|---|
   | Yoru | `cdn/sources-with-title` | |
   | Cypher | `downloader2/sources-with-title` | |
   | Breach | `m4uhd/sources-with-title` | |
   | Neon | `vsrc/sources-with-title` | |
   | Vyse | `hdmovie/sources-with-title` | qualityFilter English |
   | Killjoy | `meine/sources-with-title` | params language=german |
   | Fade | `hdmovie/sources-with-title` | qualityFilter Hindi |
   | Omen | `lamovie/sources-with-title` | |
   | Raze | `superflix/sources-with-title` | |

3. **Per query, first fetches a short-lived seed** — `GET {origin}/seed?mediaId={id}` →
   `{ "seed": "<token>", "ttlMs": 30000 }`. The token expires in **30 seconds** and is
   bound to the request.
4. **Then asks for sources** — `GET api.speedracelight.com/{endpoint}?title=…&mediaType=…
   &year=…&seasonId=…&episodeId=…&tmdbId=…&imdbId=…&enc=2&seed=<token>`. The response is
   **encrypted** ("STREAMCRYPTO"; `enc=2` is a version marker). Changing or dropping `enc`
   invalidates the seed.

## Live evidence (reproducible)

Headers used: `-A "Mozilla/5.0" -H "Referer: https://www.vidking.net/" -H "Origin: https://www.vidking.net"`.

| Step | Request | Result |
|---|---|---|
| TMDB proxy | `db.speedracelight.com/3/movie/550?append_to_response=external_ids` | `200` — full metadata, `imdb_id:"tt0137523"`, **no key** |
| Seed | `api.speedracelight.com/seed?mediaId=550` | `200` — `{"seed":"…","ttlMs":30000}`, **no key** |
| Sources (Fight Club, has sources) | `cdn/sources-with-title?…&tmdbId=550&enc=2&seed=…` | `200`, **1135 bytes, encrypted** |
| Sources (The Dark Knight, has sources) | `…&tmdbId=155&enc=2&seed=…` | `200`, **1135 bytes** — byte-identical size to Fight Club |
| Sources, `enc=0` / no `enc` | same, plaintext requested | `401 {"error":"STREAMCRYPTO_SEED_INVALID"}` |
| Sources (bogus tmdbId 99999999) | `…&tmdbId=99999999&enc=2&seed=…` | `500`, 173 bytes |

## Why it's a trap, not a shortcut

- **Encrypted by design.** You cannot read "are there sources" without reversing the
  STREAMCRYPTO decryption (key derivation from the seed + cipher). The scheme is versioned
  (`enc=2`) and seeds rotate every 30s — hallmarks of something built to be re-broken on
  their schedule, not a stable API.
- **Nine engines, two round-trips each.** Honest availability means seed+query across
  several providers *per title* — heavy for a Sources sheet that lists many titles.
- **Size/status inference is unreliable.** Two different available movies returned the exact
  same 1135-byte envelope; the only "no" I could produce was a `500` on a bogus id, not a
  clean empty-sources signal on a real-but-unavailable title.
- **Boundary.** It is squarely "scrape/intercept their internal resolver" — the thing the
  VidKing plan and Hemanth's constraint explicitly barred. Building on it converts a clean,
  keyless, low-maintenance extension into a brittle scraper.

## Recommendation (aligns with the VidLink plan)

1. **Do not query the resolver.** Keep to VidKing's documented iframe/postMessage interface.
2. **Confirmed-failure negative cache with expiry** — remember a title that dead-ended so it
   stops wasting clicks, then let the row try again after ~72h (sources come and go; a
   permanent hide would be honest-but-wrong). *Now specified in the VidLink Provider-B plan.*
3. **Provider fleet** — VidKing + VidLink (+ future) so when one has nothing another usually
   does, with a consented "try another source" button on the failure panel. *That plan.*

## Provenance / caveats

- All requests run 2026-08-02 from the dev box; VidKing bundle hashes change, so endpoint
  paths and the `enc` version will drift — another reason not to couple to them.
- The documented `PLAYER_EVENT` schema (`timeupdate|play|pause|ended|seeked`) has **no
  error/"no source" event**, so within the iframe interface a dead-end is only detectable by
  the absence of a play/timeupdate event within a short window — which is exactly what the
  negative-cache detection should key on.
