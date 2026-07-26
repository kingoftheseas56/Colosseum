# Handover — Extensions page overhaul (Agent 0 → Codex)

**From:** Agent 0 (Claude), theatre · **Date:** 2026-07-26
**Base:** Colosseum `master` @ `c42d9fe` (my arc merged and pushed — build from master, not from a branch)
**Task Hemanth is giving you:** overhaul `qml/ExtensionsPage.qml`, with **Harbor's add-on store as the reference**.

Read this before you touch the file. It is not a spec — it is what I learned the hard way,
what he has already ruled on, and where the mines are.

---

## 1. The reference is Harbor, and it is on this machine

`C:\Users\Suprabha\Desktop\harbor-main\harbor-main` — a React/TS app. **Read it, do not
paraphrase a screenshot.** Relevant files:

- `src/components/community-addons-rail.tsx`, `src/components/trending-addons-rail.tsx` — card anatomy
- `src/components/addon-star-badge.tsx` — the rating badge
- `src/components/addons-mosaic-backdrop.tsx` — the artwork collage behind the hero
- `src/components/addon-logo.tsx` — our `qml/AddonLogos.js` was ported from its `BUNDLED` array
- `src/lib/addons-store/curated.ts` — our `RAILS` in `qml/ExtensionsCatalog.js` was ported from this

**What Hemanth showed me of Harbor**, so you know what caught his eye: a featured hero with
the name large, **one** short tagline, an `Installed` pill and a `View details ›` link, and an
artwork collage behind it. Then a section with an eyebrow (`COMMUNITY RATINGS`), a heading
(`Top on stremio-addons.net`), a segmented control (`Top rated` / `Just added`) and `Browse all`.
Then cards that lead with **artwork**, with a star badge, the logo overlaid, the name, and a
two-line description.

**The nuance that matters:** Harbor's cards DO carry descriptions. They do not read as clutter
because artwork carries the visual weight and the text is secondary. Ours had no artwork, so the
text WAS the card, and it read as a wall. He asked me to strip our descriptions before he showed
me Harbor — **treat "give the cards artwork and hierarchy" as at least as likely to be the real
ask as "keep them textless"**. Ask him; do not assume my strip was the final word.

---

## 2. His rulings — these are settled, do not re-litigate

| Ruling | Date | Where it lives now |
|---|---|---|
| **Discover and Browse are Theatre-only.** The community registry is entirely video add-ons, so a world-filtered Browse would be permanently empty in Tankoban and Biblio. Those worlds get **Installed only**. | 07-26 | `root.paneModel`, `hasStore` |
| **No descriptions on any row, any pane.** He called them clutter and several were factually wrong. | 07-26 | all delegates |
| **No group subtitles** under CATALOGUE / WELLS either. | 07-26 | group header |
| **Our catalogues are the Colosseum Grand Database and AniList.** WeebCentral and GetComics are wells — they say what is *downloadable*, which is a well's job. | 07-26 | `ExtensionsStore.cpp` house roster |
| **"Colosseum Grand Database"** is his name for the private data vault row. | 07-26 | roster + `AddonLogos.js` |
| **Keyboard and remote support is dropped.** *"forget about keyboard, that's the least of our problems."* | 07-26 | audit P0-9/13/15 — do not build |
| **A search goes where the answers are** — typing in Discover moves to Browse; clearing gives Discover back; a user who chose Browse himself is left there. | 07-26 | `onQueryChanged` |

---

## 3. What is already fixed — do not undo these

All are covered by tests; if you rewrite the file, **keep the behaviour or the suites fail**.

1. **Reorder is world-relative** (`Catalog.moveDestination` → `Extensions.moveTo`). A global ±1
   swap was doing nothing visible in Tankoban 4 presses out of 8, and 3 of those silently
   reordered **Biblio**. The resolver also picks *which* of the two rows to physically move, so a
   shared well can be dragged across one world without disturbing the other.
   **`Extensions.move(id, ±steps)` no longer exists.** Use `moveTo(id, absoluteIndex)`.
2. **Per-world rank is derived, never stored** — one row reads 4 in Tankoban and 2 in Biblio.
3. **The Installed tab counts its own world** (`countIn(world)`), not the whole app.
4. **Browse cannot hang** — the discarded-answer branch releases `communityLoading`, and the
   transport has a 12s timeout plus `ontimeout`/`onerror`.
5. **The featured slab derives its state** from `carried()` / `coreOf()` rather than hardcoding
   "Installed" and "built-in".
6. **House manifests refresh on migration.** A house row's name/metadata are ours; `enabled` and
   position are his and live outside the manifest. Bump `kHouseDefaultsVersion` on any copy change.
7. **Logo assets are capped at 256px** — whole-directory decode went 115 MB → 9 MB. Do not add a
   4K logo; `tests/addon_logos_house_wells_test.mjs` checks every matcher resolves to a real file.

---

## 4. Traps that cost me hours

- **QML `console.log` does not reach stdout on Windows.** Launch with
  `QT_ASSUME_STDERR_HAS_CONSOLE=1` or your probes silently produce nothing and you will think
  the code never ran.
- **The first click on an unfocused window only focuses it.** I twice concluded a control was
  broken when the click was simply eaten by focus. Click a neutral part of the app first, then
  the target. This wasted real time — do not repeat it.
- **Unit-testing the JS in isolation is not enough.** I changed `moveDestination`'s return type,
  updated its test, shipped — and left all three QML call sites testing `>= 0` against an object.
  Every suite was green and the arrows were dead. `tests/extension_page_wiring_contract.mjs`
  exists to catch exactly that; **extend it, do not delete it.**
- **Do not read a derived binding inside its own dependency's change handler.**
  `onWorldChanged` read `hasStore` (derived from `world`) and got the stale value, so switching
  world from Discover did nothing. Compare `world` directly, and gate the panes declaratively.
- **Build:** absolute path to `native/build-msvc.bat`, then grep the log for
  `error C|LNK[0-9]{4}|ninja: build stopped` — **the exit code lies**. Kill the running exe
  **by PID**, not `taskkill /IM colosseum.exe`: that also kills Agent 4's worktree build.
- **`installed.json` is at `AppData/Roaming/Brotherhood/Colosseum/extensions/`** (Roaming, not Local).
- **Removals are permanent by design** — the migration will not resurrect a row the user removed.
  If you remove one of his add-ons while testing, restore it by hand. I had to do that twice.

---

## 5. Open, and honestly not done

- **The featured slab's install verb has never been seen working.** The first version's click
  never reached its MouseArea (proven by a probe that never fired); it now has an explicit 44px
  hit box and the contract test pins the wiring, but **I did not verify the click on screen** —
  doing so means removing Torrentio from his live profile a third time. Verify it or replace the
  whole hero with Harbor's `Installed` pill + `View details ›`.
- **Reorder and on/off are not wired to any real fetch in Tankoban or Biblio.** The rows are an
  accurate *picture* of the system, not yet a control panel. That was stage 3 of the
  extension-worlds spec and it touches `qml/Main.qml` — **declare that on `Brotherhood/agents/chat.md`
  before you touch it**, Agent 4's player work lives in that file.
- **A5's audit still has ~50 open findings** — `Brotherhood/agents/audit-extensions-store-ux-2026-07-25.md`.
  I verified its 9 cheapest P0s adversarially; **all 9 held**. Seven places where his prose is off
  are logged in `Brotherhood/agents/chat.md` (2026-07-26 entry). One finding, P3 "a raw URL as
  body copy", is now dead — I deleted that Text.
- **The universes page is designed and stacked but NOT built.** Spec:
  `docs/superpowers/specs/2026-07-25-colosseum-universes-as-extensions-design.md` (374 lines,
  locked). Plans: `...plans/2026-07-25-universe-one-piece-addon-plan.md` (279 lines, 53 verified
  pins) and `...-universe-dcau-addon-plan.md` (210 lines, 31 pins). His standing rule was
  **stack, do not implement** — confirm with him before building any of it.
  Note `worldTitles` already knows a `universes` world and `worldsFor()` already returns it, but
  no tab exists (audit P0-8).

---

## 6. The one thing I would tell you if I could only say one

Twice today I wrote something plausible instead of something checked — false add-on descriptions,
and a comment claiming other worlds were safe when a test proved they were not. He caught the
first; my own test caught the second. **On this page, assert nothing you have not read in the
code or seen on screen.** The whole arc above is the cost of that lesson.
