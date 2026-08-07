# Theatre — House HTTP Source: Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: use `brotherhood-executing-plans` to execute this
> plan slice by slice. Report each slice in the exact status vocabulary.

- **Date:** 2026-08-07 · **Arc:** THEATRE_HTTP_SOURCE
- **Spec:** `docs/superpowers/specs/2026-08-07-theatre-house-http-source-design.md` (approved by Hemanth 2026-08-07)
- **Lane:** Agent 4 (Player/Theatre). Shared-file edits declared on `Brotherhood/agents/chat.md` before landing.
- **Repo:** Colosseum, branch master (Rule 28).
- **Ledgers read fresh 2026-08-07:** `docs/colosseum-test-verification.md`, `docs/colosseum-lanista-verification.md`.

## Ground truth already verified — do not re-derive

- `AddonClient.parseStream()` (`qml/AddonClient.js:174-203`) already produces a `Direct · HTTP
  stream` row for any stream carrying `url`; `infoHash` becomes `"url:" + url`.
- The play chain: `PlayerPage.qml:1166` strips the `url:` prefix (`substring(4)`) and line 1171
  calls `mpv.loadFile(directUrl)`. **There are nine `mpv.loadFile` call sites in PlayerPage.qml**
  (1171, 1385, 1413, 2012, 2114, 2173, 3153 and two more) — header state must not leak between them.
- `MpvItem::loadFile` issues `loadfile <url> replace` and nothing else (`mpvitem.cpp`).
  `MpvItem::setProperty(name, value)` exists and is used for the constructor option block
  (`mpvitem.cpp:46-60`, incl. `user-agent` already forced to a VLC UA).
- `behaviorHints.proxyHeaders` is **discarded** — `parseStream` reads only `bingeGroup` and
  `filename` from behaviorHints (`AddonClient.js:202-203`).
- `bingeGroup` is parsed (`AddonClient.js:202`, `Torrentio.js:142`) and **consumed nowhere**.
- **`qml/SourcesSheet.qml` contains ZERO `objectName` declarations.** `dump-ui` only sees named
  items, so today the bridge cannot address a single source row.
- `ExtensionsStore::installBundled()` exists for app-owned rows (built for `net.vidking.player`).
- Extensions browse is already wired to `stremio-addons.net` (`ExtensionsCatalog.js:8`).
- Curated list already carries `com.notorrent.addon` (verified live, v2.7.0) and `webstreamr-mbg`.
- Qt WebEngine is required by `qml/reader2/*` — **not removable**.
- Standard deterministic gate: `ctest --test-dir native/build-msvc -L unit --output-on-failure`.
- Lanista test sessions: `lanista session run <scenario.json> --tag <t> [--drive]` — disposable
  tagged app, unique pipe, isolation asserted from the app's own `get-state`.
- `ui-wait-for` polls **one property for strict equality only** — no operators, no compound
  predicates. Any completion signal must be expressible as `property == value`.

---

## Slice 1: Streams that need a `Referer` actually play

**Purpose:** the two HTTP source addons Colosseum already ships stop failing silently — a link that
requires a header now plays instead of erroring.

**Dependencies:** none. **This slice gates the entire arc.**

**Implementation guidance:**
- `native/player/mpvitem.{h,cpp}`: add `Q_INVOKABLE void loadFileWithHeaders(const QString& url,
  const QVariantMap& headers)`. It formats the map into mpv's `http-header-fields` list
  (`"Key: value"` entries), sets it via the existing `setProperty`, then performs the same
  `loadfile` command `loadFile` performs. **`loadFile` itself must clear `http-header-fields`**
  so a header-carrying source cannot leak into the next load — that is the whole reason the
  clearing lives in `loadFile` rather than in the caller.
- Extract the map→`http-header-fields` formatting into a free function in a small header so it is
  unit-testable without an mpv instance.
- `qml/AddonClient.js parseStream()`: add `headers: (s.behaviorHints && s.behaviorHints.proxyHeaders) || ({})`.
- `qml/PlayerPage.qml:1166-1171`: when the row carries a non-empty `headers`, call
  `mpv.loadFileWithHeaders(directUrl, headers)`; otherwise the existing `mpv.loadFile`.

**Behavior to preserve:** all nine existing `mpv.loadFile` call sites keep working unchanged;
torrent playback is untouched; the constructor's `user-agent` override still applies.

**Baseline:** with an HTTP stream that requires a `Referer`, capture the current failure — record
the mpv error and that no frames decode. Save before touching code.

**Focused tests:**
- Qt Test: **new** `tests/auto/player/tst_http_header_fields.cpp` — the formatting function as data
  rows: empty map → empty string; one pair; multiple pairs; a value containing a comma; a value
  containing a colon. Registered in `tests/CMakeLists.txt` under label `unit` (follows the
  registered `colosseum.qttest.window_state_policy` pattern; `Qt6::Test` is discovered under
  `BUILD_TESTING` only and `Qt6Test.dll` is POST_BUILD-staged — both traps already solved there).
- Qt Quick Test: not applicable — no QML component behavior changes; the JS edit is covered by the
  harness below.
- Existing harnesses: `node tests/hosted_player_contract_test.mjs` and the full
  `ctest -L unit` gate must stay green. **New:** a loopback header-echo harness
  `tests/http_header_channel_harness.cpp` — a local `QTcpServer` (the `anime_order_service` pattern)
  that serves a tiny media file and records the request headers it received, proving the header
  reached the wire. Deterministic, no live network, registered under `unit`.
- Negative control: **required** — the same loopback fetch through plain `loadFile` must arrive
  with **no** `Referer`. If both paths look identical the channel is doing nothing and the slice is
  vacuous. Additionally: load a header-carrying source, then a header-free one, and assert the
  second request carries no leftover header (the leak test).

**Test seam status:** available — Qt Test is registered and running today; the loopback-server
pattern exists in the estate.

**Lanista actions:** `human-witnessed:` open Theatre → any title → Sources → pick a row from
NoTorrent → confirm the picture starts. Eyes are the right witness for "the video plays"; the
deterministic proof that headers reached the wire is the loopback harness above.

**Completion signal:** loopback harness prints its `OK` sentinel and exits 0; `ctest -L unit`
green.

**State / events / probes:** harness stdout records the exact header block received per request.

**Visual evidence:** none required (the human-witnessed play is the exhibit).

**Regression paths:** play one torrent source and one local file after this lands — the two
`loadFile` paths that must be unaffected.

**Evidence artifacts:** harness output + `ctest` output committed to the slice report; Hemanth's
verdict recorded.

**Bridge status:** not applicable — verification is deterministic plus human-witnessed.

**Completion criterion:** a header-requiring stream plays; the negative control proves the plain
path sends no header; the leak test proves no carry-over; `ctest -L unit` green; Hemanth has seen
a NoTorrent source play.

---

## Slice 2: The bridge can see a source row (prerequisite)

**Purpose:** no user-visible change. This exists so slices 5–7 can be proven in the running app
instead of by eye alone.

**Dependencies:** none (parallelisable with slice 1).

**Implementation guidance:** add named automation surfaces to `qml/SourcesSheet.qml`, following the
ledger's existing convention (`discoverCard_<id>` etc.):
- `sourcesSheet` on the root, exposing `loading` (bool), `rowCount` (int), `httpRowCount` (int).
- `sourceRow_<index>` on each delegate, exposing `streamKind` (string), `checkState` (string:
  `"checking"` / `"confirmed"`), `providerName` (string), `preselected` (bool).

`checkState` must be a plain string property so `ui-wait-for`'s **strict-equality-only** polling can
wait on it. Nothing else changes — no layout, no styling, no behavior.

**Behavior to preserve:** the sheet renders and behaves exactly as before; adding `objectName` and
read-only properties changes no visuals.

**Baseline:** run `dump-ui` against the daily app with the sheet open and record that **no** source
row appears. That is the "before" that makes this slice's value checkable.

**Focused tests:**
- Qt Test: not applicable — QML-only change, no C++ contract.
- Qt Quick Test: not applicable — this adds names, not behavior; a Quick Test asserting a name
  exists would restate the diff. The bridge check below is the real proof.
- Existing harnesses: `ctest -L unit` stays green (regression only).
- Negative control: **required** — before/after `dump-ui` diff. Before: zero `sourceRow_*`. After:
  one per visible row. A name that does not appear in `dump-ui` is not actually reachable, which is
  exactly the failure this slice exists to prevent.

**Test seam status:** not applicable — the deliverable is bridge reachability, proven by `dump-ui`.

**Lanista actions:** in a tagged session (`lanista session run --tag httpsrc-names --drive`):
`ui-wait-for bootSplash.visible == false` → `ui-click modePill_Theatre` → open a title → open the
sheet → `dump-ui` → `qml-get sourcesSheet` (`loading`, `rowCount`).

**Completion signal:** `ui-wait-for sourcesSheet.loading == false`.

**State / events / probes:** `dump-ui` lists `sourceRow_0…n`; `qml-get sourceRow_0` returns
`streamKind`, `checkState`, `providerName`.

**Visual evidence:** one grab of the sheet, to confirm nothing moved.

**Regression paths:** open the sheet in both `play` and `season` modes; scroll the row list.

**Evidence artifacts:** before/after `dump-ui` output, session manifest under
`artifacts/lanista-sessions/`.

**Bridge status:** available — `ui-click`, `ui-wait-for`, `qml-get`, `dump-ui` and tagged sessions
are all in the ledger's AVAILABLE section. `modePill_Theatre` and `bootSplash` are named surfaces
the ledger already documents.

**Completion criterion:** `dump-ui` shows a named row per visible source; `qml-get` returns the
four properties; the sheet is visually unchanged.

---

## Slice 3: Measure the two HTTP addons we already ship

**Purpose:** find out how much of the goal already ships before writing any house code. **This
slice can shrink or delete slice 6.**

**Dependencies:** slice 1 (without headers the measurement would under-report).

**Implementation guidance:** no product code. Install `com.notorrent.addon` and `webstreamr-mbg`
in a tagged session, then for **20 titles drawn from Theatre's own shelves** (Discover, Trending,
Continue Watching, plus one running series across three episodes) record: did any HTTP row appear,
did it play, at what quality, and which addon produced it. Also record WebStreamr's liveness, which
the spec flags as unverified. Write `docs/research/theatre-http-source/01-existing-addons.md`.

**Behavior to preserve:** the daily app's installed extension list is never touched — this runs in
a tagged session with its own AppData root.

**Baseline:** none to reproduce — **this slice IS the baseline.** No coverage figure exists yet for
any HTTP source in this app; that absence is exactly why the target is unset. Slices 5–6 measure
their delta against whatever this slice records.

**Focused tests:**
- Qt Test / Qt Quick Test: not applicable — investigation slice, no code.
- Existing harnesses: not applicable.
- Negative control: **required** — one title deliberately expected to fail (a genuinely obscure or
  archival pick) must show **no** HTTP row. If every title in a 20-sample "succeeds", the rig is
  reporting rows it has not actually confirmed, and the whole measurement is void.

**Test seam status:** not applicable — live-network investigation; never entered into any
deterministic gate, per the test ledger's rule on live-network work.

**Lanista actions:** tagged session per title batch; `ui-click` the source button;
`qml-get sourcesSheet.httpRowCount`; `qml-get sourceRow_0.providerName`.

**Completion signal:** `ui-wait-for sourcesSheet.loading == false` per title.

**State / events / probes:** per title — `httpRowCount`, provider names, and whether playback
started.

**Visual evidence:** grabs for a representative five titles.

**Regression paths:** not applicable — read-only measurement.

**Evidence artifacts:** `01-existing-addons.md` with the full 20-row table and the negative
control's result.

**Bridge status:** available (depends on slice 2's names).

**Completion criterion:** a 20-title table exists with the measured coverage figure, WebStreamr's
liveness settled, the deliberate-failure control recorded, and an explicit recommendation on
whether slices 5–6 are still needed at full size.

> **This slice produces the coverage target; it does not test against one.** An earlier draft
> carried an 18-of-20 gate that was invented before any title had ever been tested. Hemanth caught
> it. Slice 3 reports the real figure and **proposes** a target; **Hemanth ratifies it before slice
> 6 begins.** The only number fixed in advance is the floor: **below 10 of 20 the lane is a novelty
> and the arc is reconsidered rather than pushed.** Report the figure with its per-title table, and
> report *why* each miss missed (no provider had it / provider had it but preflight failed) — those
> two failures point at completely different work.

---

## Slice 4: VidKing is gone

**Purpose:** the hosted-player row disappears from Sources and Extensions; nothing offers a stream
it cannot verify.

**Dependencies:** slice 3 (do not remove the old path until the replacement path is measured as
viable).

**Implementation guidance:** delete `native/hostedplayer/`, `resources/hostedplayer/`,
`qml/HostedPlayerPage.qml`, `qml/HostedPlayerApi.js`, and `tests/hosted_player_{api_test.mjs,
bridge_harness.cpp, contract_test.mjs, webengine_smoke.qml}`. Strip the `hosted-player` resource,
`hostedRows`, `isHosted`, the `HOSTED PLAYER` row treatment and the `net.vidking.player` entry from
`qml/SourcesSheet.qml`, `qml/Main.qml`, `qml/TheatreSeries.qml`, `qml/TheatreApi.js`,
`qml/AddonClient.js`, `qml/ExtensionsCatalog.js`, `native/main.cpp`, `native/CMakeLists.txt`,
`native/app_resources.qrc`, `native/engine/ExtensionsStore.{cpp,h}`. Also update
`tests/extension_reorder_world_test.mjs`, `tests/extension_worlds_derivation_test.mjs` and
`tests/test_theatre_progress_parity.ps1`, which reference the hosted rows.

**Keep `Qt6::WebEngineQuick` in `CMakeLists.txt`** — `qml/reader2/*` depends on it. Removing it
breaks Biblio's reader.

**Behavior to preserve:** Biblio's Reader 2 opens and renders; Theatre's torrent sources are
unaffected; a user who previously had VidKing installed does not see a broken row (its entry is
dropped from `installed.json` on load, not left dangling).

**Baseline:** record the current Sources sheet with a VidKing row present, and the Extensions page
showing it installed. Grabs before deletion.

**Focused tests:**
- Qt Test: not applicable — deletion, no new C++ contract.
- Qt Quick Test: not applicable.
- Existing harnesses: `ctest -L unit` green; the three updated `.mjs`/`.ps1` gates green.
- Negative control: **required** — grep the tree for `hostedplayer|HostedPlayer|isHosted|hostedRows|vidking` and
  assert **zero** hits outside `dist/` and build directories. A partial removal that still compiles
  is the likely failure mode here.

**Test seam status:** available.

**Lanista actions:** tagged session: open Theatre → a title → Sources →
`qml-get sourcesSheet.rowCount`; `dump-ui` must contain no hosted row. Then
`human-witnessed:` open Biblio and open any book, confirming the reader still renders — the bridge
cannot judge a rendered book page, and WebEngine is the shared risk in this slice.

**Completion signal:** `ui-wait-for sourcesSheet.loading == false`.

**State / events / probes:** `dump-ui` free of hosted-player names; Extensions list free of
`net.vidking.player`.

**Visual evidence:** before/after grabs of both the Sources sheet and the Extensions page.

**Regression paths:** open Extensions → Installed and Browse tabs; play one torrent source; open
one book in Biblio.

**Evidence artifacts:** grep output, before/after grabs, session manifest, Hemanth's reader verdict.

**Bridge status:** available for the Sources/Extensions checks; the Biblio reader check is
human-witnessed by design.

**Completion criterion:** zero references remain, the app builds, torrent sources play, Biblio's
reader opens under Hemanth's eyes, and `ctest -L unit` is green.

---

## Slice 5: The house source appears, and tells the truth about itself

**Purpose:** a single "House HTTP" row appears in Extensions showing how many providers are alive,
and its rows appear in Sources as `checking…` then `Checked · plays now` — or vanish.

**Dependencies:** slices 1, 2, 4.

**Implementation guidance:**
- **Public** `native/httpsource/`: `HttpSourceRegistry` (holds providers, runs the sweep, enforces
  the 4 s per-provider timeout, tracks health), `IHttpProvider` (abstract: `accepts(type)`,
  `resolve(type, id)` → candidate links + required headers), and `HttpSourcePreflight` (the ranged
  `Range: bytes=0-1` probe; for an `.m3u8`, fetch and require it to parse as a playlist).
- Registered as an app-owned Extensions row via the existing `installBundled()` path (the mechanism
  built for VidKing, reused). Resolved **in-process** — no localhost server, no public endpoint.
- `AddonClient` asks it alongside remote addons and receives the standard stream-object shape.
- Extensions row subtitle: `N of M providers responding`, or `no providers responding` at zero.
- **CMake:** include a private provider tree when present beside the repo; when absent the target
  still builds and the registry holds zero providers. Ship this slice with **one** trivial
  reference provider behind a test flag so the machinery is exercisable without the private tree.

**Behavior to preserve:** torrent rows appear and sort exactly as today; the sheet stays interactive
while HTTP rows resolve; a title with no HTTP source shows the quiet no-direct-links line and never
auto-plays anything (Hemanth's no-silent-switching rule).

**Baseline:** post-slice-4 sheet — record `rowCount`/`httpRowCount` with no house source present.

**Focused tests:**
- Qt Test: **new** `tests/auto/httpsource/tst_http_source_registry.cpp` — sweep timeout drops a slow
  provider; health counts N-of-M correctly at 0, partial and full; a provider that throws does not
  take down the sweep; preflight admits 200 and 206 and rejects 403/404. Fake providers only, no
  network. Registered under `unit`.
- Qt Quick Test: **new** `tests/qml/tst_sources_sheet_check_state.qml` — a row driven through
  `checking → confirmed` renders `Checked · plays now`; a row driven to failure removes itself;
  confirmed rows sort above torrent rows. Runs under the existing `colosseum.qml` runner (labels
  `qml;windows` — real windows, never offscreen).
- Existing harnesses: `ctest -L unit` green.
- Negative control: **required, two.** (1) A provider stubbed to return a link that fails preflight
  must produce **no** row — if it still renders, the receipt is a lie and criterion 4 of the spec is
  already broken. (2) Force one provider down and assert the Extensions health line drops by exactly
  one; a line that reads the same regardless of provider state is decorative, not honest.

**Test seam status:** available — Qt Test and the Qt Quick Test runner are both registered and
green today.

**Lanista actions:** tagged session: `ui-click modePill_Theatre` → open a title → open Sources →
`ui-wait-for sourceRow_0.checkState == "confirmed"` → `qml-get sourceRow_0` (`providerName`,
`streamKind`) → `qml-get sourcesSheet.httpRowCount`. Then Extensions → `qml-get` the house row's
subtitle.

**Completion signal:** `ui-wait-for sourceRow_0.checkState == "confirmed"` (strict equality, no
sleep).

**State / events / probes:** `httpRowCount` > 0; `sourceRow_0.streamKind == "Direct"`; health
subtitle string matches `N of M providers responding`.

**Visual evidence:** grabs of the sheet mid-check and after confirmation, and of the Extensions row
showing the health line. **Hemanth's eyes are the closing gate on how the check state reads** —
whether `checking…` feels quiet rather than noisy is his judgment, not the bridge's.

**Regression paths:** open the sheet twice in a row (second open must re-sweep, not show stale
rows); open in `season` mode; navigate away mid-check and back.

**Evidence artifacts:** grabs, `dump-ui`, session manifest, both negative-control outputs.

**Bridge status:** available (depends on slice 2).

**Completion criterion:** confirmed rows render and sort above torrents; a failing-preflight
provider yields no row; the health line moves with provider state; the sheet never blocks; Hemanth
has approved how the states read.

---

## Slice 6: Providers until the bar is met

**Purpose:** anything on Theatre's shelves offers a confirmed HTTP row.

**Dependencies:** slice 5, and **sized by slice 3** — if the existing addons already clear the bar,
this slice is a thin gap-filler or does not run at all.

**Implementation guidance:** add providers to the private tree one at a time, each declaring
accepted id types, content types and required headers. Prefer providers that are awkward for
browsers — the spec's native-advantage point: we play through mpv and need none of the CORS/proxy
scaffolding the web frontends depend on. **A provider requiring a login, captcha, paywall or
copy-protection bypass is recorded as a disqualifier and dropped, never defeated.** Provider count
is an outcome, not a target.

**Behavior to preserve:** the 4 s sweep budget — adding providers must not slow the sheet; the
sweep is parallel and the budget is per-sweep, not per-provider.

**Baseline:** slice 3's 20-title table, re-run before any provider is added.

**Focused tests:**
- Qt Test: each provider gets a parsing test against a **recorded fixture response** (no live net) —
  its extraction returns the expected link and headers, and returns empty on a malformed page rather
  than throwing. Registered under `unit`.
- Qt Quick Test: not applicable — no new QML.
- Existing harnesses: `ctest -L unit` green.
- Negative control: **required** — a fixture with the provider's markup deliberately mangled must
  yield an empty result, not a wrong link. A parser that returns something for garbage will
  eventually hand Hemanth a dead link with a receipt on it.

**Test seam status:** available.

**Lanista actions:** re-run slice 3's 20-title sweep in a tagged session and record the pass count.

**Completion signal:** `ui-wait-for sourcesSheet.loading == false` per title.

**State / events / probes:** per title — `httpRowCount` and whether a confirmed row exists.

**Visual evidence:** grabs for five representative titles including one that legitimately has no
HTTP source.

**Regression paths:** the running series' three episodes; one 4K title; one non-English title.

**Evidence artifacts:** the re-run 20-title table beside slice 3's, so the delta is visible.

**Bridge status:** available.

**Completion criterion — two parts, and only the first is pass/fail:**

1. **The honesty gate, absolute.** In the whole 20-title sweep, **no row that says
   `Checked · plays now` may fail to start playing.** One violation fails this slice. This is the
   feature's entire promise and it does not depend on any coverage guess.
2. **The coverage target Hemanth ratified after slice 3**, measured per title and reported beside
   slice 3's baseline so the delta from the house providers is visible.

**Stop rule, so this slice cannot run forever:** measure coverage after each provider is added.
When a newly added provider brings **fewer than one additional title** into the 20-sample, stop
adding providers and report the figure. Chasing the long tail past that point costs maintenance
forever and buys almost nothing — the P-Stream half-life makes every extra provider a standing
liability, not a one-off cost.

---

## Slice 7: A series remembers what worked

**Purpose:** episode 4 opens already pointed at the source that played episode 3, labelled, one
click to change.

**Dependencies:** slice 5.

**Implementation guidance:** on a successful play, store the row's `bingeGroup` (or, absent one,
provider id + series id) against the series in the existing progress/collection persistence — **no
new store**. When the sheet opens for another episode of that series, mark the matching row
`preselected` and label it `Continuing on <provider>`. If that row fails preflight this episode,
clear the preselection and fall back to normal ordering.

**Behavior to preserve:** the choice is visible and reversible in one click — never a silent
switch. Torrent rows may also carry `bingeGroup`; the same memory applies to them, which is a free
gain, but must not change their ordering.

**Baseline:** today, opening episode 4 shows no preselection. Record it.

**Focused tests:**
- Qt Test: not applicable — the memory rides existing persistence; the selection logic is QML.
- Qt Quick Test: **new** `tests/qml/tst_binge_continuity.qml` — a stored group preselects the
  matching row; a stored group whose row is absent this episode preselects nothing; a preselected
  row that fails preflight clears the mark. Under the `colosseum.qml` runner.
- Existing harnesses: `ctest -L unit` green.
- Negative control: **required** — with the stored memory deliberately cleared, episode 4 must show
  **no** preselection. A test that passes with and without the stored value is proving nothing.

**Test seam status:** available.

**Lanista actions:** tagged session: play episode 3 to a confirmed source, stop, open episode 4's
sheet → `ui-wait-for sourceRow_0.preselected == true` → `qml-get sourceRow_0.providerName` and
assert it equals the provider used for episode 3.

**Completion signal:** `ui-wait-for sourceRow_0.preselected == true`.

**State / events / probes:** `providerName` matches across episodes; one `ui-click` on another row
clears `preselected`.

**Visual evidence:** grab of episode 4's sheet showing the `Continuing on …` label.

**Regression paths:** a different series in the same session must not inherit the memory; restart
the app and reopen episode 4 (the memory must survive); switch source manually and confirm episode
5 remembers the **new** choice.

**Evidence artifacts:** grabs, session manifest, negative-control output.

**Bridge status:** available.

**Completion criterion:** episode 4 preselects episode 3's working source, labelled; clearing the
memory removes the preselection; a manual change is remembered next episode; the choice is always
visible and one click to override.

---

## Plan self-review (performed at write time)

1. **Spec coverage.** Header channel §6.1 → slice 1. Public/private seam §6.2 and in-process
   registration §6.3 → slice 5. Preflight §6.4 → slice 5. Binge §6.5 → slice 7. VidKing removal
   §6.6 → slice 4. Sequence §7 → slice order, with the bridge prerequisite inserted as slice 2
   because the ledger check showed the sheet is unreachable. All nine acceptance criteria in §8 map
   to a slice completion criterion: 1–2→slice 1, 3→slice 6, 4→slices 5 and 6, 5→slice 5, 6→slice 5,
   7→slice 5, 8→slice 7, 9→slice 4.
2. **Ledger honesty.** Both ledgers read fresh today. Every Lanista action named
   (`ui-click`, `ui-wait-for`, `qml-get`, `dump-ui`, grabs, tagged `session run`) is in the
   AVAILABLE section; `modePill_Theatre` and `bootSplash` are documented named surfaces. **The one
   capability the sheet needed did not exist** — no `objectName` anywhere in `SourcesSheet.qml`, so
   `dump-ui` cannot see a row. Rather than write "verify manually", slice 2 adds the smallest
   bridge prerequisite and every later slice depends on it. Every `ui-wait-for` is a strict property
   equality, per the ledger's explicit limit. **No sleeps anywhere.** Live-network work (slices 3, 6)
   is kept out of the deterministic gate per the test ledger's rule.
3. **Negative controls.** Every slice has one and each is a real falsifier, not ceremony: the
   plain-path-sends-no-header check (slice 1) is the difference between a working channel and a
   vacuous pass; the leak test catches the bug that would poison the *next* stream; the before/after
   `dump-ui` diff (slice 2) catches names that are declared but unreachable; the deliberate-failure
   title (slice 3) catches a rig that reports unconfirmed rows; the grep sweep (slice 4) catches a
   partial removal that still compiles; the failing-preflight provider (slice 5) directly tests the
   spec's central promise that a receipt never lies; the mangled fixture (slice 6) catches a parser
   that returns garbage confidently; the cleared-memory check (slice 7) catches a test that passes
   either way.
4. **Human-witnessed where eyes are right.** Slice 1's "the video plays", slice 4's "the book reader
   still opens", and slice 5's "the check state reads quietly" are ordered as human-witnessed with
   exact repro steps — a five-second look beats a flaky drive, and the aesthetic call on how the
   states read is Hemanth's by house law.
5. **Safety.** Every driving slice runs in a tagged disposable session with a unique pipe and its
   own AppData root; the daily app's extension list, progress and settings are never touched. Slice
   3 installs addons only inside a tagged session.
6. **Order and gating.** 1 and 2 are independent and may run in parallel; 3 needs 1; 4 needs 3; 5
   needs 1, 2, 4; 6 needs 5 and is sized by 3; 7 needs 5. **Slice 3 can legitimately shrink or
   delete slice 6, and that is a success.**
7. **No invented numbers survive.** An earlier draft carried an 18-of-20 coverage gate; Hemanth
   challenged it and it was fabricated — nothing had been measured. It is gone. Slice 6's pass/fail
   is now the **honesty gate** (no row that claims `Checked` may fail to play — absolute, one
   violation fails), while coverage is a **measurement** slice 3 produces and Hemanth ratifies. The
   only figure fixed in advance is a floor of 10 of 20, below which the arc is reconsidered rather
   than pushed. Slice 6 also carries a stop rule so provider-chasing cannot run forever.

---

*Plan ends. Execute under `brotherhood-executing-plans`. Slice 1 gates everything — nothing else in
this arc works until a header reaches the wire.*
