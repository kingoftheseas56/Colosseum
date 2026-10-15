# Anime HTTP Source Landscape — via Aniyomi (research)

- **Date:** 2026-08-07 · **By:** Agent 0 (Claude), direct research
- **For:** THEATRE_HTTP_SOURCE arc, the deferred anime lane
- **Why me:** Hemanth named `aniyomiorg/aniyomi` and `hayase-app` after ZCode had already begun the
  earlier brief, so this half fell to me.
- **Method:** live fetches of GitHub trees + web search synthesis. Confidence marked per claim.
  Nothing here is asserted from memory alone.

## The one finding that decides the anime lane

**The biggest anime sources deliver video the exact way our design refuses to touch — encrypted,
anti-bot-guarded, and under active legal fire specifically for the decryption.** That is not a
setback; it is our no-bypass constraint meeting reality and drawing a clean line.

| Anime source | How it hands over video | Verdict under OUR rules |
|---|---|---|
| **HiAnime / Zoro → MegaCloud** (the dominant one) | Obfuscated JavaScript + an AES key that must be extracted by deobfuscating their player; TLS-fingerprint anti-bot needing curl-impersonate | **DISQUALIFIED.** Requires defeating encryption and anti-bot — both explicit disqualifiers in our spec — and it is the single hottest legal target (below). |
| **AnimePahe → Kwik** | Cloudflare + packed/obfuscated JS + a token POST challenge + frequently AES-128 encrypted HLS segments | **DISQUALIFIED.** Encryption + anti-bot. |
| **AllAnime** | GraphQL API + a light hex-encode resolved via a `clock.json` endpoint; direct CDN MP4; **needs only a `Referer`/`Origin` header**; no captcha, no AES | **VIABLE.** The header channel (arc slice 1) is exactly what it needs. Hex-decode is packaging, not a §1201 protection. |
| Shared cyberlockers (streamwish, filemoon, voe, mp4upload, doodstream…) | Usually packed-JS unwrap + a header; varies per host | **MIXED / case-by-case.** Some are header-only; some need JS unpacking that sits near our line. |

**Confidence:** the MegaCloud/Kwik encryption picture is *Inferred (strong)* from multiple
converging technical write-ups; the AllAnime "header-only" picture is *Inferred (strong)* from
community API descriptions. Neither was read from the extractor source line-by-line — that is the
one deferred verification if the anime lane is ever commissioned.

## The legal line is sharper for anime than for film

MegaCloud is not merely "a site that might get taken down." In **March 2026 Crunchyroll filed a
DMCA** against the MegaCloud key-extraction tooling, framed as a **§1201 anti-circumvention**
violation — the legal theory that specifically criminalises *defeating encryption*, distinct from
ordinary takedown. The main community key-extractor is now marked **"NO LONGER FUNCTIONAL."**

This matters to us directly: our no-bypass rule is not squeamishness, it is the exact boundary
Crunchyroll is litigating. A plain HTTP request for a file that a site serves openly is a different
legal object from extracting an AES key out of obfuscated JS. **Our design already sits on the safe
side of that line — this finding is the reason to keep it there, hard.**

Source: `github.com/github/dmca/blob/master/2026/03/2026-03-23-crunchyroll.md`.

## The takedown pattern now has three independent data points

Our whole design assumes these ecosystems rot. That assumption is now a well-evidenced pattern, not
a fear:

1. **P-Stream** — ALPA DMCA 2026-03-16, went closed-source (ZCode's finding).
2. **Aniyomi official extensions** — `aniyomiorg/aniyomi-extensions` **archived by its owner
   2025-07-05**, read-only; development scattered to community forks.
3. **MegaCloud tooling** — Crunchyroll §1201 DMCA 2026-03, key-extractor non-functional.

Three unrelated corners of the ecosystem, three central repos going dark inside ~a year. The health
line and the private-repo seam in our design are the correct response to *how this landscape
actually behaves*, confirmed from the outside.

## Where anime extension development actually moved (durability)

After the July 2025 archive, the anime side did **not** die — it forked:

- **`yuzono/anime-extensions`** — the most active successor. **5,432 commits**, 361★, actively
  maintained, feeds the **Anikku** app fork (an Aniyomi descendant). *Confirmed live.*
- **`Kohi-den/extensions-source`** — a second community fork for Aniyomi and forks.
- **`keiyoushi/extensions`** — the *manga* successor (Mihon/Tachiyomi lineage), not anime, but the
  same rescue pattern.

So the living reference we would actually read is **`yuzono/anime-extensions`**, not the archived
official repo. *Confidence: Confirmed for existence/activity; Inferred that yuzono is the primary
anime successor.*

## The video-host layer — and its overlap with the film ecosystem

Aniyomi's shared extractor library (`lib/`, read directly — **Confirmed**) names 43 video-host
extractors. The ones that **also** appear in ZCode's film/TV resolver survey are the strongest
reliability signal, because two unrelated communities maintain them independently:

**Overlap (maintained by both anime AND film ecosystems):**
`streamwish` · `filemoon` · `vidhide` (= filelions) · `voe` · `dood` (doodstream) · `mixdrop` ·
`mp4upload` · `streamtape` · `upstream` · `uqload`

**Anime-specific hosts in the library:** `megacloud` (disqualified, above), `gogostream`,
`chillx`, `streamhub`, `sibnet`, `okru`, `vk`, `dailymotion`, `sendvid`, `fastream`, `vidbom`,
`vidsrc-extractor`, plus Google-Drive and Blogger extractors.

**Architectural note that separates anime from film:** film/TV has *branded resolvers*
(vidnest, videasy, vixsrc) that answer by TMDB id. Anime has no such thing — it has anime *sites*
(AllAnime, HiAnime, AnimePahe) that each embed a video host, and each site indexes by its **own**
ids via its own search. That is the identity problem ZCode's Task 2 is chasing, and it is
unavoidable for anime.

## What this means for the arc

- **Deferring anime was correct, and for a stronger reason than "different IDs."** The dominant
  anime backends are structurally off-limits to us. Anime HTTP would rest almost entirely on
  **AllAnime** plus whatever shared cyberlockers prove header-only — a thin, single-anchor
  foundation, exactly the shape that should wait for evidence, not be built on hope.
- **AllAnime is the one real anchor**, and it happens to need precisely the header channel slice 1
  already builds — so if anime is ever commissioned, the plumbing is shared, not new.
- **The film/TV lane is unaffected and better-founded.** Its shared cyberlockers overlap the anime
  set, so the durability work ZCode is doing on the film side partly covers anime's host layer too.

## What I could NOT determine (honest gaps)

- Whether AllAnime's `clock.json`/GraphQL flow is *stable* enough to be an anchor, or itself churns
  — needs the same commit-history durability read ZCode is doing for film providers, applied to
  `yuzono/anime-extensions`.
- Which shared cyberlockers are header-only versus packed-JS — the `MIXED` row needs per-host
  reading before any could be called viable.
- Whether AllAnime can be reached from a TMDB/IMDb id at all, or only from an AniList/MAL id we
  would have to obtain first (the identity problem — ZCode Task 2 territory).
- None of the extractor *source* was read line-by-line; the delivery-method verdicts are from
  technical write-ups, strong but second-hand.

## Sources

- `github.com/aniyomiorg/aniyomi-extensions` (archived 2025-07-05; `lib/` read directly)
- `github.com/yuzono/anime-extensions` (live successor, 5,432 commits)
- `github.com/Kohi-den/extensions-source`
- `github.com/github/dmca/blob/master/2026/03/2026-03-23-crunchyroll.md` (§1201 notice)
- `github.com/Eggwite/megacloud-key-extractor` (marked NO LONGER FUNCTIONAL)
- Community technical write-ups on MegaCloud, Kwik, and AllAnime extraction (search synthesis)
