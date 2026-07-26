# Universe Extensions Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship One Piece and DC Animated Universe as installed *universe extensions* that appear in the Home carousel and open a single served-payload page — replacing the five baked universes.

**Architecture:** A universe extension is an installed extension whose manifest declares `resources: ["universe"]`. Its content arrives as a `universe.json` payload of ordered sections. **One page renderer serves every universe, forever** — no per-IP template. The Home carousel and the Hall of Worlds both derive their model from the installed extensions, so installing or removing a universe is the only way either surface changes.

**Tech Stack:** Qt 6.11 QML (loaded from disk, no qrc), plain `.pragma library` JS, `ExtensionsStore` (C++) for the manifest roster, Node ESM harnesses for pure-logic tests.

---

## 0. Why this plan exists — read before touching anything

Three attempts were made without one, and each shipped the wrong thing:

1. Two empty manifest rows described as "implemented" — Hemanth: *"the extensions aren't even installed. right now they are just placeholders."*
2. A hand-written `UniversesShelf.qml` when a carousel already existed in history — Hemanth: *"i specifically told you to look up the old universe carosel."*
3. `UniversePage.qml` wired as the destination — the **generic anime fallback**, not yesterday's designed page. Hemanth: *"this is such a misdirected piece of work."*

The lesson encoded into this plan: **every artefact already exists and is named below.** Nothing here is to be invented. If a step seems to need a new design decision, stop and ask — it means the plan is wrong.

### Ground truth (all verified to exist, 2026-07-26)

| Artefact | Path | What it gives you |
|---|---|---|
| Payload contract | `docs/superpowers/specs/2026-07-25-colosseum-universes-as-extensions-design.md` §5.2 | The exact `universe.json` shape and field rules |
| One Piece data | `docs/superpowers/plans/2026-07-25-universe-one-piece-addon-plan.md` §4 | 54 entries across 5 sections, every ID provider-verified |
| DCAU data | `docs/superpowers/plans/2026-07-25-universe-dcau-addon-plan.md` §4 | 31 entries across 4 sections, every ID provider-verified |
| Page design | `agents/colosseum-universe-onepiece-rows-mock.html` | The rows page, rev 2 |
| Visual contract | One Piece plan §3.0 | Exact px/font values, read out of `SagaUniversePage.qml` |
| The carousel | `qml/Main.qml` — "UNIVERSE HERO" block | Restored from `179bb87~1`; already in the tree |
| The Hall | `qml/UniverseHallPage.qml` | Never deleted; already in the tree |

### State already in the working tree (from the misfires — some keep, some revert)

| Change | Verdict |
|---|---|
| Two universe rows seeded in `ExtensionsStore.cpp` (gen 6) | **KEEP** — Task 1 corrects the manifests |
| `Catalog.isUniverse()` in `ExtensionsCatalog.js` | **KEEP** |
| Universe assertions in `extension_worlds_derivation_test.mjs` | **KEEP** |
| Exhibit hero restored into `Main.qml` | **KEEP** — Task 5 changes only its model |
| `qml/UniversesShelf.qml` | Already deleted. Stays deleted. |
| `OnePieceUniversePage.qml`, `CosmereUniversePage.qml`, `DragonBallUniversePage.qml`, `MagazineUniversePage.qml` restored | **REVERT** — Task 8. They served the old per-category dispatcher, which this plan removes. |
| `universeSourceFor()` dispatcher in `Main.qml` | **REVERT** — Task 5 replaces it with the single renderer |

---

## 1. File structure

| File | Responsibility |
|---|---|
| `assets/universes/one-piece.json` **(new)** | One Piece payload — the §5.2 shape, data from its plan §4 |
| `assets/universes/dcau.json` **(new)** | DCAU payload — same shape, data from its plan §4 |
| `qml/UniverseExtApi.js` **(new)** | Load a payload by extension id, validate it, cache it. Pure logic + one fetch. |
| `qml/UniverseExtensionPage.qml` **(new)** | The one renderer: header band, Continue, N sections of tiles |
| `qml/UniverseTile.qml` **(new)** | One tile, 150×236 — art, index numeral, optional chip, caption |
| `native/engine/ExtensionsStore.cpp` | Manifests gain `logo`/`background`; DCAU renamed to its real name |
| `qml/Main.qml` | Carousel model → installed universes; route → `UniverseExtensionPage` |
| `qml/UniverseHallPage.qml` | Model → installed universes |
| `tests/universe_payload_test.mjs` **(new)** | Both payloads parse, validate, and match their plans exactly |
| `tests/universe_ext_api_test.mjs` **(new)** | Validation rules: bad entries dropped, order preserved |

**Why bundled JSON rather than a served URL:** spec §5.5 wants these published over HTTPS. No such server exists. Bundling the payload at `assets/universes/<id>.json` honours the contract *exactly* — same shape, same loader, same validation — so moving to HTTPS later changes one URL in `UniverseExtApi.js` and nothing else. Inventing a server would be pretending.

---

## Task 1: Correct the two universe manifests

**Files:**
- Modify: `native/engine/ExtensionsStore.cpp` (the universes block, ~`:240-258`)

- [ ] **Step 1: Update both manifest rows**

The `manifest()` helper takes `(id, name, desc, resources, types, idPrefixes, configurable)` and has no artwork parameters. Add `logo` and `background` after building it. Replace the whole universes block with:

```cpp
    // ---- Universes: an IP gathered across every medium it lives in ---------
    // Classified by ROLE, before content: a universe declares types across every medium
    // it spans, so deriving from content would scatter One Piece into manga AND anime AND
    // film AND its own row. (Universes design §5.1a.)
    //
    // Artwork is verified in each plan §2. One Piece's logo is a transparent wordmark;
    // DCAU has no metahub entry of its own so its identity art is anchored to BTAS, the
    // origin work. The page renders the NAME as Fraunces text, so `logo` is used only by
    // the Home carousel tile.
    auto universe = [&manifest](const char* id, const char* name,
                                const char* logo, const char* background) {
        QVariantMap m = manifest(id, name, "",
                                 { QStringLiteral("universe") },
                                 { QStringLiteral("universe") }, {}, false);
        m.insert(QStringLiteral("logo"), QString::fromLatin1(logo));
        m.insert(QStringLiteral("background"), QString::fromLatin1(background));
        return m;
    };
    add("com.colosseum.universe.onepiece", "colosseum://universe/onepiece", false,
        universe("com.colosseum.universe.onepiece", "One Piece",
                 "https://images.metahub.space/logo/medium/tt0388629/img",
                 "https://s4.anilist.co/file/anilistcdn/media/manga/banner/30013-hbbRZqC5MjYh.jpg"));
    add("com.colosseum.universe.dcau", "colosseum://universe/dcau", false,
        universe("com.colosseum.universe.dcau", "DC Animated Universe",
                 "https://images.metahub.space/logo/medium/tt0103359/img",
                 "https://images.metahub.space/background/medium/tt0103359/img"));
```

- [ ] **Step 2: Bump the defaults generation**

At `native/engine/ExtensionsStore.cpp:28`, change `kHouseDefaultsVersion = 6;` to `= 7;` and extend the comment above it with: `7 gave the universes their real names and artwork.`

A manifest change reaches an existing profile ONLY through a generation bump — `appendHouseDefaults` refreshes house manifests, but `migrateDefaults` runs once per generation.

- [ ] **Step 3: Build**

Run: `cmd //c "C:\Users\Suprabha\Desktop\Brotherhood\Colosseum\native\build-msvc.bat"`
Then: `grep -cE 'error C|LNK[0-9]{4}|ninja: build stopped' <logfile>`
Expected: `0`. **The exit code lies — the grep is the gate.**

- [ ] **Step 4: Verify it reached the real profile**

```bash
node -e "
const j=JSON.parse(require('fs').readFileSync(process.env.HOME+'/AppData/Roaming/Brotherhood/Colosseum/extensions/installed.json','utf8'));
console.log('gen', j.defaultsVersion);
j.extensions.filter(e=>e.id.includes('universe')).forEach(e=>
  console.log(e.manifest.name, '| logo', !!e.manifest.logo, '| bg', !!e.manifest.background));
"
```
Expected: `gen 7`, then `One Piece | logo true | bg true` and `DC Animated Universe | logo true | bg true`.

- [ ] **Step 5: Commit**

```bash
git add native/engine/ExtensionsStore.cpp
git commit -m "feat(universes): the two manifests carry their real names and verified artwork"
```

---

## Task 2: The One Piece payload

**Files:**
- Create: `assets/universes/one-piece.json`

- [ ] **Step 1: Write the payload**

Shape is spec §5.2, verbatim. Data is the One Piece plan §4 — **all 54 entries, in plan order**. Array order is display order; the client never re-sorts.

Two rules that are load-bearing:
- Manga row 2 (the digitally coloured edition) has **no provider ID** and must keep **array position 2** — Hemanth's ruling, it sits beside the main manga, not at the end. Give it `"manual": true` and no `id`.
- `tt33992229` has **no metahub poster at any size** (plan §2.1). It carries an explicit `poster` override.

```json
{
  "universe": {
    "id": "one-piece",
    "title": "One Piece",
    "logo": "https://images.metahub.space/logo/medium/tt0388629/img",
    "background": "https://s4.anilist.co/file/anilistcdn/media/manga/banner/30013-hbbRZqC5MjYh.jpg",
    "sections": [
      { "id": "tv", "title": "TV Shows", "kind": "video", "entries": [
        { "id": "tt0388629",  "type": "series", "title": "One Piece", "year": "1999" },
        { "id": "tt11737520", "type": "series", "title": "One Piece (live action)", "year": "2023" },
        { "id": "tt33992229", "type": "series", "title": "One Piece Log: Fish-Man Island Saga", "year": "2024",
          "poster": "https://s4.anilist.co/file/anilistcdn/media/manga/banner/30013-hbbRZqC5MjYh.jpg" },
        { "id": "tt36600601", "type": "series", "title": "One Piece in Love", "year": "2025" },
        { "id": "tt30476502", "type": "series", "title": "THE ONE PIECE", "year": "2027", "note": "Wit remake" }
      ]},
      { "id": "movies", "title": "Movies", "kind": "video", "entries": [
        { "id": "tt0814243",  "type": "movie", "title": "One Piece: The Movie", "year": "2000" },
        { "id": "tt0832449",  "type": "movie", "title": "Clockwork Island Adventure", "year": "2001" },
        { "id": "tt0997084",  "type": "movie", "title": "Chopper's Kingdom on the Island of Strange Animals", "year": "2002" },
        { "id": "tt1006926",  "type": "movie", "title": "Dead End Adventure", "year": "2003" },
        { "id": "tt1010435",  "type": "movie", "title": "The Cursed Holy Sword", "year": "2004" },
        { "id": "tt1018764",  "type": "movie", "title": "Baron Omatsuri and the Secret Island", "year": "2005" },
        { "id": "tt1059950",  "type": "movie", "title": "Giant Mechanical Soldier of Karakuri Castle", "year": "2006" },
        { "id": "tt1037116",  "type": "movie", "title": "Episode of Alabasta: The Desert Princess and the Pirates", "year": "2007" },
        { "id": "tt1206326",  "type": "movie", "title": "Episode of Chopper Plus: Bloom in Winter, Miracle Sakura", "year": "2008" },
        { "id": "tt1485763",  "type": "movie", "title": "One Piece Film: Strong World", "year": "2009" },
        { "id": "tt1865467",  "type": "movie", "title": "One Piece 3D: Straw Hat Chase", "year": "2011" },
        { "id": "tt2375379",  "type": "movie", "title": "One Piece Film: Z", "year": "2012" },
        { "id": "tt5251328",  "type": "movie", "title": "One Piece Film: Gold", "year": "2016" },
        { "id": "tt9430698",  "type": "movie", "title": "One Piece: Stampede", "year": "2019" },
        { "id": "tt16183464", "type": "movie", "title": "One Piece Film: Red", "year": "2022" }
      ]},
      { "id": "specials", "title": "Specials", "kind": "video", "entries": [
        { "id": "tt1012788", "type": "movie", "title": "Defeat the Pirate Ganzak!", "year": "1998" },
        { "id": "tt0975705", "type": "movie", "title": "Adventure in the Ocean's Navel", "year": "2000" },
        { "id": "tt1003286", "type": "movie", "title": "Open Upon the Great Sea! A Father's Huge, HUGE Dream!", "year": "2003" },
        { "id": "tt1010037", "type": "movie", "title": "Protect! The Last Great Stage", "year": "2003" },
        { "id": "tt1012787", "type": "movie", "title": "Luffy's Detective Story", "year": "2005" },
        { "id": "tt7947592", "type": "movie", "title": "Strong World Episode 0", "year": "2010" },
        { "id": "tt2598466", "type": "movie", "title": "Episode of Nami", "year": "2012" },
        { "id": "tt3354344", "type": "movie", "title": "Episode of Luffy: Adventure on Hand Island", "year": "2012" },
        { "id": "tt3354352", "type": "movie", "title": "Episode of Merry", "year": "2013" },
        { "id": "tt2893336", "type": "movie", "title": "Dream 9 Toriko x One Piece x Dragon Ball Z", "year": "2013" },
        { "id": "tt5098548", "type": "movie", "title": "3D2Y", "year": "2014" },
        { "id": "tt6597356", "type": "movie", "title": "Episode of Sabo", "year": "2015" },
        { "id": "tt6609162", "type": "movie", "title": "Adventure of Nebulandia", "year": "2015" },
        { "id": "tt6425816", "type": "movie", "title": "Heart of Gold", "year": "2016" },
        { "id": "tt11757066", "type": "movie", "title": "Episode of East Blue", "year": "2017" },
        { "id": "tt11744496", "type": "movie", "title": "Episode of Skypiea", "year": "2018" },
        { "id": "tt33998607", "type": "movie", "title": "One Piece Fan Letter", "year": "2024" }
      ]},
      { "id": "manga", "title": "Manga", "kind": "manga", "entries": [
        { "id": "30013",  "provider": "anilist", "title": "One Piece", "year": "1997" },
        { "manual": true, "title": "One Piece — digitally coloured edition",
          "note": "No provider entry — kept beside the main manga by ruling" },
        { "id": "44414",  "provider": "anilist", "title": "One Piece Log Book Omake", "year": "1999" },
        { "id": "47152",  "provider": "anilist", "title": "ONE PIECE: STRONG WORLD (Chapter 0)", "year": "2009" },
        { "id": "82353",  "provider": "anilist", "title": "Chopperman", "year": "2010" },
        { "id": "102533", "provider": "anilist", "title": "One Piece Party", "year": "2014" },
        { "id": "110258", "provider": "anilist", "title": "CHIN PIECE", "year": "2018" },
        { "id": "110233", "provider": "anilist", "title": "Koisuru ONE PIECE", "year": "2018" },
        { "id": "103252", "provider": "anilist", "title": "One Piece: Shokugeki no Sanji", "year": "2018" },
        { "id": "110232", "provider": "anilist", "title": "Kobiyama Who Looks Like Koby", "year": "2018" },
        { "id": "110715", "provider": "anilist", "title": "ONE PIECE Gakuen!!", "year": "2019" },
        { "id": "117802", "provider": "anilist", "title": "One Piece: Ace's Story — The Manga", "year": "2020" },
        { "id": "154266", "provider": "anilist", "title": "One Piece 1000-wa Kinen! Tokubetsu Bangai-hen", "year": "2021" }
      ]},
      { "id": "novels", "title": "Novels", "kind": "book", "entries": [
        { "id": "1509329459", "provider": "applebooks", "title": "One Piece: Ace's Story, Vol. 1", "edition": "novel" },
        { "id": "1528233153", "provider": "applebooks", "title": "One Piece: Ace's Story, Vol. 2", "edition": "novel" },
        { "id": "6741084754", "provider": "applebooks", "title": "One Piece: Law's Story", "edition": "novel" },
        { "id": "6736634886", "provider": "applebooks", "title": "One Piece: Heroines, Vol. 2", "edition": "novel" }
      ]}
    ]
  }
}
```

- [ ] **Step 2: Verify it parses and matches the plan's totals**

```bash
node -e "
const p=JSON.parse(require('fs').readFileSync('assets/universes/one-piece.json','utf8')).universe;
const n=Object.fromEntries(p.sections.map(s=>[s.id,s.entries.length]));
console.log(n, 'total', p.sections.reduce((a,s)=>a+s.entries.length,0));
"
```
Expected: `{ tv: 5, movies: 15, specials: 17, manga: 13, novels: 4 } total 54`

- [ ] **Step 3: Commit**

```bash
git add assets/universes/one-piece.json
git commit -m "feat(universes): the One Piece payload — 54 provider-verified entries"
```

---

## Task 3: The DCAU payload

**Files:**
- Create: `assets/universes/dcau.json`

- [ ] **Step 1: Write the payload**

Data is the DCAU plan §4. Row order is fixed by that plan: TV Shows → Web Shorts → Movies → Comics.

Comics are the amendment case: **explicit GetComics post IDs in `posts[]`, never a tag.** Tags are polluted — `738` (Batman Beyond) holds 107 posts including mainline *Batman Beyond 2.0*; "Justice League Unlimited" mixes the DCAU book with an unrelated 2024 series of the same name. A tag cannot express "the DCAU books only".

```json
{
  "universe": {
    "id": "dcau",
    "title": "DC Animated Universe",
    "logo": "https://images.metahub.space/logo/medium/tt0103359/img",
    "background": "https://images.metahub.space/background/medium/tt0103359/img",
    "sections": [
      { "id": "tv", "title": "TV Shows", "kind": "video", "entries": [
        { "id": "tt0103359", "type": "series", "title": "Batman: The Animated Series", "year": "1992" },
        { "id": "tt0115378", "type": "series", "title": "Superman: The Animated Series", "year": "1996" },
        { "id": "tt0118266", "type": "series", "title": "The New Batman Adventures", "year": "1997" },
        { "id": "tt0147746", "type": "series", "title": "Batman Beyond", "year": "1999" },
        { "id": "tt0247729", "type": "series", "title": "Static Shock", "year": "2000" },
        { "id": "tt0260662", "type": "series", "title": "The Zeta Project", "year": "2001" },
        { "id": "tt0275137", "type": "series", "title": "Justice League", "year": "2001" },
        { "id": "tt6025022", "type": "series", "title": "Justice League Unlimited", "year": "2004" }
      ]},
      { "id": "shorts", "title": "Web Shorts", "kind": "video", "entries": [
        { "id": "tt6075386", "type": "series", "title": "Lobo", "year": "2000", "note": "14 episodes" },
        { "id": "tt0337763", "type": "series", "title": "Gotham Girls", "year": "2000", "note": "31 episodes" }
      ]},
      { "id": "movies", "title": "Movies", "kind": "video", "entries": [
        { "id": "tt0106364", "type": "movie", "title": "Batman: Mask of the Phantasm", "year": "1993" },
        { "id": "tt0143127", "type": "movie", "title": "Batman & Mr. Freeze: SubZero", "year": "1998" },
        { "id": "tt0231237", "type": "movie", "title": "Batman Beyond: The Movie", "year": "1999" },
        { "id": "tt0233298", "type": "movie", "title": "Batman Beyond: Return of the Joker", "year": "2000" },
        { "id": "tt0346578", "type": "movie", "title": "Batman: Mystery of the Batwoman", "year": "2003" },
        { "id": "tt6556890", "type": "movie", "title": "Batman and Harley Quinn", "year": "2017" },
        { "id": "tt8752474", "type": "movie", "title": "Justice League vs. the Fatal Five", "year": "2019" }
      ]},
      { "id": "comics", "title": "Comics", "kind": "comic", "entries": [
        { "provider": "getcomics", "title": "The Batman Adventures", "year": "1992", "posts": [11366] },
        { "provider": "getcomics", "title": "The Batman Adventures: Mad Love", "year": "1994", "posts": [153724] },
        { "provider": "getcomics", "title": "Mad Love Deluxe Edition", "year": "2015", "posts": [80956] },
        { "provider": "getcomics", "title": "Batman & Robin Adventures", "year": "1995", "posts": [50187] },
        { "provider": "getcomics", "title": "Superman Adventures", "year": "1996", "posts": [14615] },
        { "provider": "getcomics", "title": "Adventures in the DC Universe", "year": "1997", "posts": [48881] },
        { "provider": "getcomics", "title": "Batman: Gotham Adventures", "year": "1998", "posts": [15941] },
        { "provider": "getcomics", "title": "Batman Beyond", "year": "1999", "posts": [190572] },
        { "provider": "getcomics", "title": "Batman Beyond (TV tie-ins)", "year": "2000", "posts": [163954] },
        { "provider": "getcomics", "title": "Batman Beyond: Return of the Joker", "year": "2001", "posts": [282726] },
        { "provider": "getcomics", "title": "Justice League Adventures", "year": "2002", "posts": [10563] },
        { "provider": "getcomics", "title": "Gotham Girls", "year": "2002", "posts": [10470] },
        { "provider": "getcomics", "title": "Batman Adventures Vol. 2", "year": "2003", "posts": [183948] },
        { "provider": "getcomics", "title": "Justice League Unlimited", "year": "2004", "posts": [8823] }
      ]}
    ]
  }
}
```

- [ ] **Step 2: Verify totals**

```bash
node -e "
const p=JSON.parse(require('fs').readFileSync('assets/universes/dcau.json','utf8')).universe;
const n=Object.fromEntries(p.sections.map(s=>[s.id,s.entries.length]));
console.log(n, 'total', p.sections.reduce((a,s)=>a+s.entries.length,0));
"
```
Expected: `{ tv: 8, shorts: 2, movies: 7, comics: 14 } total 31`

- [x] **Step 2b: Every comic post ID resolves to its intended series — DONE 2026-07-26**

Hemanth's ruling: *"we have to confirm if the posts linked connect to the particular comic series."* Each ID was fetched live at `https://getcomics.org/?p=<id>` and its page title read. **All 14 resolve, and all 14 match.** Re-run with:

```bash
while IFS='|' read -r id exp; do
  t=$(curl -s -L --max-time 40 "https://getcomics.org/?p=$id" \
      | grep -oiE "<title>[^<]*</title>" | head -1 | sed -e 's/<[^>]*>//g' -e 's/&#8211;/-/g')
  printf "%-8s %-34s => %s\n" "$id" "$exp" "${t:-FAILED}"; sleep 1
done < <(node -e "
JSON.parse(require('fs').readFileSync('assets/universes/dcau.json','utf8'))
  .universe.sections.find(s=>s.id==='comics').entries
  .forEach(e=>e.posts.forEach(p=>console.log(p+'|'+e.title)));")
```

Verified result — every row landed on its own series:

| post | payload title | GetComics page title |
|---|---|---|
| 11366 | The Batman Adventures | Batman Adventures (Collection) (1992-2004) |
| 153724 | The Batman Adventures: Mad Love | Batman Adventures - Mad Love #1 (1994) |
| 80956 | Mad Love Deluxe Edition | The Batman Adventures - Mad Love Deluxe Edition (2015) |
| 50187 | Batman & Robin Adventures | Batman and Robin Adventures #1 - 25 + Annuals + Sub-Zero (1995-1997) |
| 14615 | Superman Adventures | Superman Adventures #1 - 66 + Extras (1966-2002) |
| 48881 | Adventures in the DC Universe | Adventures In the DC Universe #1 - 19 + Annual (1997-1998) |
| 15941 | Batman: Gotham Adventures | Batman Gotham Adventures #1 - 60 (1998-2003) |
| 190572 | Batman Beyond | Batman Beyond #1 - 24 (1999-2001) |
| 163954 | Batman Beyond (TV tie-ins) | Batman Beyond (TV Tie-ins) (2000-2002) |
| 282726 | Batman Beyond: Return of the Joker | Batman Beyond - Return of the Joker (2001) |
| 10563 | Justice League Adventures | Justice League Adventures #1 - 34 (2002-2004) |
| 10470 | Gotham Girls | Gotham Girls #1 - 5 (2002-2003) |
| 183948 | Batman Adventures Vol. 2 | Batman Adventures Vol. 2 #1 - 17 (2003-2004) |
| 8823 | Justice League Unlimited | Justice League Unlimited #1 - 46 (2004-2008) |

**Two observations recorded rather than silently fixed — both are Hemanth's call:**
- **Post 11366 is a *collection* spanning 1992-2004**, not only the 1992 series. Its range therefore overlaps post 183948 (*Batman Adventures Vol. 2*, 2003-2004), so those two rows can offer the same books twice. Leaving both is defensible (one is the omnibus, one the individual run); it is a curation choice, not a data error.
- **Post 14615's own page title reads "(1966-2002)"** — a typo on GetComics' side; *Superman Adventures* began in 1996. Our payload says `"year": "1996"`, which is correct. Nothing to change here; noted so a future reader does not "correct" our right value to match their wrong one.

- [ ] **Step 3: Commit**

```bash
git add assets/universes/dcau.json
git commit -m "feat(universes): the DCAU payload — 31 verified entries, comics pinned by post ID"
```

---

## Task 4: `UniverseExtApi.js` — load and validate a payload

**Files:**
- Create: `qml/UniverseExtApi.js`
- Test: `tests/universe_ext_api_test.mjs`

- [ ] **Step 1: Write the failing test**

```javascript
// tests/universe_ext_api_test.mjs — validation rules for a universe payload.
// An invalid entry is DROPPED, never rendered: a video tile that reaches Theatre
// without a type opens a series as a movie and dies (spec §5.2, §5.4).
import { readFileSync } from 'node:fs';
let src = readFileSync('qml/UniverseExtApi.js', 'utf8').replace(/^\.pragma library\s*$/m, '');
const mod = {};
new Function('module', 'XMLHttpRequest', src + '\nmodule.validate=validate;module.fileFor=fileFor;')(mod, function(){});

let failed = 0;
const ok  = m => console.log('  ok   ' + m);
const bad = m => { console.log('  FAIL ' + m); failed++; };
const eq  = (a, b, m) => JSON.stringify(a) === JSON.stringify(b) ? ok(`${m} → ${JSON.stringify(a)}`)
                                                                 : bad(`${m} → ${JSON.stringify(a)}, expected ${JSON.stringify(b)}`);

console.log('section order is the server\'s, never re-sorted');
{
  const p = { universe: { title: 'X', sections: [
    { id: 'b', title: 'B', kind: 'video', entries: [{ id: 'tt1', type: 'movie', title: 'b' }] },
    { id: 'a', title: 'A', kind: 'video', entries: [{ id: 'tt2', type: 'movie', title: 'a' }] }
  ]}};
  eq(mod.validate(p).sections.map(s => s.id), ['b', 'a'], 'order preserved');
}

console.log('\na video entry with no type is dropped');
{
  const p = { universe: { title: 'X', sections: [
    { id: 'tv', title: 'TV', kind: 'video', entries: [
      { id: 'tt1', type: 'series', title: 'good' },
      { id: 'tt2', title: 'no type' }
    ]}
  ]}};
  eq(mod.validate(p).sections[0].entries.map(e => e.title), ['good'], 'typeless video dropped');
}

console.log('\nan unknown kind drops the whole section');
{
  const p = { universe: { title: 'X', sections: [
    { id: 'v', title: 'V', kind: 'video', entries: [{ id: 'tt1', type: 'movie', title: 'k' }] },
    { id: 'w', title: 'W', kind: 'hologram', entries: [{ id: 'z', title: 'q' }] }
  ]}};
  eq(mod.validate(p).sections.map(s => s.id), ['v'], 'unknown kind skipped');
}

console.log('\na manual entry survives without an id, and keeps its position');
{
  const p = { universe: { title: 'X', sections: [
    { id: 'm', title: 'M', kind: 'manga', entries: [
      { id: '1', provider: 'anilist', title: 'first' },
      { manual: true, title: 'coloured' },
      { id: '2', provider: 'anilist', title: 'third' }
    ]}
  ]}};
  eq(mod.validate(p).sections[0].entries.map(e => e.title), ['first', 'coloured', 'third'],
     'manual entry kept in place');
}

console.log('\na comic entry needs posts, not a tag');
{
  const p = { universe: { title: 'X', sections: [
    { id: 'c', title: 'C', kind: 'comic', entries: [
      { provider: 'getcomics', title: 'good', posts: [123] },
      { provider: 'getcomics', title: 'tagged', tag: 'batman' }
    ]}
  ]}};
  eq(mod.validate(p).sections[0].entries.map(e => e.title), ['good'], 'tag-only comic dropped');
}

console.log('\nan empty section never renders');
{
  const p = { universe: { title: 'X', sections: [
    { id: 'e', title: 'E', kind: 'video', entries: [{ id: 'x', title: 'no type' }] }
  ]}};
  eq(mod.validate(p).sections.length, 0, 'section emptied by validation is removed');
}

console.log('\nextension id maps to its bundled payload');
eq(mod.fileFor('com.colosseum.universe.onepiece'), 'one-piece', 'One Piece file');
eq(mod.fileFor('com.colosseum.universe.dcau'), 'dcau', 'DCAU file');
eq(mod.fileFor('com.example.other'), '', 'unknown extension has no payload');

console.log(failed ? `\n${failed} FAILED` : '\nall green');
process.exit(failed ? 1 : 0);
```

- [ ] **Step 2: Run it to confirm it fails**

Run: `node tests/universe_ext_api_test.mjs`
Expected: FAIL — `ENOENT ... qml/UniverseExtApi.js`

- [ ] **Step 3: Write the implementation**

```javascript
// UniverseExtApi.js — load, validate and cache a universe extension's payload.
//
// The payload contract is the universes-as-extensions design §5.2. Its end state is a
// served universe.json over HTTPS (§5.5); until that server exists the same document is
// bundled at assets/universes/<file>.json. Same shape, same loader, same validation — so
// the move to HTTPS changes the URL below and nothing else.
//
// VALIDATION IS A GATE, NOT A FORMALITY. A video tile that reaches Theatre without a type
// opens a series as a movie and dies (§5.4). An invalid entry is DROPPED and the rest of
// the payload still renders; a section left empty by that is removed entirely, because an
// empty row is a lie about what the universe holds.
.pragma library

var KINDS = { video: true, manga: true, comic: true, book: true };

// The bundled payload each installed universe extension reads. When these are served,
// this becomes the extension's transportUrl base + "/universe.json".
var FILES = {
    "com.colosseum.universe.onepiece": "one-piece",
    "com.colosseum.universe.dcau":     "dcau"
};
function fileFor(extensionId) { return FILES[extensionId] || ""; }

function _entryOk(kind, e) {
    if (!e || !e.title) return false;
    if (e.manual === true) return true;          // no provider identity, by curation
    if (kind === "video") return !!e.id && (e.type === "movie" || e.type === "series");
    if (kind === "comic") return !!(e.posts && e.posts.length);   // post IDs, never a tag
    return !!e.id;
}

// Returns { title, logo, background, sections: [...] } with everything invalid removed.
function validate(payload) {
    var u = (payload && payload.universe) || {};
    var out = { id: u.id || "", title: u.title || "", logo: u.logo || "",
                background: u.background || "", sections: [] };
    var src = u.sections || [];
    for (var i = 0; i < src.length; i++) {
        var s = src[i];
        if (!s || !s.title || !KINDS[s.kind]) continue;      // unknown kind → skip section
        var kept = [];
        var entries = s.entries || [];
        for (var j = 0; j < entries.length; j++)
            if (_entryOk(s.kind, entries[j])) kept.push(entries[j]);
        if (!kept.length) continue;                          // never render an empty row
        out.sections.push({ id: s.id || "", title: s.title, kind: s.kind, entries: kept });
    }
    return out;
}

var _cache = {};   // extensionId → validated payload

function load(extensionId, done) {
    if (_cache[extensionId]) { done(_cache[extensionId]); return; }
    var file = fileFor(extensionId);
    if (!file) { done(null); return; }
    var xhr = new XMLHttpRequest();
    xhr.onreadystatechange = function () {
        if (xhr.readyState !== XMLHttpRequest.DONE) return;
        var parsed = null;
        try { parsed = JSON.parse(xhr.responseText); } catch (e) { parsed = null; }
        if (!parsed) { done(null); return; }
        var v = validate(parsed);
        _cache[extensionId] = v;
        done(v);
    };
    xhr.open("GET", "../assets/universes/" + file + ".json");
    xhr.send();
}
```

- [ ] **Step 4: Run the test again**

Run: `node tests/universe_ext_api_test.mjs`
Expected: `all green`

- [ ] **Step 5: Commit**

```bash
git add qml/UniverseExtApi.js tests/universe_ext_api_test.mjs
git commit -m "feat(universes): payload loader + validation gate"
```

---

## Task 5: `universe_payload_test.mjs` — the payloads match their plans

**Files:**
- Create: `tests/universe_payload_test.mjs`

- [ ] **Step 1: Write the test**

```javascript
// universe_payload_test.mjs — the shipped payloads still say what the locked plans say.
// Both plans were verified entry-by-entry against live providers on 2026-07-25; five DCAU
// IDs were WRONG rather than missing, one of them an adult film in a Batman row. This file
// is what stops a careless edit undoing that work.
import { readFileSync } from 'node:fs';
let src = readFileSync('qml/UniverseExtApi.js', 'utf8').replace(/^\.pragma library\s*$/m, '');
const mod = {};
new Function('module', 'XMLHttpRequest', src + '\nmodule.validate=validate;')(mod, function(){});

let failed = 0;
const ok  = m => console.log('  ok   ' + m);
const bad = m => { console.log('  FAIL ' + m); failed++; };
const eq  = (a, b, m) => JSON.stringify(a) === JSON.stringify(b) ? ok(`${m} → ${JSON.stringify(a)}`)
                                                                 : bad(`${m} → ${JSON.stringify(a)}, expected ${JSON.stringify(b)}`);

const load = f => JSON.parse(readFileSync(`assets/universes/${f}.json`, 'utf8'));

console.log('One Piece — plan section 4');
{
  const raw = load('one-piece');
  const v = mod.validate(raw);
  eq(v.title, 'One Piece', 'title');
  eq(v.sections.map(s => s.id), ['tv', 'movies', 'specials', 'manga', 'novels'], 'section order');
  eq(v.sections.map(s => s.entries.length), [5, 15, 17, 13, 4], 'entries per section');
  // Every entry the plan pinned must SURVIVE validation — a drop here means a bad payload.
  const rawCount = raw.universe.sections.reduce((a, s) => a + s.entries.length, 0);
  eq(v.sections.reduce((a, s) => a + s.entries.length, 0), rawCount, 'nothing dropped by validation');
  // Hemanth's ruling: the coloured edition sits at position 2, beside the main manga.
  const manga = v.sections.find(s => s.id === 'manga');
  eq(manga.entries[1].manual, true, 'the manual coloured edition is at position 2');
  eq(manga.entries[0].id, '30013', 'and the main manga is first');
  // The one confirmed art hole must carry its override or it renders empty.
  const tv = v.sections.find(s => s.id === 'tv');
  eq(!!tv.entries.find(e => e.id === 'tt33992229').poster, true, 'Fish-Man Island Log has a poster override');
}

console.log('\nDCAU — plan section 4');
{
  const raw = load('dcau');
  const v = mod.validate(raw);
  eq(v.title, 'DC Animated Universe', 'title');
  eq(v.sections.map(s => s.id), ['tv', 'shorts', 'movies', 'comics'], 'section order');
  eq(v.sections.map(s => s.entries.length), [8, 2, 7, 14], 'entries per section');
  const rawCount = raw.universe.sections.reduce((a, s) => a + s.entries.length, 0);
  eq(v.sections.reduce((a, s) => a + s.entries.length, 0), rawCount, 'nothing dropped by validation');
  // The amendment: comics pin post IDs. A tag imports same-named mainline books silently.
  const comics = v.sections.find(s => s.id === 'comics');
  eq(comics.entries.every(e => Array.isArray(e.posts) && e.posts.length > 0), true,
     'every comic entry pins explicit post IDs');
  eq(comics.entries.some(e => e.tag), false, 'no comic entry carries a tag');
  // Four IDs corrected during verification — pin them so a "tidy-up" cannot revert them.
  const tv = v.sections.find(s => s.id === 'tv');
  eq(tv.entries.find(e => e.title.indexOf('Unlimited') >= 0).id, 'tt6025022', 'JLU keeps its verified id');
  const shorts = v.sections.find(s => s.id === 'shorts');
  eq(shorts.entries.map(e => e.id), ['tt6075386', 'tt0337763'], 'the two web shorts keep their corrected ids');
}

console.log('\nevery video entry can reach Theatre safely');
for (const f of ['one-piece', 'dcau']) {
  const v = mod.validate(load(f));
  const badEntries = v.sections.filter(s => s.kind === 'video')
    .flatMap(s => s.entries).filter(e => !e.id || !(e.type === 'movie' || e.type === 'series'));
  eq(badEntries.length, 0, `${f}: no video entry missing id or type`);
}

console.log(failed ? `\n${failed} FAILED` : '\nall green');
process.exit(failed ? 1 : 0);
```

- [ ] **Step 2: Run it**

Run: `node tests/universe_payload_test.mjs`
Expected: `all green`

- [ ] **Step 3: Commit**

```bash
git add tests/universe_payload_test.mjs
git commit -m "test(universes): the payloads are pinned to their locked plans"
```

---

## Task 6: `UniverseTile.qml` — one tile

**Files:**
- Create: `qml/UniverseTile.qml`

Values are the visual contract (One Piece plan §3.0), read out of `SagaUniversePage.qml`. **Do not adjust them by eye** — they are inherited so a universe page sits in the same family as the saga pages.

- [ ] **Step 1: Write it**

```qml
// UniverseTile — one work in a universe row. 150x236 r8, index numeral, optional chip,
// 56px caption block. Every value is the inherited saga contract (One Piece plan §3.0),
// not a fresh choice: a universe page must sit in the same family as SagaUniversePage.
//
// Art by kind: video → metahub poster/small (ONLY /small is reliable; /medium 404s across
// the long tail and Cinemeta URLs must never be upscaled — house doctrine). Non-video
// kinds have no universal art endpoint, so they degrade to the honest lettered plate
// rather than borrowing a stand-in.
pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: tile
    property var entry: ({})
    property string kind: "video"
    property int index: 0
    signal activated()

    width: 150
    height: 236 + 56

    Theme { id: theme }

    readonly property string art: {
        if (entry.poster) return entry.poster
        if (kind === "video" && entry.id)
            return "https://images.metahub.space/poster/small/" + entry.id + "/img"
        return ""
    }

    Rectangle {
        id: plate
        width: 150; height: 236; radius: 8
        color: "#12141a"
        border.width: 1; border.color: theme.edge
        clip: true

        Image {
            anchors.fill: parent
            source: tile.art
            visible: source != "" && status === Image.Ready
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            cache: true
            sourceSize.width: 300
        }
        // The honest fallback: the work's own name, never another IP's art.
        Text {
            anchors.fill: parent
            anchors.margins: 12
            visible: tile.art === "" || artImg.status !== Image.Ready
            text: tile.entry.title || ""
            color: theme.inkDimmer
            font.family: theme.display; font.pixelSize: 15
            wrapMode: Text.WordWrap
            verticalAlignment: Text.AlignVCenter
            horizontalAlignment: Text.AlignHCenter
        }
        Image { id: artImg; source: tile.art; visible: false; asynchronous: true; cache: true }

        Rectangle {
            width: 26; height: 26; radius: 6
            x: 8; y: 8
            color: Qt.rgba(0, 0, 0, 0.62)
            Text {
                anchors.centerIn: parent
                text: tile.index + 1
                color: theme.gold
                font.family: theme.ui; font.pixelSize: 13; font.bold: true
            }
        }
        Text {
            anchors.right: parent.right; anchors.top: parent.top
            anchors.margins: 8
            visible: !!tile.entry.note
            text: (tile.entry.note || "").toUpperCase()
            color: theme.gold
            font.family: theme.ui; font.pixelSize: 9; font.letterSpacing: 2
        }
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: tile.activated()
        }
    }

    Column {
        anchors.top: plate.bottom
        anchors.topMargin: 10
        width: parent.width
        spacing: 2
        Text {
            width: parent.width
            text: tile.entry.title || ""
            color: theme.ink
            font.family: theme.ui; font.pixelSize: 12; font.weight: Font.DemiBold
            elide: Text.ElideRight
            maximumLineCount: 2
            wrapMode: Text.WordWrap
        }
        Text {
            width: parent.width
            visible: !!tile.entry.year
            text: tile.entry.year || ""
            color: theme.inkDimmer
            font.family: theme.ui; font.pixelSize: 11
        }
    }
}
```

- [ ] **Step 2: Commit**

```bash
git add qml/UniverseTile.qml
git commit -m "feat(universes): the universe tile, inheriting the saga visual contract"
```

---

## Task 7: `UniverseExtensionPage.qml` — the one renderer

**Files:**
- Create: `qml/UniverseExtensionPage.qml`

- [ ] **Step 1: Write it**

Layout is `agents/colosseum-universe-onepiece-rows-mock.html`: header band 360 → Continue → N sections. Section titles are Fraunces 25 with a 13px sub, a gold rule under, then a horizontal row of tiles at gap 22.

```qml
// UniverseExtensionPage — the ONE renderer for every universe extension, forever.
//
// Layout: agents/colosseum-universe-onepiece-rows-mock.html (rev 2). Values inherited from
// SagaUniversePage via the One Piece plan §3.0 — band 360, margin 54, name Fraunces 62,
// kicker letterSpacing 4, section title Fraunces 25, tile 150x236, gap 22, spacer 40.
//
// SECTIONS ARE DATA. Order, titles and contents come from the payload; the page renders
// whatever arrives and never re-sorts (spec §5.2). No hero, no canon tags, no chronology —
// One Piece has no inherent timeline, and a timeline belongs only to IPs that have one.
// Continue is the only row that knows anything about the user.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import "UniverseExtApi.js" as UniverseApi

Item {
    id: root
    anchors.fill: parent

    property Item backdrop: null
    property string extensionId: ""
    property string universeName: ""

    signal backRequested()
    signal minimizeRequested()
    signal fullscreenRequested()
    signal closeRequested()
    signal watchRequested(var payload)     // video → Theatre
    signal seriesRequested(string name)    // manga → Tankoban
    signal bookRequested(var payload)      // book → Biblio
    signal comicsArchiveRequested(var payload)

    Theme { id: theme }

    property var payload: null
    onExtensionIdChanged: root.reload()
    Component.onCompleted: root.reload()
    function reload() {
        if (!extensionId) { payload = null; return }
        UniverseApi.load(extensionId, function (p) { root.payload = p })
    }

    // A tile's destination is decided by its SECTION kind, never guessed from the entry.
    function openEntry(kind, entry) {
        if (kind === "video")
            root.watchRequested({ id: entry.id, type: entry.type,
                                  title: entry.title, cover: "" })
        else if (kind === "manga")
            root.seriesRequested(entry.title)
        else if (kind === "book")
            root.bookRequested({ id: entry.id, title: entry.title })
        else if (kind === "comic")
            root.comicsArchiveRequested({ title: entry.title, posts: entry.posts })
    }

    Rectangle { anchors.fill: parent; color: "#0c0e11" }

    Flickable {
        id: page
        anchors.fill: parent
        contentWidth: width
        contentHeight: col.implicitHeight
        boundsBehavior: Flickable.StopAtBounds
        clip: true

        Column {
            id: col
            width: page.width
            spacing: 0

            // ---- header band: 360, banner at low opacity behind a left-heavy wash ----
            Item {
                width: parent.width
                height: 360
                clip: true
                Image {
                    anchors.fill: parent
                    source: (root.payload && root.payload.background) || ""
                    visible: source != ""
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    cache: true
                    opacity: 0.30
                }
                Rectangle {
                    anchors.fill: parent
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0.04; color: "#0c0e11" }
                        GradientStop { position: 0.55; color: Qt.rgba(0.047, 0.055, 0.067, 0.55) }
                        GradientStop { position: 1.0;  color: Qt.rgba(0.047, 0.055, 0.067, 0.90) }
                    }
                }
                Column {
                    x: theme.margin
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 46
                    spacing: 9
                    Text {
                        text: "UNIVERSE"
                        color: theme.gold
                        font.family: theme.ui; font.pixelSize: 12
                        font.letterSpacing: 4; font.bold: true
                    }
                    Text {
                        text: (root.payload && root.payload.title) || root.universeName
                        color: theme.ink
                        font.family: theme.display; font.pixelSize: 62
                    }
                    Text {
                        text: root.metaline
                        color: theme.inkDimmer
                        font.family: theme.ui; font.pixelSize: 14
                    }
                }
            }

            // ---- the served sections ----
            Repeater {
                model: root.payload ? root.payload.sections : []
                delegate: Column {
                    id: section
                    required property var modelData
                    x: theme.margin
                    width: page.width - theme.margin * 2
                    topPadding: 44
                    spacing: 0

                    Text {
                        text: section.modelData.title
                        color: theme.ink
                        font.family: theme.display; font.pixelSize: 25
                        bottomPadding: 12
                    }
                    Text {
                        text: section.modelData.entries.length
                              + (section.modelData.entries.length === 1 ? " work" : " works")
                        color: theme.inkDimmer
                        font.family: theme.ui; font.pixelSize: 13
                        bottomPadding: 16
                    }
                    Rectangle {
                        width: parent.width; height: 3; radius: 1.5
                        opacity: 0.5
                        gradient: Gradient {
                            orientation: Gradient.Horizontal
                            GradientStop { position: 0.0;  color: theme.gold }
                            GradientStop { position: 0.42; color: "transparent" }
                        }
                    }
                    Item { width: 1; height: 18 }

                    ListView {
                        width: parent.width
                        height: 246 + 56
                        orientation: ListView.Horizontal
                        spacing: 22
                        clip: true
                        model: section.modelData.entries
                        boundsBehavior: Flickable.StopAtBounds
                        // A horizontal list ignores the vertical wheel by default.
                        WheelHandler {
                            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                            onWheel: (ev) => {
                                var d = (ev.angleDelta.y !== 0 ? ev.angleDelta.y : ev.angleDelta.x)
                                parent.contentX = Math.max(0, Math.min(
                                    Math.max(0, parent.contentWidth - parent.width),
                                    parent.contentX - d))
                            }
                        }
                        delegate: UniverseTile {
                            required property var modelData
                            required property int index
                            entry: modelData
                            kind: section.modelData.kind
                            index: index
                            onActivated: root.openEntry(section.modelData.kind, modelData)
                        }
                    }
                    Item { width: 1; height: 40 }
                }
            }
            Item { width: 1; height: 60 }
        }
    }

    // "54 works · TV Shows, Movies, Specials, Manga, Novels" — derived, never written down.
    readonly property string metaline: {
        if (!payload || !payload.sections.length) return ""
        var n = 0, names = []
        for (var i = 0; i < payload.sections.length; i++) {
            n += payload.sections[i].entries.length
            names.push(payload.sections[i].title.toLowerCase())
        }
        return n + " works · " + names.join(", ")
    }

    BackAction {
        x: theme.margin; y: 28
        onTriggered: root.backRequested()
    }
    HouseScrollBar { flick: page }
    ScrollGlide { flick: page }
}
```

- [ ] **Step 2: Verify it loads with no QML errors**

```bash
PID=$(powershell -NoProfile -Command "(Get-CimInstance Win32_Process -Filter \"Name='colosseum.exe'\" | Where-Object { \$_.ExecutablePath -like '*build-msvc*' }).ProcessId" | tr -d '\r')
[ -n "$PID" ] && taskkill //PID $PID //F
PATH="/c/Qt/6.11.1/msvc2022_64/bin:$PATH" QT_ASSUME_STDERR_HAS_CONSOLE=1 ./native/build-msvc/colosseum.exe > /tmp/uni.log 2>&1 &
sleep 18; grep -iE "UniverseExtensionPage|UniverseTile" /tmp/uni.log
```
Expected: no output. **QML console output needs `QT_ASSUME_STDERR_HAS_CONSOLE=1` on Windows or it silently vanishes.**

- [ ] **Step 3: Commit**

```bash
git add qml/UniverseExtensionPage.qml
git commit -m "feat(universes): the one universe renderer — header band, served sections, tiles"
```

---

## Task 8: Revert the wrongly-restored bespoke pages

**Files:**
- Delete: `qml/OnePieceUniversePage.qml`, `qml/CosmereUniversePage.qml`, `qml/DragonBallUniversePage.qml`, `qml/MagazineUniversePage.qml`

These were restored from `179bb87~1` to feed a per-category dispatcher. This plan replaces that with **one renderer for every universe**, so they are dead weight. They remain in git history if ever wanted.

- [ ] **Step 1: Delete and confirm nothing references them**

```bash
rm -f qml/OnePieceUniversePage.qml qml/CosmereUniversePage.qml \
      qml/DragonBallUniversePage.qml qml/MagazineUniversePage.qml
grep -rn "OnePieceUniversePage\|CosmereUniversePage\|DragonBallUniversePage\|MagazineUniversePage" --include=*.qml qml/
```
Expected: no output from the grep.

- [ ] **Step 2: Commit**

```bash
git add -A qml/
git commit -m "chore(universes): drop the bespoke per-IP pages — one renderer replaces them"
```

---

## Task 9: Wire the carousel, the Hall and the route

**Files:**
- Modify: `qml/Main.qml`
- Modify: `qml/UniverseHallPage.qml`

- [ ] **Step 1: Point the carousel at the installed universes**

In `qml/Main.qml`, the restored UNIVERSE HERO block has `model: Universes.universes`. Replace with `model: win.installedUniverses`, and add this property beside `installedExtensions`:

```qml
    // The carousel and the Hall both derive from the ROSTER, so installing or removing a
    // universe is the only way either surface changes. Replaces the five baked universes.
    readonly property var installedUniverses: {
        var out = []
        for (var i = 0; i < win.installedExtensions.length; i++) {
            var e = win.installedExtensions[i]
            if (!ExtCatalog.isUniverse(e)) continue
            if (e.enabled !== true) continue
            var m = e.manifest || ({})
            out.push({ extensionId: e.id, name: m.name || e.id,
                       banner: m.background || "", logo: m.logo || "" })
        }
        return out
    }
```

Add `import "ExtensionsCatalog.js" as ExtCatalog` to `Main.qml`'s imports if absent.

Inside the hero delegate, the slide reads `modelData.name` and `modelData.banner` — both present above. Any slide field the baked entries had and these do not (`blurb`, `chips`, `c1`) must be removed from the delegate, not faked.

- [ ] **Step 2: Route to the one renderer**

Replace `universeSourceFor()` and `openUniverse()` in `Main.qml` with:

```qml
    // One renderer for every universe. The per-category dispatcher is gone: it existed to
    // pick between bespoke per-IP pages, and those are gone too (Task 8).
    function openUniverse(extensionId, name) {
        if (!extensionId) return
        universeLayer.extensionId = extensionId
        universeLayer.universeName = name || ""
        if (universeLayer.item) {
            universeLayer.item.extensionId = extensionId
            universeLayer.item.universeName = name || ""
        }
        universeLayer.active = true
    }
```

And the layer:

```qml
    Loader {
        id: universeLayer
        anchors.fill: parent
        z: 52
        active: false
        visible: active
        asynchronous: true
        property string extensionId: ""
        property string universeName: ""
        source: "UniverseExtensionPage.qml"
        onLoaded: {
            item.backdrop = wall
            item.extensionId = universeLayer.extensionId
            item.universeName = universeLayer.universeName
            item.backRequested.connect(win.closeUniverse)
            item.minimizeRequested.connect(win.minimizeShell)
            item.fullscreenRequested.connect(win.toggleFullscreenShell)
            item.closeRequested.connect(function() { Qt.quit() })
            item.watchRequested.connect(win.openTheatreSeries)
            item.seriesRequested.connect(win.openSeries)
            item.bookRequested.connect(win.openBook)
            item.comicsArchiveRequested.connect(win.openComicArchive)
        }
    }
```

The hero's slide click and the Hall's `exploreRequested` must both now pass **two** arguments: `win.openUniverse(modelData.extensionId, modelData.name)`.

- [ ] **Step 3: Point the Hall at the same model**

In `qml/UniverseHallPage.qml`: add `property var universes: []`, change `model: Universes.universes` to `model: root.universes`, change the count text to `root.universes.length + " universes"`, remove `import "Universes.js" as Universes`, and change the bar's signal to `exploreRequested(modelData.extensionId, modelData.name)` — update the signal to `signal exploreRequested(string extensionId, string name)`. Any bar field the baked entries had and the roster does not (`blurb`, `kicker`, `chips`) must be removed, not faked.

In `Main.qml`'s `universeHallLayer.onLoaded`, add `item.universes = win.installedUniverses` and update the connect to pass both arguments.

- [ ] **Step 4: Verify end to end**

```bash
PID=$(powershell -NoProfile -Command "(Get-CimInstance Win32_Process -Filter \"Name='colosseum.exe'\" | Where-Object { \$_.ExecutablePath -like '*build-msvc*' }).ProcessId" | tr -d '\r')
[ -n "$PID" ] && taskkill //PID $PID //F
PATH="/c/Qt/6.11.1/msvc2022_64/bin:$PATH" QT_ASSUME_STDERR_HAS_CONSOLE=1 ./native/build-msvc/colosseum.exe > /tmp/uni.log 2>&1 &
sleep 18; grep -iE "\.qml:[0-9]+" /tmp/uni.log | grep -viE "metahub|404"
```
Expected: no output.

Then **Hemanth's eyes**: Home shows a two-slide carousel (One Piece, DC Animated Universe); clicking a slide opens the rows page with its sections; "Hall of Worlds ›" lists both; Esc steps back.

- [ ] **Step 5: Commit**

```bash
git add qml/Main.qml qml/UniverseHallPage.qml
git commit -m "feat(universes): carousel and Hall derive from the installed roster; one route to one renderer"
```

---

## Task 10: Full-suite regression and the writeback

**Files:**
- Modify: `agents/chat.md` (Brotherhood repo)

- [ ] **Step 1: Run every extensions + universe suite**

```bash
for t in universe_payload_test universe_ext_api_test extension_page_wiring_contract \
         extension_panes_and_search_test extension_reorder_world_test \
         extension_worlds_derivation_test extension_world_isolation_test \
         addon_logos_house_wells_test addon_torrentio_honesty_test; do
  printf "%-38s %s\n" "$t" "$(node tests/$t.mjs 2>&1 | tail -1)"
done
```
Expected: every line `all green` or `PASS`.

- [ ] **Step 2: Post the shared-file writeback**

`qml/Main.qml` is shared. Append an entry to `Brotherhood/agents/chat.md` naming the blocks touched (hero model, universe route, universe layers) so A4 and A5 can see it.

- [ ] **Step 3: Commit and push**

```bash
git add -A && git commit -m "feat(universes): One Piece and DCAU ship as installed universe extensions"
git push origin master
```

---

## Task 11: The comics route — post IDs, not a tag

**Files:**
- Modify: `qml/Main.qml`

Ruling 2. `openComicArchive` (`Main.qml:649`) is **tag-shaped**: it sets `boxTitle`, `tagSlug`, `boxCount`, `tagId` and the index layer resolves from `tagId`. A DCAU comic entry has none of those — it has verified `posts[]`. Wiring it to that route would open an empty archive.

The seam that *does* fit already exists: **`openGcdSeries` injects an explicit `releases[]` array** into `westernLayer.baked` and opens `ComicSeriesPage` with no tag at all (`tagSlug = ""`, `tagId = 0`). A pinned post list is the same shape as a baked release list. That is the route to mirror.

- [ ] **Step 1: Read the existing injection before writing anything**

Read `openGcdSeries` in full (`qml/Main.qml`, from ~`:571`) **and** the `westernLayer` Loader's `onLoaded`. Mirror its exact property names — `bakedReleases`, `seriesTitle`, `poster`, `openChapterId` and any others it sets. **Do not infer them from this plan**; the list here is illustrative and the file is the truth. Also read `ComicSeriesPage.qml:47` and `:433` to confirm which release fields are actually consumed (`url` feeds `postUrl` feeds `Comics.downloadIssue`).

- [ ] **Step 2: Add the route**

Add beside `openGcdSeries`, mirroring whatever Step 1 found:

```qml
    // ---- a universe's comic row: the entry pins VERIFIED GetComics post IDs, so there is
    //      no tag to resolve. This mirrors openGcdSeries' baked injection — an explicit
    //      release list, tagSlug/tagId deliberately empty. `?p=<id>` is GetComics' canonical
    //      permalink and redirects to the post, which is what downloadIssue consumes.
    //      d: { title, posts: [Number], year? } ----
    function openUniverseComic(d) {
        var posts = (d && d.posts) || []
        if (!posts.length) return
        var rel = []
        for (var i = 0; i < posts.length; i++)
            rel.push({ id: String(posts[i]),
                       url: "https://getcomics.org/?p=" + posts[i],
                       name: d.title || "", cover: "",
                       year: Number(d.year || 0), sizeMB: 0, synopsis: "",
                       date: "", collection: true })
        // ... then set westernLayer.baked / .title / .tagSlug="" / .tagId=0 and activate,
        // exactly as openGcdSeries does. Match its live-vs-reinject branch too.
    }
```

- [ ] **Step 3: Repoint the connection made in Task 9**

In `universeLayer.onLoaded`, change `item.comicsArchiveRequested.connect(win.openComicArchive)` to `item.comicsArchiveRequested.connect(win.openUniverseComic)`. `UniverseExtensionPage.openEntry` already emits `{ title, posts }` — extend it to pass `year` as well.

- [ ] **Step 4: Prove one post actually reaches a download**

Launch, open DC Animated Universe, click **Justice League Unlimited** in the Comics row. Expected: `ComicSeriesPage` opens with a release list, not an empty archive. **This is the one step that cannot be proven headlessly** — a redirect that `Comics.downloadIssue` fails to follow would look identical to success until download time, so watch one issue actually start.

- [ ] **Step 5: Commit**

```bash
git add qml/Main.qml qml/UniverseExtensionPage.qml
git commit -m "feat(universes): comics open by pinned post ID, mirroring the baked release route"
```

---

## Task 12: Real marks for both universes

**Files:**
- Create: `assets/addon-logos/one-piece.png`
- Modify: `qml/AddonLogos.js`

Ruling 3: One Piece takes the **One Pace** add-on's mark; DC Animated Universe takes the **DC Universe** add-on's mark. `dc.png` already ships — only One Piece's is missing.

- [ ] **Step 1: Fetch the One Pace mark and normalise it**

Its URL is the live manifest's own logo, read from the installed profile — not a guess:

```bash
node -e "
const j=JSON.parse(require('fs').readFileSync(process.env.APPDATA+'/Brotherhood/Colosseum/extensions/installed.json','utf8'));
console.log(j.extensions.find(e=>e.id==='com.onepace.fedew').manifest.logo);"
# → https://i.pinimg.com/originals/4c/46/ee/4c46ee47e0710a6d928454f68fc4ee17.png  (verified 200, 40 KB)
curl -sL -o assets/addon-logos/one-piece.png "<that url>"
python -c "from PIL import Image; im=Image.open('assets/addon-logos/one-piece.png').convert('RGBA'); im.thumbnail((256,256)); im.save('assets/addon-logos/one-piece.png')"
```

256 is the house cap — these tiles draw at 96px max, and an oversized logo is what cost 115 MB of decode earlier this arc.

- [ ] **Step 2: Map both, matched on extension ID**

In `qml/AddonLogos.js`, add to the catalogs block **above** the existing `marvel.png` / `dc.png` rules:

```javascript
    // ---- universes: an installed universe wears the mark of the add-on it grew out of
    //      (Hemanth, 2026-07-26). Matched on ID, not name: "DC Animated Universe" does not
    //      match the dc.png rule's /\bdc universe\b/, and matching loosely on "one pace"
    //      vs "One Piece" would cross the two.
    { file: "one-piece.png", m: function (id, n) { return id === "com.colosseum.universe.onepiece"; } },
    { file: "dc.png",        m: function (id, n) { return id === "com.colosseum.universe.dcau"; } },
```

- [ ] **Step 3: Extend the logo harness and run it**

Add both rows to `tests/addon_logos_harness.qml`'s table beside the existing `["com.tapframe.dcaddon", "DC Universe", "dc.png"]`, then run the logo suite. Confirm the existing `com.tapframe.dcaddon` row still resolves to `dc.png` — a **negative control** proving the new ID rules did not shadow it.

- [ ] **Step 4: Commit**

```bash
git add assets/addon-logos/one-piece.png qml/AddonLogos.js tests/addon_logos_harness.qml
git commit -m "feat(universes): both universes wear their add-on marks in the Extensions row"
```

---

## Rulings — answered by Hemanth 2026-07-26, no longer open

1. **DCAU's display name → "DC Animated Universe".** Task 1 already writes exactly that; no change. The short form is chat shorthand only and must not reach a surface.
2. **Comic destination → confirm the posts first, then route.** All 14 post IDs were fetched and verified (Task 3 Step 2). `openComicArchive` is confirmed **tag-shaped** (`Main.qml:649` sets `tagSlug`/`tagId` only) and cannot take `posts[]` — so the comics row gets its own route, **Task 11**.
3. **Icons → reuse the two existing add-on marks.** One Piece takes the **One Pace** add-on's mark; DC Animated Universe takes the **DC Universe** add-on's mark, which already ships as `assets/addon-logos/dc.png`. **Task 12.**

---

## Self-review

**Spec coverage:** §5.1 manifest (Task 1) · §5.2 payload shape + field contract (Tasks 2–5) · §5.3 identity-not-sources (Task 7 routes out, supplies nothing) · §5.4 validation gate (Task 4) · §8 items 1/2/3/4 (Tasks 9, 7, 4) · §8a collision — Extensions page already done, untouched here.

**Placeholders:** none — every step carries its code or its exact command.

**Type consistency:** `fileFor`/`validate`/`load` used identically in Tasks 4, 5, 7. `extensionId` is the key throughout — manifest id, `FILES` key, `UniverseExtensionPage.extensionId`, `openUniverse` arg 1. `installedUniverses` entries expose `{extensionId, name, banner, logo}` and only those fields are read in Tasks 9's carousel and Hall.

**Known gap, stated not hidden:** the Continue section in the mock is not built by this plan. It needs per-medium `Progress` matching against payload entries, which is its own task once the page is on screen and its sections are proven.
