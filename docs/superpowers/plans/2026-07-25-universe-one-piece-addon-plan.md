# Universe Addon Plan — One Piece

**Status:** **LOCKED by Hemanth 2026-07-25** — stacked, NOT implemented · **Author:** Agent 0 (Claude), theatre
**Design:** `docs/superpowers/specs/2026-07-25-colosseum-universes-as-extensions-design.md`
**Nothing in this document is to be built yet.** Per Hemanth, 2026-07-25: plans are stacked, not executed.

---

## 1. What this plan is

The complete, machine-verified content of the first universe extension. It carries the addon's logo,
its row layout, every item in those rows, and the exact provider ID attached to each item. Because the
design makes sections data rather than code (spec §5.2), **this document converts to `universe.json`
with no interpretation.**

## 2. Identity and artwork

| Field | Value | Verified |
|---|---|---|
| `id` | `com.colosseum.universe.onepiece` | — |
| `name` | One Piece | — |
| `logo` | `https://images.metahub.space/logo/medium/tt0388629/img` | 200 · PNG · 140 KB · transparent wordmark |
| `background` | `https://s4.anilist.co/file/anilistcdn/media/manga/banner/30013-hbbRZqC5MjYh.jpg` | 200 · JPEG · 646 KB |
| `resources` | `["universe"]` | declares the type (spec §5.1) |

⚠ The cover URL carried in the old baked `qml/Universes.js`
(`…/cover/bx30013-BeslEMqiPhlk.jpg`) now returns **404**. Recorded as evidence for the served-content
decision: baked art rots, and this one already had.

### 2.1 Per-entry artwork — one confirmed hole

Video tiles draw from `images.metahub.space/poster/small/<imdbid>/img`. Measured across the payload,
**37 of 38 resolve.** The one miss, confirmed on two independent passes:

| Entry | Gap |
|---|---|
| One Piece Log: Fish-Man Island Saga — `tt33992229` | No metahub poster at any size. The Cinemeta *metadata* is fine (21 episodes); only the art is absent. |

This is the first real case that earns `entry.poster` — the optional per-entry art override already in the
design (spec §5.2). That entry needs a supplied poster URL or it renders as an empty tile.
Reminder from house doctrine: **only `/poster/small` is reliable on metahub**; `/medium` 404s across the
long tail, and Cinemeta poster URLs must never be upscaled.

## 3. Row layout

Rough mock: `agents/colosseum-universe-onepiece-rows-mock.html` (disposable, rev 2).

### 3.0 Visual contract — inherited, not invented

Hemanth's ruling, 2026-07-25: *"hopefully it's much closer to the game of thrones, harry potter universe
pages' dimensions and fonts."* Those are the **saga template**, `qml/SagaUniversePage.qml`. Values read
straight from it — the universe page reuses these rather than inventing proportions:

| Element | Value | Source |
|---|---|---|
| Page margin | `54` (`theme.margin`) · content width `parent.width - 108` | `Theme.qml:33`, `SagaUniversePage.qml:125` |
| Header band height | `360` | `:94` |
| Kicker | `theme.ui` 12px · letterSpacing **4** · bold · `theme.gold` | `:114-115` |
| Universe name | **`theme.display` (Fraunces) 62px** | `:116-117` |
| Metaline | `theme.ui` 14px · `inkDimmer` | `:118-119` |
| Section title | **Fraunces 25px**, subtitle `theme.ui` 13px, 12px gap, 16px block | `:194-198` |
| Tile | **150 × 236**, radius **8** | `:218-221` |
| Tile gap | **22** · row height `246` | `:210`, `:202` |
| Caption block | height `56` · `theme.ui` 12px DemiBold | `:251`, `:262-263` |
| Index numeral | 26 × 26 r6 · `theme.ui` 13px Bold gold | `:238`, `:244-245` |
| Small chip ("UPCOMING") | `theme.ui` 9px · letterSpacing 2 · gold | `:531-532` |
| Inter-section spacer | `40` (44 after the band) | `:280`, `:186` |

Fonts resolve to `theme.ui = "Segoe UI"` and `theme.display = "Fraunces"` (`Theme.qml:23-24`; Fraunces is
bundled at `assets/fonts` and loaded in `Main.qml`).

Rev 2 of the mock was measured in-browser against every row above and matches exactly.

**One open visual question for Hemanth:** the saga pages render the universe name as a Fraunces 62px
wordmark because they had no logo asset. We now have a real One Piece logo (§2). Rev 2 follows the saga
lineage (Fraunces text) and reserves the logo for the Home rail tile. He may prefer the logo on the page
header instead — his call, one line either way.

```
┌──────────────────────────────────────────────────────────────┐
│  [ONE PIECE wordmark]                            ← back      │  header, banner dark-washed behind
├──────────────────────────────────────────────────────────────┤
│  Continue                                                     │  ← only if progress exists
│  [ anime ep 1104 ]  [ manga ch 1140 ]                         │  one tile per medium touched
├──────────────────────────────────────────────────────────────┤
│  TV Shows        [•][•][•][•][•]                              │  video posters
│  Movies          [•][•][•][•][•][•][•][•] →                   │  video posters
│  Manga           [▯][▯][▯][▯][▯][▯] →                         │  manga covers
│  Specials        [•][•][•][•][•][•] →                         │  video posters
│  Novels          [▮][▮][▮][▮]                                 │  book jackets
└──────────────────────────────────────────────────────────────┘
```

Section order is the served array order. Tile shape comes from `section.kind`. No hero, no canon tags,
no chronology — One Piece has no inherent timeline (design §10).

## 4. The rows

All IDs below were verified by direct provider fetch on 2026-07-25 — Cinemeta `meta` endpoint for video
(a returned `name` **and** matching `type`), AniList GraphQL for manga (returned `format`), iTunes
`lookup` for books (returned `trackName` + `kind=ebook`). Research came from DeepSeek and ChatGPT
independently; **neither was sufficient alone** (§7).

### 4.1 TV Shows — `kind: video`, 5 entries

| # | Title | Year | type | Cinemeta ID | Verified as |
|---|---|---|---|---|---|
| 1 | One Piece | 1999– | series | `tt0388629` | One Piece |
| 2 | One Piece (live action) | 2023– | series | `tt11737520` | One Piece |
| 3 | One Piece Log: Fish-Man Island Saga | 2024– | series | `tt33992229` | One Piece Log: Fish-Man Island Saga · 21 eps |
| 4 | One Piece in Love (*Koisuru One Piece*) | 2025 | series | `tt36600601` | One Piece in Love |
| 5 | THE ONE PIECE (Wit remake) | 2027– | series | `tt30476502` | THE ONE PIECE |

Row 3 is the "Fishman Island Special Edition" from Hemanth's sketch — it turned out to be a real
broadcast series with its own ID, so it moved out of the manual block.
Row 5 confirms **2027**, not the 2026 our baked data claimed.

### 4.2 Movies — `kind: video`, 15 entries

| # | Title | Year | Cinemeta ID |
|---|---|---|---|
| 1 | One Piece: The Movie | 2000 | `tt0814243` |
| 2 | Clockwork Island Adventure | 2001 | `tt0832449` |
| 3 | Chopper's Kingdom on the Island of Strange Animals | 2002 | `tt0997084` |
| 4 | Dead End Adventure | 2003 | `tt1006926` |
| 5 | The Cursed Holy Sword | 2004 | `tt1010435` |
| 6 | Baron Omatsuri and the Secret Island | 2005 | `tt1018764` |
| 7 | Giant Mechanical Soldier of Karakuri Castle | 2006 | `tt1059950` |
| 8 | Episode of Alabasta: The Desert Princess and the Pirates | 2007 | `tt1037116` |
| 9 | Episode of Chopper Plus: Bloom in Winter, Miracle Sakura | 2008 | `tt1206326` |
| 10 | One Piece Film: Strong World | 2009 | `tt1485763` |
| 11 | One Piece 3D: Straw Hat Chase | 2011 | `tt1865467` |
| 12 | One Piece Film: Z | 2012 | `tt2375379` |
| 13 | One Piece Film: Gold | 2016 | `tt5251328` |
| 14 | One Piece: Stampede | 2019 | `tt9430698` |
| 15 | One Piece Film: Red | 2022 | `tt16183464` |

All `type: movie`. Row 11 is a 30-minute theatrical short that Toei and Wikipedia both count as film 11
— keeping it here yields exactly the **15** Hemanth asked for. IMDb labels it "Short"; Cinemeta returns
`type: movie`, so it resolves normally.

### 4.3 Specials — `kind: video`, 17 entries

| # | Title | Year | Cinemeta ID |
|---|---|---|---|
| 1 | Defeat the Pirate Ganzak! (OVA) | 1998 | `tt1012788` |
| 2 | Adventure in the Ocean's Navel | 2000 | `tt0975705` |
| 3 | Open Upon the Great Sea! A Father's Huge, HUGE Dream! | 2003 | `tt1003286` |
| 4 | Protect! The Last Great Stage | 2003 | `tt1010037` |
| 5 | Luffy's Detective Story | 2005 | `tt1012787` |
| 6 | Strong World Episode 0 (OVA) | 2010 | `tt7947592` |
| 7 | Episode of Nami | 2012 | `tt2598466` |
| 8 | Episode of Luffy: Adventure on Hand Island | 2012 | `tt3354344` |
| 9 | Episode of Merry | 2013 | `tt3354352` |
| 10 | Dream 9 Toriko × One Piece × Dragon Ball Z | 2013 | `tt2893336` |
| 11 | 3D2Y | 2014 | `tt5098548` |
| 12 | Episode of Sabo | 2015 | `tt6597356` |
| 13 | Adventure of Nebulandia | 2015 | `tt6609162` |
| 14 | Heart of Gold | 2016 | `tt6425816` |
| 15 | Episode of East Blue | 2017 | `tt11757066` |
| 16 | Episode of Skypiea | 2018 | `tt11744496` |
| 17 | **One Piece Fan Letter** | 2024 | `tt33998607` |

Row 17 is the Fan Letter from Hemanth's sketch. All `type: movie`.

### 4.4 Manga — `kind: manga`, provider `anilist`, 13 entries (12 pinned + 1 manual)

| # | Title | Year | AniList ID | format |
|---|---|---|---|---|
| 1 | One Piece | 1997 | `30013` | MANGA |
| 2 | **One Piece — digitally coloured edition** | — | **MANUAL** (no provider ID) | — |
| 3 | One Piece Log Book Omake | 1999 | `44414` | MANGA |
| 4 | ONE PIECE: STRONG WORLD (Chapter 0) | 2009 | `47152` | ONE_SHOT |
| 5 | Chopperman | 2010 | `82353` | MANGA |
| 6 | One Piece Party | 2014 | `102533` | MANGA |
| 7 | CHIN PIECE | 2018 | `110258` | MANGA |
| 8 | Koisuru ONE PIECE | 2018 | `110233` | MANGA |
| 9 | One Piece: Shokugeki no Sanji | 2018 | `103252` | MANGA |
| 10 | Kobiyama Who Looks Like Koby | 2018 | `110232` | MANGA |
| 11 | ONE PIECE Gakuen!! (School) | 2019 | `110715` | MANGA |
| 12 | One Piece: Ace's Story — The Manga (*episode A*) | 2020 | `117802` | MANGA |
| 13 | One Piece 1000-wa Kinen! Tokubetsu Bangai-hen | 2021 | `154266` | MANGA |

**Row 2 position is Hemanth's ruling (2026-07-25): the coloured edition sits immediately beside the main
manga, not at the end of the row.** It is the one manual entry inside an otherwise pinned section, so the
served payload must preserve array position 2 — the client never re-sorts (spec §5.2).

Row 11 is the **manga adaptation** of the prose novel in §4.5 rows 1–2 — one story, two media, two
different IDs, two different sections. This is the ambiguity the design was built to prevent
(spec §5.4), now closed with evidence.
Row 7 is the manga; the 2025 anime of the same name is TV Shows row 4. Both correct.

### 4.5 Novels — `kind: book`, provider `applebooks`, 4 entries

| # | Title | Apple Books ID | Verified `trackName` | kind |
|---|---|---|---|---|
| 1 | One Piece: Ace's Story, Vol. 1 | `1509329459` | One Piece: Ace's Story, Vol. 1 | ebook |
| 2 | One Piece: Ace's Story, Vol. 2 | `1528233153` | One Piece: Ace's Story, Vol. 2 | ebook |
| 3 | One Piece: Law's Story | `6741084754` | One Piece: Law's Story | ebook |
| 4 | One Piece: Heroines, Vol. 2 | `6736634886` | One Piece: Heroines, Vol. 2 | ebook |

### 4.6 Comics — omitted

GetComics carries Western comics only; zero One Piece content. Section omitted entirely rather than
shown empty (design §6).

## 5. Manual block — not researchable

| Item | Section | Why manual |
|---|---|---|
| One Pace | TV Shows, last position | Unofficial fan re-edit. No provider identity exists anywhere. |
| One Piece digitally coloured manga | Manga, **position 2 — beside the main manga** | Alternate edition of `30013`; AniList has no separate record. Placement is Hemanth's explicit ruling. |

Both need a hand-authored entry carrying its own title, art and resolution path, since neither can be
pinned. **Both are IN — confirmed by Hemanth, 2026-07-25.**

## 6. Verified-absent — dropped, with reasons

| Item | Reason |
|---|---|
| One Piece: Heroines (2026 TV) — `tt41295042` | Appears in Cinemeta **search**, metadata record **empty**. Would install and render blank. |
| Romance Dawn Story (2008 OVA) — `tt27624401` | Same failure: search hit, empty metadata. |
| Jango's Dance Carnival · Dream Soccer King! · Take Aim! The Pirate Baseball King | Theatrical featurettes; no IMDb title page found by either researcher. |
| 11 recap broadcasts (Trafalgar Law project, Cipher Pol log, Four Emperors, Revolutionary Army, Navy's Proud Log, Egghead recap, 5× Dr. Chopper's Adventure Checkup) | Almost certainly episodes *inside* `tt0388629`, not standalone titles. Would need episode-level linking, which this design does not do. |
| Monsters (1994 one-shot) · Fischer's × One Piece · Special Episode "Luff" · A Big Fan of One Piece · Eyes of Eiichiro's Staff · Memories with Oda-san · One Piece Short! · My Plans Never Fail!! · Straw Hat Theater · cover-comic one-shots (Zoro Falls Into the Sea, Vivi's Adventure, Nami vs. Kalifa) · film-comic adaptations | No standalone AniList record found. Several cover-comics are collected inside `117802`. |
| Heroines Vol. 1 | Official VIZ English edition exists; **no Apple Books ebook**. Under the law, no pin. |
| 5 Scholastic prose books (2007) · Japanese novelizations | No Apple Books English ebook. |

**Two IDs both researchers would have shipped were killed here.** The search-hit-but-no-metadata failure
is invisible to a citing researcher and only a metadata fetch exposes it.

## 7. Provenance

| Source | Contributed |
|---|---|
| DeepSeek | Base enumeration + 45 candidate IDs. Uniquely found: Luffy's Detective Story `tt1012787`, Dream 9 `tt2893336`, Law's Story `6741084754`, and 5 AniList pins ChatGPT could not open (403). |
| ChatGPT | Far wider sweep (35 manga rows, 29 novels, 32 specials). Uniquely found: Ganzack `tt1012788`, Strong World Ep 0 `tt7947592`, Fan Letter `tt33998607`, Koisuru anime `tt36600601`, AniList `44414` / `47152` / `154266` / `110232`. |
| Agent 0 machine pass | Verified all 53. Recovered `tt30476502` + `tt33992229` (DeepSeek wrongly called them nonexistent because unreleased). Killed `tt41295042` + `tt27624401`. |

**Process ruling for every future universe: two independent researchers, then the machine pass as sole
arbiter.** Neither model alone was sufficient, and both would have shipped a blank-page ID.

## 8. Divergence from Hemanth's original sketch — needs his eye

His 2026-07-25 sketch listed roughly: 5 TV shows · 15 movies · 3 manga · the TV specials incl. Fan Letter
· 1 novel (Ace's).

Applying the aggregation doctrine (*"we are only here to do aggregation of all content under a given
IP"*) produced more:

| Section | His sketch | This plan | Note |
|---|---|---|---|
| TV Shows | 5 | 5 pinned + 1 manual | Matches exactly, plus the *Koisuru* anime as a bonus. |
| Movies | 15 | **15** | Exact match. |
| Manga | 3 | **12** | ⚠ Biggest divergence. Includes gag strips, yonkoma and anniversary one-shots. Aggregation says include them; his sketch says three. |
| Specials | "all TV specials and stuff like Fan Letter" | **17** | Includes 2 OVAs and the Dream 9 crossover. |
| Novels | 1 (Ace's) | **4** | Ace's is 2 volumes; plus Law's Story and Heroines Vol. 2. |

### Rulings — Hemanth, 2026-07-25 ("seen it, looks great, lock it")

All three divergences were put to him with the rendered mock in front of him. His rulings:

1. **Manga: all 12 stay.** Aggregation wins over his shorter sketch — gag strips, yonkoma and anniversary
   one-shots included. The sketch was illustrative, not a limit.
2. ***Koisuru One Piece* keeps both appearances** — the 2025 anime in TV Shows *and* the manga in Manga.
   One story, two media, two IDs, two sections.
3. **Heroines Vol. 2 ships without Vol. 1.** Only Vol. 2 has an English ebook; a gap in volume numbering is
   accepted rather than dropping a real work.
4. **One change requested and applied:** the digitally coloured manga moves to **position 2**, immediately
   beside the main manga (§4.4).

No open items remain in this plan.

## 9. Totals

**53 verified pins** — 5 TV · 15 movies · 17 specials · 12 manga · 4 novels
**2 manual entries, both confirmed IN** — One Pace (TV Shows, last) · coloured manga (Manga, **position 2**)
**55 entries across 5 sections.** Comics omitted; GetComics carries no manga.
**0 unverified IDs.** Nothing in §4 was accepted on a researcher's word.
