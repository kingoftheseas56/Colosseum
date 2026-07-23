# Cross-model review handoff — Theatre Discover Stage 1 (requested by Agent 0 for A4)

**Producer:** Agent 0 (Claude/Opus). **Reviewer must be a DIFFERENT model** — default Codex
(`codex exec`), or Agent 9 (DeepSeek) if Codex quota is low. Review gate before this ships to
Hemanth's eyes-on.

**Diff under review:** Colosseum `2be911e..4c41c17` (9 commits, 13 files, +989/−10). Expand with
`cd ~/Desktop/Brotherhood/Colosseum && git diff 2be911e..4c41c17`.

**Spec (intent):** `docs/superpowers/specs/2026-07-23-colosseum-theatre-discover-library-parity-design.md`
(Brotherhood repo) §3 + §8.1.

---

Paste the block below into a Codex tab (`codex exec "<block>" </dev/null`, stdin closed):

```
Cross-model review for Colosseum Theatre Discover Stage 1 (requested by Agent 0, on A4's behalf).
You are a DIFFERENT model than the author — check this diff against the written Definition of Done,
not just read the code. Run `cd ~/Desktop/Brotherhood/Colosseum && git diff 2be911e..4c41c17` to see
every change.

DEFINITION OF DONE (spec §3 Stage-1 + §8.1 acceptance — verify the diff against EACH item):
1. Theatre tab bar becomes Discover · Movies · Shows · Anime, Discover FIRST and default-selected.
2. Home spine above the tabs is UNTOUCHED (featured carousel → Next Up → Continue → Your Collection → tab bar).
3. Type picker = union of types offered by installed addons' catalogs, title-cased; selecting re-derives the catalog picker.
4. Catalog picker = every installed+enabled addon's catalogs for the selected type, in installed order, Cinemeta first; shows catalog name + dimmed owning-addon; PURE addon catalogs only (no house Top-10/baked-genre duplication).
5. Extra-filter pickers = one per catalog `extra` (typically Genre); optional extras get an All/None entry; isRequired extras auto-select first option; search-required catalogs stay excluded.
6. Picker change reloads the wall, resets to page one, preview shows first item.
7. Wall = 2:3 poster grid, house card grammar (gold hover/selection edge, title under), skeletons while loading.
8. Paging uses the addon `skip` param; next page fetches as scroll nears bottom; single response capped defensively at 100.
9. Selection model: first click selects → preview fills; second click (or Show) opens the existing detail-page door.
10. Preview pane (right): art, title, meta line (runtime · year · ★rating when present), synopsis; actions = +Library (Collection, world "theatre") and ▶ Show. NO watched-eye, NO trailer button (both deferred — must not render a dead button).
11. States: uninstalled catalog's addon → inline "needs the <name> addon" bar + Install; empty catalog → honest empty state; never a silent blank.
12. Extension catalog rows on Movies/Shows/Anime tabs gain a "See all ›" door → opens Discover pre-pinned to that exact catalog (type+catalog+addon). House/genre rows keep their existing doors, unchanged.
13. Protocol (invisible): AddonClient learns extra-URL building (/catalog/{type}/{id}/{extraProps}.json) + skip paging + genre option lists from manifest. NO raw XHR added to GUI-thread QML — the .js API modules own fetch (house doctrine: QML paints, C++/JS-modules decide).
14. §8.1 end-to-end: install an addon with a catalog → it appears in the picker → browse with its genre filter → scroll pages deep → select → +Library → Play; remove the addon → the catalog offers Install.

CONTEXT THE AUTHOR FLAGS (verify these are sound, not rubber-stamp):
- The seeded Cinemeta manifest ships WITHOUT catalogs[] (ExtensionsStore::seed embeds a slim manifest;
  confirmed against the live persisted installed.json — Cinemeta core, 0 catalogs). Discover derives its
  picker purely from manifest catalogs, so the author added a QML-only fallback in DiscoverApi.js
  (_effectiveInstalled) that synthesizes Cinemeta's two live "Popular" catalogs (movie 19 genres / series
  22 genres, copied verbatim from v3-cinemeta.strem.io/manifest.json) onto ONLY a catalog-less CORE
  Cinemeta row — leaving every other addon untouched. Verify: (a) it fires only for that row, (b) it never
  mutates the caller's objects, (c) the genre lists/ids match the live manifest, (d) it doesn't duplicate
  or shadow real catalogs when Cinemeta is later re-installed with a full manifest.
- Author fixed two plan-snippet defects to make the QML valid: fractional font.pixelSize → int (Qt requires
  int), and added `import QtQuick.Controls` to DiscoverPage (needed for the ScrollBar.vertical attached
  property). Confirm no behavioral drift from these.
- discoverPin threads spec→row via a CONDITIONAL key in TheatreApi.row() so house rows keep discoverPin
  undefined and the delegate's `discoverPin !== undefined` gate reads false. Verify house rows do NOT get
  spurious See-all chevrons and extension rows DO.

YOUR REVIEW — do all four:
1. For EACH Definition-of-Done item (1-14): state MET / NOT-MET / PARTIAL with one line of evidence from the diff.
2. Flag anything the diff DOES that the DoD never asked for (scope creep / unrequested behavior change).
3. Correctness + security pass: real bugs, stale-fetch races, regressions to the existing Movies/Shows/Anime
   tabs (row() signature change, catalogSpecs field additions), URL-encoding of extra values/ids, leaked
   secrets, unsafe network handling.
4. Anything the DoD SHOULD have specified but didn't (gap in intent itself).

END with exactly one line: APPROVE or REQUEST-CHANGES, plus a one-sentence reason. Be terse; default to
REQUEST-CHANGES if any DoD item is NOT-MET or you are unsure.
```

**After the review returns:** Agent 0 (or the next Theatre wake) addresses every NOT-MET / REQUEST-CHANGES
item before this is presented as accepted. Eyes-on (Hemanth's screen) remains the final product acceptor.
