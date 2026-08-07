# Theatre — The House HTTP Source (Design Specification)

- **Date:** 2026-08-07 · **Arc:** THEATRE_HTTP_SOURCE
- **Commissioned by:** Hemanth · **Design lead:** Agent 0 (Claude) · **Implementer:** Agent 4 (Player/Theatre lane)
- **Status:** Design written, awaiting Hemanth's review. Brainstorm locked 2026-08-07.
- **Supersedes:** the VidKing / VidLink hosted-player arc (`2026-08-02-theatre-vidking-hosted-player-extension.md`,
  `2026-08-02-theatre-vidlink-provider-b.md`) — both are retired by this design.
- **Builds on:** ZCode (GLM 5.2) FMHY provider survey, 2026-08-07 — provider vocabulary and the
  P-Stream takedown finding are carried forward as evidence, not re-derived.

---

## 1. Experience promise and scope

> **Press play on anything in Theatre and get a source that is already known to work — not one
> that merely claims to.**

Today Theatre finds sources one way: torrents, via Torrentio. Torrent rows advertise themselves
with seeder counts, and we measured on 2026-08-07 that those counts lie — a "200 seeder" torrent
delivered 21 real peers. VidKing was meant to add a second way, but it is an embedded web player:
it cannot say whether a title is there until it is already on screen.

This design replaces VidKing with a source that does the opposite: it returns a real link, and
checks that the link plays **before** Hemanth commits to it.

**In scope:** films and TV episodes, resolved over HTTP, appearing beside torrent rows in the
existing Sources sheet.

**Out of scope (deferred, §9):** anime, deep-catalogue/archival titles, and the press-play source
race.

## 2. Current state (inspected 2026-08-07)

**Ships:**
- Theatre asks every installed extension `GET {base}/stream/{type}/{id}.json` (`AddonClient.js`),
  where a TV episode id is `tt1234567:1:5`.
- **The Sources sheet already renders HTTP rows.** A stream carrying `url` instead of `infoHash`
  becomes a row tagged `Direct · HTTP stream` and flows through the normal play chain via the
  `url:<url>` convention (`AddonClient.js:9-10, 174-203`).
- Extensions is a real registry: install by URL, order, enable/disable, persisted to
  `installed.json` (`ExtensionsStore`). It already browses **stremio-addons.net** plus Stremio's
  official collection (`ExtensionsCatalog.js:8-9`).
- Two HTTP-source addons are already in our curated list: **NoTorrent**
  (`com.notorrent.addon`, verified live 2026-08-07 at v2.7.0, movies + series) and **WebStreamr**
  (present in the list, liveness **not** verified).
- `installBundled()` already supports app-owned extension rows (built for VidKing).

**Partial / uncertain:**
- VidKing/VidLink: an embedded web player in a cage. No availability check before opening.
- `bingeGroup` is parsed into every row (`AddonClient.js:202`, `Torrentio.js:142`) and **read by
  nothing**. Episode-to-episode source continuity does not exist.

**Missing — and this gates everything:**
- **The player cannot send request headers.** `MpvItem::loadFile(file)` issues `loadfile <url>`
  and nothing else. Stremio's protocol carries `behaviorHints.proxyHeaders` for exactly this, and
  our client discards it. Most HTTP providers refuse to serve without a `Referer`/`Origin`.
  A resolved link would therefore fail to play.

## 3. The primary user journey

1. Hemanth opens a film or an episode and presses the source button.
2. The sheet opens immediately. Torrent rows appear as they do today. **House HTTP rows appear at
   the same time, dimmed, reading `checking…`.**
3. Within roughly a second each HTTP row either firms up to **`Checked · plays now`** or quietly
   disappears. Rows that firm up sort **above** torrent rows.
4. He picks one and it plays. A confirmed row plays without the pause a torrent needs to find peers.
5. On the next episode of the same show, the sheet opens with **the source that worked last time
   already selected**, labelled so he can see why. One click changes it.

The line that carried a seeder boast now carries a receipt:

```
[T]  Torrentio                        1080p BluRay
     240 seeders · 2.1 GB · YTS                        <- a claim
     English   [1080p] [x265]
     Torrent · P2P stream                        ▶

[H]  House HTTP                              1080p
     Checked · plays now · vidnest                     <- a fact
     English   [1080p]
     Direct · HTTP stream                        ▶
```

## 4. States, interruptions, recovery, edge cases

| State | What Hemanth sees |
|---|---|
| Resolving | HTTP rows dimmed, `checking…`. Sheet is usable; torrent rows are already live. |
| Confirmed | Row firms up: `Checked · plays now · <provider>`. Sorts above torrents. |
| Failed check | Row **removes itself**. No dead rows, no error spam. |
| No HTTP source at all | One quiet line above the torrent rows: `No direct links for this one.` Torrent rows behave exactly as today. **No automatic switch** — his no-silent-fallback rule. |
| Provider dies mid-sweep | That provider is marked down; other providers are unaffected; the Extensions health line drops by one. |
| Link dies between check and play | The player surfaces the ordinary playback failure and the sheet reopens with that row removed. Treated as a normal failed source, not a special case. |
| Check is slow | A provider that has not answered in **4 seconds** is dropped from this sweep. The sheet never waits on a slow provider. |
| Series continuity | Remembered source is pre-selected and labelled `Continuing on <provider>`. If it fails this episode, the pre-selection is cleared and normal ordering returns. |
| Offline / no network | HTTP rows never appear; torrent rows behave as today. No error dialog. |

## 5. Controls, feedback, integration

- **No new screen and no new control.** The Sources sheet, the Extensions page and the player are
  the only surfaces touched.
- **The Extensions row is the health surface.** The house source's row subtitle reads
  `N of M providers responding`, refreshed after each sweep. When `N` is 0 the row reads
  `no providers responding` — honest, not hidden.
- **One switch.** The house source is a single Extensions entry with one on/off toggle, like
  Torrentio. Individual providers are not exposed as rows.
- **Ordering.** Confirmed HTTP rows sort above torrent rows. Within HTTP rows, quality descends.
- **Styling.** Existing sheet tokens only — grey/black/white, SVG, no colour, no emoji. The
  `checking…`/`Checked` states use the sheet's existing dim/ink treatment, not a new accent.
- **Accessibility.** The check state is carried in text (`checking…` / `Checked · plays now`), not
  by colour or motion alone.

## 6. Technical shape

Only the parts a planner needs. Implementation detail stays with Agent 4.

### 6.1 The header channel — built first, gates everything

`MpvItem` gains one entry point:

```
Q_INVOKABLE void loadFileWithHeaders(const QString& url, const QVariantMap& headers)
```

It sets mpv's `http-header-fields` from the map, then issues the existing `loadfile`. It **clears**
the option when a subsequent load carries no headers, so one source's `Referer` can never leak into
the next. `MpvItem::setProperty` already exists and is used for the constructor's option block
(`mpvitem.cpp:46-60`); this reuses it.

`AddonClient.parseStream()` gains one field: `headers`, read from `behaviorHints.proxyHeaders`.
The play chain carries it to `loadFileWithHeaders`. This also makes every **existing** third-party
HTTP addon (NoTorrent, WebStreamr) work correctly — it is not house-source-specific.

### 6.2 The public/private seam

Locked decision: the site-specific code does not live in the public repo.

- **Public repo — `native/httpsource/`:** the generic machinery. Provider registry, the sweep and
  timeout policy, the preflight check, health accounting, and an abstract provider interface.
  Nothing site-specific. Fully buildable and testable on its own.
- **Private repo — provider implementations:** one unit per provider, each declaring the id types
  it accepts, the content types it serves, and the headers its links require.
- **Build:** CMake includes the private provider tree when it is present beside the repo. When it
  is **absent the app still builds and runs**, the house source registers with zero providers and
  reports `no providers responding`. The public repo is therefore complete and honest, and carries
  no scraper for any named site.

### 6.3 How the house source is asked

It registers as an **app-owned extension row** through the existing `installBundled()` path (the
mechanism built for VidKing, reused after VidKing is removed). It is resolved **in-process** rather
than over HTTP — there is no localhost server and no public endpoint, which is deliberate: a hosted
endpoint is the exact shape that got P-Stream taken down.

`AddonClient` asks it alongside remote addons, using the same `type` + `id` it sends everyone else,
and receives the same stream-object shape back:

```
{ url, quality, headers, behaviorHints: { bingeGroup, filename, videoSize } }
```

### 6.4 Preflight

After a provider returns a link, the source issues a **ranged probe** (`Range: bytes=0-1`) carrying
that link's required headers. A 200/206 confirms; anything else drops the row. The probe reads the
first bytes only — it never downloads media.

For an HLS link the probe fetches the playlist and requires it to parse as a playlist. Nothing
deeper — a valid playlist that later stalls is an ordinary playback failure, not a preflight
concern.

### 6.5 Binge continuity

On a successful play, the source's `bingeGroup` (or, absent one, the provider id plus the series
id) is stored against the series. When the sheet opens for another episode of that series, the
matching row is pre-selected and labelled. Stored with existing progress/collection persistence;
no new store.

### 6.6 VidKing removal

Deleted: `native/hostedplayer/`, `resources/hostedplayer/`, `qml/HostedPlayerPage.qml`,
`qml/HostedPlayerApi.js`, and the four hosted-player tests.
Edited to drop the `hosted-player` resource, `hostedRows`, `isHosted`, the `HOSTED PLAYER` row
treatment and the `net.vidking.player` catalogue entry: `qml/SourcesSheet.qml`, `qml/Main.qml`,
`qml/TheatreSeries.qml`, `qml/TheatreApi.js`, `qml/AddonClient.js`, `qml/ExtensionsCatalog.js`,
`native/main.cpp`, `native/CMakeLists.txt`, `native/app_resources.qrc`,
`native/engine/ExtensionsStore.{cpp,h}`.

**Qt WebEngine stays.** Biblio's Reader 2 depends on it (`qml/reader2/*`); this is a surface
removal, not an engine teardown.

## 7. Sequence

The order is forced by dependency, not preference.

| # | Slice | Why here |
|---|---|---|
| 1 | **Header channel** | Nothing else works without it, and it independently fixes NoTorrent/WebStreamr. |
| 2 | **Measure what exists** | With headers working, test NoTorrent + WebStreamr against §8's bar. Sizes everything after it. |
| 3 | **VidKing removal** | Independent of the rest; do it once the replacement path is proven viable. |
| 4 | **House source skeleton + health line** | The registry, sweep, preflight and Extensions row, with zero or few providers. |
| 5 | **Providers to the bar** | Add providers until §8 passes. Count is an outcome, not a target. |
| 6 | **Binge continuity** | Pure gain, no dependency on provider count. |

**Slice 2 may shrink slice 5 to nothing.** If the existing addons clear the bar, the house source
stays a thin gap-filler and this arc ends early. That is a success, not a failure.

## 8. Acceptance criteria (observable)

1. **Headers.** A stream carrying `proxyHeaders` plays. Negative control: the same link without the
   headers fails. Proves the channel is doing the work and not passing vacuously.
2. **No leak.** After playing a header-carrying source, a subsequent source with no headers plays
   with none set.
3. **Coverage bar.** From a sample of **20 titles drawn from Theatre's own shelves** (Discover,
   Trending, Continue Watching, and one running series across three episodes), **at least 18 offer
   a confirmed HTTP row.** Recorded per title, not asserted.

   > **Translation flagged for Hemanth.** He locked the bar as "everything on Theatre's shelves
   > plays." 18-of-20 is my reading of that as something a test can fail, not a quiet softening:
   > a 20-of-20 gate would fail the whole arc on one obscure title that slipped into a shelf, and
   > would make the criterion untestable in practice. If he wants the stricter reading, this
   > becomes 20-of-20 and slice 5 runs longer. **His call, and it is the only number in this spec
   > that is mine rather than his.**
4. **Honesty.** No row that says `Checked · plays now` fails to start playing. A single violation
   fails this criterion — the whole promise is that the receipt is true.
5. **Speed.** The sheet is interactive immediately; HTTP rows resolve within **4 seconds** or drop.
6. **Health line.** With a provider deliberately disabled, the Extensions row count drops by
   exactly one. With all providers disabled it reads `no providers responding`.
7. **No silent switch.** With HTTP unavailable for a title, the sheet shows the no-direct-links
   line and torrent rows; nothing auto-plays.
8. **Continuity.** Episode 4 of a series opens pre-selected on the provider that played episode 3,
   labelled. Changing it takes one click.
9. **VidKing gone.** No hosted-player row, resource, file or test remains; the app builds and the
   book reader still opens.

## 9. Non-goals and deferred

**Non-goals:** anime; deep-catalogue and archival titles; any source requiring a login, captcha,
paywall or copy-protection bypass (recorded as a disqualifier, never defeated); a public hosted
endpoint; replacing Torrentio.

**Deferred:** anime over HTTP (different sites and a different numbering system); the press-play
source race (`ideas/2026-08-07-source-race-and-swarm-merge-ladder.md`); per-provider user toggles.

## 10. Discarded alternatives

- **Keep VidKing and add preflight to it.** Rejected: its availability is inside an opaque page;
  a separate probe path was investigated on 2026-08-02 and found to be token-gated and encrypted.
- **Host our own addon endpoint.** Rejected: a public endpoint under our name is precisely what
  got P-Stream's provider package taken down in ~3 months.
- **Put the providers in the public repo.** Rejected by Hemanth, same reason.
- **One extension per provider family.** Rejected: clutters Extensions and makes him the one who
  notices dead sites.
- **Check everything before showing the list.** Rejected: reintroduces the wait this arc exists to
  remove.
- **Build the house source first, measure later.** Rejected: two HTTP addons are already installable
  and unmeasured; building before measuring risks duplicating work that already ships.

## 11. Assumptions on the record

- NoTorrent verified live 2026-08-07 (v2.7.0). **WebStreamr's liveness is unverified.**
- That most FMHY-class providers require `Referer`/`Origin` is strong from ZCode's source reading
  and from both Stremio's and Nuvio's protocols carrying a header field — **not yet proven against
  a live request.** Slice 1's negative control settles it.
- Provider half-life is assumed to be months. The health line and the private-repo seam exist
  because of that assumption, not despite it.

## 12. Decision ledger

Locked: VidKing/VidLink removed · code outside the public repo · one house source, many providers ·
instant rows that confirm themselves · bar is Theatre's own shelves · films and shows first ·
health line on the Extensions row · series remembers its source visibly.

Constraints: header channel first · measure before building · no silent switching · no
access-control bypass · reuse the Sources sheet · WebEngine stays · grey/black/white + SVG ·
assume providers die · prefer sources browsers cannot use · Agent 4 implements.

Open: none.
