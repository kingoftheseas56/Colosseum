# Slice 3 — the two shipped HTTP addons, measured

- **Date:** 2026-08-07 · **Arc:** THEATRE_HTTP_SOURCE · **Agent 4 (Claude), Player/Theatre**
- **Method:** direct Stremio-addon API probe (`GET {base}/stream/{type}/{id}.json`), read-only.
  Titles drawn from the SAME catalog Theatre's Discover uses (Cinemeta `catalog/movie/top`) plus a
  running series across three episodes and two ancient/obscure ids as negative controls.
  Scripts: `artifacts/advisor/slice3_{probe,full20,playcheck}.py` (artifacts/ is gitignored).
- **Why direct-API and not the in-app drive:** reaching the open Sources sheet requires clicking a
  dynamically-id'd Discover card then an unnamed series-page play button — navigation the static
  Lanista scenario can't script standalone (bridge limit, ledger UNAVAILABLE: no per-card walk over
  runtime-discovered names). The direct API measures the same coverage the sheet would show, faster
  and deterministically. In-app eyes-on (slice 1's human-witness) is delivered on a known-good title.

## Liveness

| Addon | URL | Status |
|---|---|---|
| NoTorrent | `addon.notorrent2.workers.dev` | **live** — v2.7.0, HTTP 200, resources [catalog, stream], types [movie, series] |
| WebStreamr | `87d6a6ef6b58-webstreamrmbg.baby-beamup.club` | **live** — v0.73.2, HTTP 200 — but see coverage |

**WebStreamr liveness settled** (spec flagged it unverified): the endpoint is up, but returns
**zero** streams for every title as-shipped. Its manifest says it needs language configuration and a
MediaFlow proxy for URLs. As-shipped it contributes nothing.

## Coverage (18 real titles + 2 controls)

| Source | Covered |
|---|---|
| **NoTorrent** | **18 / 18** — 2–24 playable HTTP streams per title |
| **WebStreamr** | 0 / 18 |
| **Either (the shipped-addons figure)** | **18 / 18** |
| Negative controls (Carmencita 1894, Roundhay 1888) | both empty — measurement is non-vacuous |

Full per-title table lands in `artifacts/advisor/slice3_full20_result.txt`. Every miss column is
zero because there were no misses: NoTorrent covered brand-new releases (Spider-Man: Brand New Day,
Devil Wears Prada 2, Project Hail Mary), catalogue staples (Troy, Spider-Man: Homecoming), and a
running series (Breaking Bad S1E1–3) alike.

**Playability confirmed, not just presence:** Range-probing NoTorrent's actual stream URLs for The
Matrix returned `206` + `video/x-matroska` (a direct MKV) and `application/vnd.apple.mpegurl` (HLS)
— real video bytes, no headers required. NoTorrent proxies everything through its OWN worker and
self-handles upstream auth, which is why its streams carry no `proxyHeaders`.

## Verdict against the bar

The plan's floor was 10/20; the (Hemanth-flagged) proposed bar was 18/20. **NoTorrent alone clears
it outright at 18/18.** The existing shipped addon covers Theatre's HTTP lane completely today.

## The standing liability

Every NoTorrent stream is funneled through a **single Cloudflare worker it owns**. This is the exact
hosted-endpoint fragility the spec named (P-Stream: ALPA DMCA, dead in three months). "NoTorrent
covers everything" and "NoTorrent could vanish overnight and take the whole HTTP lane with it" are
both true. The house source (slices 5–6) was always the insurance against precisely this.

## Decision (Hemanth, ratified 2026-08-07)

**Ship NoTorrent, defer our own engine.** Wire NoTorrent as the HTTP source, remove VidKing, add
source-memory. **Do NOT build the custom house-source providers now (slices 5 and 6 DEFERRED)** —
NoTorrent covers coverage, so building a redundant engine isn't earned. Revisit the house source
only if NoTorrent degrades. The single-worker fragility is accepted and watched, not pre-solved.

**Revised remaining arc:** slice 4 (VidKing removal) proceeds — Hemanth's standing call. Slice 7
(source memory) proceeds — it applies to NoTorrent HTTP rows and torrent rows alike, independent of
the house source. Slices 5–6 shelved with this measurement as the trigger to revive them.

**WebStreamr:** shipped-but-empty. Small separate call — configure it (language + proxy) or drop it
from the curated default. Not resolved here.
