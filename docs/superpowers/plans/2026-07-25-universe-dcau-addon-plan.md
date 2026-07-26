# Universe Addon Plan — DCAU (the Bruce Timm universe)

**Status:** stacked, NOT implemented · **Date:** 2026-07-25 · **Author:** Agent 0 (Claude), theatre
**Design:** `docs/superpowers/specs/2026-07-25-colosseum-universes-as-extensions-design.md`
**Sibling plan:** `2026-07-25-universe-one-piece-addon-plan.md`
**Nothing here is to be built yet.** Plans are stacked, not executed.

---

## 1. Scope — locked

**Strict shared continuity only** (Hemanth, 2026-07-25). The one connected animated universe, not
Bruce Timm's wider filmography. Excluded by that ruling: the Tomorrowverse/DCAMU films, the 2016
*Killing Joke*, *Justice League: Gods and Monsters*, and any film that merely reuses DCAU voice actors.

**Boundary works — all four admitted** (Hemanth, 2026-07-25, on evidence):
*Justice League vs. the Fatal Five* (2019) · *Batman and Harley Quinn* (2017) · *Batman Beyond: The
Movie* (1999) · and *Lobo* + *Gotham Girls* promoted to their own **Web Shorts** row rather than sitting
beside BTAS in TV Shows.

DCAU is also **the IP that trips the deferred chronology branch** — his own rule: *"only when the IP has
an inherent timeline to it, like MCU or DCAU then we think about starting points and stuff."* Per his
ruling, **ordering data is captured here (§6) even though the page still renders as aggregation.** The
research is paid for once; the visual treatment is decided later.

## 2. Identity and artwork

| Field | Value | Verified |
|---|---|---|
| `id` | `com.colosseum.universe.dcau` | — |
| `name` | DC Animated Universe | — |
| `logo` | `https://images.metahub.space/logo/medium/tt0103359/img` | 200 · 80 KB |
| `background` | `https://images.metahub.space/background/medium/tt0103359/img` | 200 · 257 KB |
| `resources` | `["universe"]` | spec §5.1 |

DCAU has no metahub entry of its own, so identity art is anchored to **BTAS** — the origin work and the
most recognisable image in the universe. Note the page header renders the name as **Fraunces text**
(§3), so `logo` is used only for the Home rail tile. Verified alternates if he prefers a League-wide
face: Justice League background (`tt0275137`, 961 KB) or JLU (`tt6025022`, 99 KB).

### 2.1 Artwork coverage — no holes

All **17 screen works** return 200 for both `poster/small` and `logo/medium`. Unlike One Piece (where
Fish-Man Island Log had no art at any size), **DCAU needs no `entry.poster` overrides.**

## 3. Row layout

Same inherited visual contract as One Piece — margin 54, band 360, name Fraunces 62, kicker
letter-spacing 4, section titles Fraunces 25, tiles 150×236 radius 8, gap 22. Read from
`qml/SagaUniversePage.qml`; full table in the One Piece plan §3.0. **Not re-derived here.**

Row order: `Continue` → `TV Shows` → `Web Shorts` → `Movies` → `Comics`.

## 4. The rows

Every screen ID verified by direct Cinemeta `meta` fetch (returned `name` **and** matching `type`).
Every comic post verified live against the GetComics `wp-json` API. **Nothing accepted on a
researcher's word** — see §7 for why that mattered enormously here.

### 4.1 TV Shows — `kind: video`, `type: series`, 8 entries

| # | Title | Years | Cinemeta ID | Verified as |
|---|---|---|---|---|
| 1 | Batman: The Animated Series | 1992–1995 | `tt0103359` | Batman: The Animated Series |
| 2 | Superman: The Animated Series | 1996–2000 | `tt0115378` | Superman: The Animated Series |
| 3 | The New Batman Adventures | 1997–1999 | `tt0118266` | The New Batman Adventures |
| 4 | Batman Beyond | 1999–2001 | `tt0147746` | Batman Beyond |
| 5 | Static Shock | 2000–2004 | `tt0247729` | Static Shock |
| 6 | The Zeta Project | 2001–2003 | `tt0260662` | The Zeta Project |
| 7 | Justice League | 2001–2004 | `tt0275137` | Justice League |
| 8 | Justice League Unlimited | 2004–2006 | `tt6025022` | Justice League Unlimited |

TNBA has its **own** IMDb entry, separate from BTAS — confirmed, not assumed. JLU is likewise separate
from Justice League. `tt6025022` is correct despite looking anomalously modern for a 2004 show; I
doubted it and was wrong.

### 4.2 Web Shorts — `kind: video`, `type: series`, 2 entries

| # | Title | Years | Cinemeta ID | Note |
|---|---|---|---|---|
| 1 | Lobo | 2000 | `tt6075386` | Flash web series, 14 eps. **Corrected** — see §7. |
| 2 | Gotham Girls | 2000–2002 | `tt0337763` | Flash web series, 31 eps. **Corrected** — see §7. |

Own row by Hemanth's ruling, so two minor web shorts don't read as headline shows.

### 4.3 Movies — `kind: video`, `type: movie`, 7 entries

| # | Title | Year | Cinemeta ID | Note |
|---|---|---|---|---|
| 1 | Batman: Mask of the Phantasm | 1993 | `tt0106364` | The only theatrical DCAU film. |
| 2 | Batman & Mr. Freeze: SubZero | 1998 | `tt0143127` | Direct-to-video. |
| 3 | Batman Beyond: The Movie | 1999 | `tt0231237` | The 3-part pilot re-cut as a feature. **Found by my own pass — both researchers missed it.** Included by the same logic that kept One Piece's "Episode of" re-cuts. |
| 4 | Batman Beyond: Return of the Joker | 2000 | `tt0233298` | A film, not an episode. |
| 5 | Batman: Mystery of the Batwoman | 2003 | `tt0346578` | Direct-to-video. |
| 6 | Batman and Harley Quinn | 2017 | `tt6556890` | Admitted boundary work. Timm ambivalent on canon; Hemanth ruled it in. |
| 7 | Justice League vs. the Fatal Five | 2019 | `tt8752474` | Admitted boundary work. Direct JLU continuation. **Corrected** — see §7. |

### 4.4 Comics — `kind: comic`, provider `getcomics`, 14 pinned posts

Per the spec amendment (§5.2), comics pin **explicit post IDs, never a tag.** All 14 verified live.

| # | Run | Years | GetComics post |
|---|---|---|---|
| 1 | The Batman Adventures (collection) | 1992–2004 | `11366` |
| 2 | The Batman Adventures: Mad Love | 1994 | `153724` |
| 3 | — Mad Love Deluxe Edition | 2015 | `80956` |
| 4 | Batman & Robin Adventures #1–25 + Annuals + Sub-Zero | 1995–1997 | `50187` |
| 5 | Superman Adventures #1–66 + Extras | 1996–2002 | `14615` |
| 6 | Adventures in the DC Universe #1–19 + Annual | 1997–1998 | `48881` |
| 7 | Batman: Gotham Adventures #1–60 | 1998–2003 | `15941` |
| 8 | Batman Beyond #1–24 | 1999–2001 | `190572` |
| 9 | Batman Beyond (TV tie-ins) | 2000–2002 | `163954` |
| 10 | Batman Beyond: Return of the Joker | 2001 | `282726` |
| 11 | Justice League Adventures #1–34 | 2002–2004 | `10563` |
| 12 | Gotham Girls #1–5 | 2002–2003 | `10470` |
| 13 | Batman Adventures Vol. 2 #1–17 | 2003–2004 | `183948` |
| 14 | Justice League Unlimited #1–46 | 2004–2008 | `8823` |

**No comics research was outsourced.** Hemanth caught that we already own a complete GetComics mirror —
72,322 posts in `scripts/comics_brain/getcomics_index.json` — so this section was curated locally and
verified against the live API. Four pollution traps it exposed, each of which a tag would have imported
silently:

| Trap | Evidence |
|---|---|
| Batman Beyond tag `738` = 107 posts | Includes *Batman Beyond 2.0* and *Unlimited* (2012–14) — **mainline DC, not DCAU** |
| "Justice League Unlimited" | Mixes the DCAU book (2004–08) with an unrelated **2024– DC series of the identical name** (posts `354830`…`399540`) |
| "Static Shock" | Almost entirely the New 52 title (2011–12). **DCAU Static gets no comic row at all.** |
| "Zeta Project" | **Zero posts.** GetComics simply doesn't carry it. |

The real Gotham Adventures slug is `batman-gotham-adventures`, not `gotham-adventures` — my first guess
missed, which is why slug-guessing is not a method.

## 5. Not pinned — dropped with reasons

| Item | Reason |
|---|---|
| Chase Me (2003 short) | Not in Cinemeta. The proposed ID resolved to an **adult film** — see §7. |
| Batman Beyond (2014 Darwyn Cooke short) | Not in Cinemeta. Proposed ID resolved to *Full Contact* (2015). |
| The Dark Knight's First Night (1991) | Developmental pilot; no IMDb entry exists. |
| Justice League: The First Mission (2000) | Developmental pilot; no IMDb entry exists. |
| DCAU Static Shock comics | No DCAU Static comic exists on GetComics (the tag is the New 52 book). |
| The Zeta Project comics | No posts on GetComics. |

Both surviving shorts are DVD-bonus pieces, so their absence from Cinemeta is expected, not a failure.

## 6. Chronology — captured, not yet rendered

Held for the deferred timeline treatment (spec §11). Recorded now so the research is never re-run.
Additive to the payload; a v1 renderer ignores it.

**Release order** — 1 BTAS (1992) · 2 Mask of the Phantasm (1993) · 3 STAS (1996) · 4 TNBA (1997) ·
5 SubZero (1998) · 6 Batman Beyond + BB: The Movie (1999) · 7 Lobo, Gotham Girls, Return of the Joker,
Static Shock (2000) · 8 Zeta Project, Justice League (2001) · 9 Mystery of the Batwoman (2003) ·
10 JLU (2004) · 11 Batman and Harley Quinn (2017) · 12 Fatal Five (2019).

**In-universe order** — BTAS → Mask of the Phantasm → SubZero → STAS ∥ TNBA (concurrent) → Gotham Girls
∥ Mystery of the Batwoman ∥ Batman and Harley Quinn (TNBA era) → Lobo (STAS era) → Justice League → JLU
∥ Static Shock → Fatal Five → **Batman Beyond (~2039)** → Return of the Joker (~2040s) → Zeta Project
(~2041).

⚠ **Sourcing discipline.** DCAU chronology is heavily fan-theorised. The series-level ordering above is
stated by the Wikipedia DCAU article. Two placements are **INFERRED**, not sourced, and must not be
presented as fact: *Batman and Harley Quinn* in the TNBA era (inferred from cast and art style) and the
exact in-fiction years for the Beyond era.

**The two orders genuinely diverge** — Batman Beyond and Zeta Project aired years *before* JLU but are
set decades *after* it. Any future timeline UI must pick one order and say which.

## 7. Provenance — and why verification was decisive

| Source | Contributed |
|---|---|
| Hemanth | Caught that the comics half needed no outsourcing at all — we already own the mirror. That single observation removed a whole research pass *and* exposed the tag-vs-post design flaw. |
| DeepSeek | Full screen enumeration + chronology + all 8 TV and 4 core film IDs, sourced from **Wikidata** after IMDb proved unbrowsable (it said so plainly rather than faking it). Missed *Batman Beyond: The Movie*. |
| Agent 0 machine pass | Verified all 31 entries. **Corrected 3 wrong IDs, killed 2, and found 1 work both researchers missed.** |

### The five wrong IDs

**13 of 18 proposed screen IDs were right. Five were wrong — and wrong is worse than missing**, because
a wrong ID is syntactically perfect and silently resolves to a real, different work.

| Entry | Proposed | Actually resolves to | Outcome |
|---|---|---|---|
| Justice League vs. the Fatal Five | `tt8755116` | a 1977 TV episode | fixed → `tt8752474` |
| Lobo | `tt0255758` | *Tony Brown's Journal* (1978) | fixed → `tt6075386` |
| Gotham Girls | `tt0292810` | *Kadam: Breast Cancer* | fixed → `tt0337763` |
| **Chase Me** | `tt0428284` | **an adult film** (2004) | dropped |
| Batman Beyond (2014 short) | `tt3702720` | *Full Contact* (2015) | dropped |

Note the Fatal Five failure mode: `tt8755116` vs `tt8752474` — three digits transposed, landing on a
genuine record. No citation, no reasoning and no human eye could catch that; only a fetch.

**And Chase Me is the Universe Page Law's founding incident recurring verbatim.** That law
(`docs/UNIVERSE_PAGE_LAW.md`, ratified 2026-07-13) exists because a name-based lookup once put a yaoi
anthology behind Lord of the Rings. Here, an unverified pin would have placed a pornographic title in a
**Batman** row. It looked exactly as valid as the thirteen correct IDs.

All five failures were in DeepSeek's own declared weak spot — the rate-limited "separate pass" covering
shorts, web series and the later films. Everything it drew from Wikidata in its main pass was correct.

**Standing process ruling, now twice proven:** researchers propose, the machine pass is the sole arbiter,
and no ID enters a plan without a provider fetch that returns the expected work.

## 8. Totals

**31 verified entries** — 8 TV · 2 web shorts · 7 movies · 14 comics runs
**0 manual entries** (unlike One Piece; everything DCAU is pinnable)
**0 artwork holes** — all 17 screen works have poster and logo art
**0 unverified IDs.** Nothing in §4 was accepted on a researcher's word.
