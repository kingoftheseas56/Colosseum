# Biblio per-card cover diagnostics — the walk that closes the cover-fix arc

**Plan author:** Agent 0 (Claude), governance — 2026-08-06, under `brotherhood-writing-plans`.
**Spec:** the Hemanth-ratified decision brief (`Brotherhood/agents/colosseum-lanista-test-session-biblio-image-decision-brief.md`, §4, §10) plus his direction tonight: *explain the live blank specimen ("Theo of Golden") and close the cover-fix verification.*
**Executor contract:** run under `brotherhood-executing-plans`. Capability authority is
`docs/colosseum-lanista-verification.md` **as of Colosseum `d4c6d9a` + the ledger truth-up
committed with this plan** — read it fresh; do not trust this plan's paraphrases over it.
**Baseline commit:** `d4c6d9a` (pilot green 13/13; `lanista session run`, `BiblioImageDiag`,
named surfaces all landed there).

## Where this picks up

Tonight's pilot proved: isolated disposable sessions, no-sleep navigation to the Biblio
Discover wall, per-URL network recording, and pixels showing the `5b28b5a` fix visibly
healing covers ("The Deal", "The Mistake" render real art). It also produced one live blank
card — **"Theo of Golden"** — and one pre-loaded hypothesis the executor must test, not
assume:

> **H1:** "Theo of Golden" is a BUNDLED card (`qml/Catalog.js:147`) whose cover URL ends
> `/400x600bb.jpg` on Apple's mzstatic CDN. Bundled `Catalog.js` covers go straight from QML
> to the network and **never pass through `normalizedAppleArtworkUrl()`** — the C++ fix
> heals provider/store rows only. If that URL family is dead or failing on the CDN, every
> bundled Apple-shaped cover is un-healed by design.

Also inherited, to record not chase: in pilot run 3 the wall header still read "Biblio
built-in catalogue" while `freshness == "fresh"` — the walk records each card's page-level
context so this attribution/freshness mismatch gets evidence too.

## Slices

### Slice 1: `lanista session run --walk` — the per-card evidence walker
Purpose: nothing changes on screen; this gives every later slice the machine that turns "a
wall of cards" into one evidence row per card, so a blank cover stops being a mystery.
Dependencies: none (baseline `d4c6d9a`).
Implementation guidance: extend the `session` verb in `native/tools/lanista.cpp` with a
`--walk` stage that runs after the scenario passes, while the session app is still alive.
Loop: `dump-ui` → collect `objectName =~ ^discoverCard_(?!.*_art)` names → for each new
name: `qml-get` on `<name>_art_img` (`source`, `status`, `sourceSize`, `paintedWidth`,
`paintedHeight`) and on `<name>_art` (`activeSource`, `exhausted`, `candidateIndex`,
`sources`, `ready`), then `invoke-read BiblioImageDiag.rowsForUrl(<source>)` (use the
longest URL path segment as the fragment; the arg is a QString). Join into one JSON line
per card: `{ cardObjectName, source, imageStatus, sourceSize, painted, activeSource,
exhausted, candidateIndex, candidates, netRows[], verdict }`, newest run wins, written to
`artifacts/lanista-sessions/<id>/cards.jsonl`. Then `ui-scroll` the wall (target
`biblioDiscoverPage`, default dy) and repeat until a full pass adds no new names or a
`--walk-max` cap (default 60) hits; log a dropped-count line if capped — no silent caps.
Verdict classification (plain rules, in the walker): `healed` (source ends
`/600x600bb.jpg` ∧ status Ready ∧ a 200 row), `already-good` (Ready, non-Apple or non-bb
source), `still-broken` (status Error ∨ exhausted ∨ newest row non-200), `undecoded`
(Ready row but sourceSize 0), `unfetched` (no rows, status Loading/Null).
Behavior to preserve: every existing verb byte-identical; the exit-code contract (0/1/2/4/5)
untouched — a walk failure is infra 4 only if the bridge died, otherwise the walk always
completes and verdicts speak.
Baseline: `git show d4c6d9a:native/tools/lanista.cpp` — no walk; tonight's sessions have no
`cards.jsonl`.
Focused tests: `tests/test_lanista.ps1` gate stays green (grep-shape + harness + self
scenarios); add a grep-shape assertion there that `session` help mentions `--walk` and the
walker classifies all five verdicts (presence contract, matching that gate's house style).
Lanista actions: n/a at plan level — this slice BUILDS the actions.
Completion signal: n/a (internal; the tool exits).
State / events / probes: n/a.
Visual evidence: n/a.
Regression paths: run `lanista session run tests/lanista_scenarios/biblio_covers_pilot.json
--drive --tag walkdev --keep-going` WITHOUT `--walk`: behavior and artifacts identical to
tonight's pilot (13/13, no cards.jsonl).
Evidence artifacts: build log grep-clean; gate output.
Bridge status: not applicable (this IS the bridge prerequisite the ledger's "per-card
WALK/JOIN" Unavailable entry names).
Completion criterion: done when the gate is green, the no-walk regression run matches
tonight's pilot, and a `--walk` run against a live session writes a `cards.jsonl` whose
every line carries all schema keys. Status vocabulary: Test-reported is sufficient here —
slice 2 is where the walker meets runtime truth.

### Slice 2: The whole wall walked, cold and warm — per-card truth for every visible book
Purpose: Hemanth gets, for every card on the Discover wall, a one-line answer to "did this
cover load, from where, at what size, cache or network" — cold boot and warm boot.
Dependencies: Slice 1.
Implementation guidance: no app code. Add scenario
`tests/lanista_scenarios/biblio_covers_walk.json` = tonight's `biblio_covers_pilot.json`
plus, after the freshness gate: `ui-scroll` down + `ui-wait-for` back on
`biblioDiscoverPage.visible == true` after a `ui-click` on `modePill_Theatre` and again
`modePill_Biblio` (the navigate-away/back regression), ending with window + item grabs.
Run twice with the same fresh tag: run A (cold — empty tagged cache), run B (warm).
Lanista actions: `lanista session run tests/lanista_scenarios/biblio_covers_walk.json
--drive --walk --tag <fresh tag>` twice; all steps from the ledger's AVAILABLE section.
Completion signal: scenario's own gates (`bootSplash.visible == false`,
`biblioDiscoverPage.loading == false`, `freshness == "fresh"`); the walk's termination is
the walker's no-new-names pass (Slice 1).
State / events / probes: cards.jsonl from both runs. Expected: run A — ≥ 10 cards, zero
`unfetched`, every Apple-sourced card `healed` with `cacheHit false` on its newest row;
run B — same card set, Apple cards' newest rows `cacheHit true`; `still-broken` set
identical between runs (a cache must not change verdicts). `log-mark` before each phase
(`walk-cold`, `walk-warm`) for stderr correlation.
Visual evidence: the item grab of `biblioDiscoverPage` from each run — run A and run B
walls must show the same covers; blanks (if any) must correspond one-to-one with
non-`healed`/non-`already-good` rows in cards.jsonl. Pixels corroborate the rows, never
replace them.
Regression paths: scroll away/back (in-scenario), navigate Theatre→Biblio and back
(in-scenario), cold→warm restart (the two runs). Daily app untouched throughout — sessions
only, tagged, unique pipe (controller-enforced).
Evidence artifacts: two `artifacts/lanista-sessions/<id>/` dirs (manifest, cards.jsonl,
grabs, stdout/stderr).
Bridge status: available (given Slice 1; every named command is in the ledger).
Completion criterion: Runtime-validated when both runs are scenario-green, both
cards.jsonl meet the expectations above with zero `unfetched`, the cold/warm `cacheHit`
flip holds for Apple cards, and every visual blank has a matching non-green row. Any
mismatch between pixels and rows is Verification failed — report it, do not reclassify.

### Slice 3: "Theo of Golden" explained — and the cover-fix arc closed by Hemanth's eyes
Purpose: the one blank card Hemanth has already seen gets a written, evidence-backed
explanation — and if the evidence names a safe fix, the blank becomes a real cover.
Dependencies: Slice 2 (its cards.jsonl is the diagnosis).
Implementation guidance: first diagnose from Slice 2's artifacts: Theo's card row —
candidates list, activeSource, newest net rows (expect H1: a `400x600bb.jpg` mzstatic URL
with a non-200 or a decode-side failure; `exhausted` tells whether fallback ran out).
Write the verdict into the plan's Execution notes with the row quoted. THEN, only if the
evidence confirms a dead URL-shape on bundled cards: apply the smallest fix at the OWNING
seam — the bundled data itself (`qml/Catalog.js` cover URLs to the verified
`600x600bb.jpg` shape) or, if multiple bundled cards share the disease, the JS candidate
seam (`PosterSourcePolicy.js`) so bundled Apple thumbs are normalized exactly like C++
rows — never a media-specific branch inside the shared card/image components (standing
stop condition). If the evidence shows something else (DNS, decode, non-Apple), STOP:
report Plan contradicted with the row, and scope the fix with Hemanth — do not improvise.
Behavior to preserve: non-Apple bundled covers untouched; the C++ normalizer untouched;
the shared `RoundedPosterImage` fallback walk untouched.
Baseline: tonight's artifacts — `artifacts/lanista-sessions/20260806-143454-1db8688b/`
(built-in wall grab, Theo dark) and `20260806-143712-0af3f1bc/` (live wall grab, Theo
dark) — plus Slice 2's pre-fix cards.jsonl row for Theo.
Focused tests: if `Catalog.js`/`PosterSourcePolicy.js` change: extend the existing biblio
QML harness gate (`tests/test_biblio_discover_explore.ps1` house pattern) with a
shape-assertion that no bundled cover URL matches the known-dead pattern; keep every
existing gate green.
Lanista actions: after the fix, re-run Slice 2's cold run verbatim (fresh tag).
Completion signal: same scenario gates as Slice 2.
State / events / probes: Theo's cards.jsonl row flips to `healed` (or `already-good` if
the chosen fix moves it off Apple), with a 200 row and Ready status; the full-wall
expectations of Slice 2 still hold (the fix must not regress another card's verdict).
Visual evidence: item grab of the wall — Theo renders real art; plus `lanista brief
biblio-covers` gallery assembled from the final run for the eyes-on record.
Regression paths: Slice 2's full scenario re-run IS the regression pass (scroll,
navigate-away/back, cold fetch).
Evidence artifacts: pre/post cards.jsonl rows quoted in Execution notes; final session
dir; `agents/eyes-on/<date>-biblio-covers/gallery.md`.
Bridge status: available (given Slice 1).
Completion criterion: this slice — and with it the cover-fix arc — is Runtime-validated
only when (a) Theo's verdict flip is proven by rows AND pixels in a green re-run, (b) no
other card's verdict regressed, and (c) **Hemanth has looked at the gallery and said the
shelf looks right — his eyes are the closing gate, the harness only feeds them.** If the
diagnosis contradicted H1 and no fix was ratified, the honest closing status is
Verification failed or Plan contradicted with the evidence attached — never a quiet
"done".

## Plan self-review (performed)

All three slices carry the full field contract; every Lanista action in slices 2–3 exists
in the ledger's AVAILABLE section given Slice 1, which is the explicit bridge prerequisite
for the ledger's "per-card WALK/JOIN" Unavailable entry; baselines are named artifacts on
disk; no slice's verification is "manually"/"ensure"/screenshot-only (pixels corroborate
probes everywhere); order is prerequisite-first; the daily app is never touched; the one
aesthetic outcome (the shelf looking right) names Hemanth's eyes as the gate. Known open
risk, stated: the attribution/freshness mismatch is recorded as evidence-to-collect, not
assumed away; and `ui-scroll`'s effect on GridView materialization is the walker's main
empirical risk — if scrolling fails to materialize new delegates, the walker reports its
dropped-count honestly and the executor stops rather than sleeps.
