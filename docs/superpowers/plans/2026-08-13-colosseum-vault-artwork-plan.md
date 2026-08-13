# Colosseum Vault — Browse Artwork · Execution Plan

**From spec:** `docs/superpowers/specs/2026-08-13-colosseum-vault-artwork-design.md` (ratified 2026-08-13).
**Planner:** Agent 0 (Claude, Opus). **Executor:** fresh session under `brotherhood-executing-plans`.
**Ledgers this plan traces to:** `docs/colosseum-test-verification.md` (native/QML),
`docs/colosseum-lanista-verification.md` (runtime bridge). Read both before executing.

## Gates (from the test ledger, verbatim)

- Native unit gate: `C:\Qt\Tools\CMake_64\bin\ctest.exe --test-dir native/build-msvc -L unit --output-on-failure`
- QML gate: `C:\Qt\Tools\CMake_64\bin\ctest.exe --test-dir native/build-msvc -R colosseum.qml --output-on-failure`
- Build: `native/build-msvc.bat` (MSVC 2022 + Ninja). Exit codes lie — grep the log for
  `error C|error LNK|ninja: build stopped`. One build per out/ dir; a running exe locks its own
  `.exe` (kill by PID or compile the object only).

## Runtime capabilities this plan uses (all in the Lanista ledger's AVAILABLE NOW section)

`qml-get` (read a named property, equality), `ui-wait-for` (poll a property until **equal**, no
operators), `ui-query`/`dump-ui` (structure + geometry, L1-Bridge), `get-state` (root windows +
resolved `appDataRoot`/`cacheRoot` — the isolation proof), item/window `grab`, and isolated sessions
via `lanista session run --tag <t> --ready-ms 60000`. Existing Vault scenario:
`tests/lanista_scenarios/vault_browse_smoke.json`.

**Two ledger limits this plan respects, not fights:**
- `invoke-read` exposes **no Vault methods** (8 allowlisted: 6 TankobanVolumes + 2 BiblioImageDiag).
  This plan reads Vault runtime state through `qml-get` on exposed QML properties, never `invoke-read`.
  No slice is Bridge blocked as a result.
- **Qt/D3D is uncapturable headless** and grabs on a windowed session land on Hemanth's screen. So
  every *pixel/aesthetic* claim is **human-witnessed**; the bridge proves *state* (a tile's coverRef
  flipped, a waitable art flag went true), never taste.

## Cross-lane coordination (declared)

- `VaultForensics::coverRefProvenance` (Assistant 1 / F1-Core, `b0fde45`, currently 2/13 unbuilt)
  assumes a Film coverRef is `file://`-only. The shipped comic fix (`dc8a5fd`) and Slice 3 below make
  coverRef also hold `image://comiccover|vaultbookcover/…` and a cached poster/thumb ref. **Flagged on
  `Brotherhood/agents/chat.md`; his lane folds the new provenance branches when F1-Core stabilizes.**
  Not a slice this plan owns.
- Touches `native/CMakeLists.txt` (new test target registration) — additive only, grep-verify and post
  to chat.md per shared-file discipline; use the surgical-blob commit (both ledgers carry other lanes').

---

## Slice 1 — Persistent frame-grab producer (internal)

**Purpose:** turn the player's throwaway hover-frame extraction into a persisted, cached still that a
video tile can later wear as its picture. No user-visible change yet.

**Dependencies:** none.

**Implementation guidance:** a small engine component (e.g. `VaultThumbnailer` beside
`native/engine/VaultEnricher.*`, or a method on the enricher) that, given a video file path, runs one
ffmpeg still — reuse SeekThumbnailer's exact invocation (`-ss <t> -i <src> -frames:v 1 -vf scale=…
-f mjpeg`, `native/player/seekthumbnailer.cpp:92-102`) at an offset **~10 % into the file, floored to
a few seconds in** to skip black/logo (duration is already cached in `VaultEnricher`'s
`durations.json`; fall back to a fixed 60 s when unknown). Write the JPEG to a **persistent** file in
the enricher's injected `cacheDir` (`VaultEnricher.cpp:225`), named by the same triple key the
duration cache uses — `(normPath, size, mtimeMs)` (`durationKey`). Idempotent: a cache hit returns the
path and does **not** re-spawn ffmpeg. Off the GUI thread (async `QProcess`, as SeekThumbnailer
already is); a small bounded in-flight count.

**Behavior to preserve:** SeekThumbnailer's hover path is untouched (this is a sibling, not a
refactor of it); `VaultEnricher`'s existing duration cache and comic-cover behavior unchanged.

**Baseline:** before the change, no thumbnail file exists in `cacheDir` for the fixture MP4 — show the
empty cache dir listing.

**Focused tests:**
- **Qt Test:** new cases (extend `colosseum.qttest.vault_enricher` — its ledger row already lists
  "video thumbnails" as deferred — or a new `colosseum.qttest.vault_artwork` target compiling the
  producer). Drive the **existing decodable-MP4 fixture** (the one `vault_admission_probe_harness`
  uses): (a) a still file is produced at the keyed cache path; (b) second call is a cache **hit** —
  file mtime unchanged, no new process; (c) a missing/corrupt input yields **no file and an honest
  false**, never a wedge.
- **Qt Quick Test:** not applicable — no QML in this slice.
- **Existing harnesses:** `vault_enricher` must stay green (5/5) if extended in place.
- **Negative control:** disable the cache-hit short-circuit so the producer re-spawns ffmpeg on the
  second call → the "hit leaves file untouched / no second process" case goes red; restore.

**Test seam status:** available — fixture MP4 exists; ffmpeg is already used to build vault fixtures,
so it is present in the test environment. No live network.

**Lanista actions:** none (internal slice).
**Completion signal / State / Visual / Regression / Evidence:** not applicable (internal).
**Bridge status:** not applicable.

**Completion criterion:** the new Qt Test cases pass under `-L unit`, negative control produced exactly
the expected red and was restored, and `-L unit` is green overall (baseline red `vault_forensics`
excepted — foreign lane). Done = deterministic-tested, no runtime theater.

---

## Slice 2 — Canonical poster fetcher + cache (internal)

**Purpose:** fetch a recognized item's Cinemeta/metahub poster once and keep it locally, so it can
paint offline later. No user-visible change yet.

**Dependencies:** none (parallel to Slice 1).

**Implementation guidance:** an engine component that, given a poster URL (the
`live.metahub.space/poster/medium/<tt>/img` that `VaultIdentifier.cpp:182` already derives into
`FileRow::identityCoverUrl`) and a stable id (the identity id), downloads the image **off the GUI
thread** into the enricher `cacheDir`, keyed by identity id; idempotent (cache hit skips the fetch);
a failed fetch leaves **no file** and reports honest failure (retried opportunistically later, never a
broken-image marker). C++ owns transport (QML-paints/C++-decides).

**Behavior to preserve:** the Vault's offline-first contract — nothing in the *browse read path*
blocks on network; fetching is a background side-effect whose only output is a local cache file.

**Baseline:** no poster file in `cacheDir` for the fixture id before the change.

**Focused tests:**
- **Qt Test:** in the `vault_artwork` target. **No live network** (ledger rule). Pass a **local
  `file://` fixture-image URL** (a committed tiny PNG/JPG) as the "poster URL": (a) it lands at the
  id-keyed cache path; (b) second call is a cache hit (no re-copy); (c) a non-existent URL yields no
  file + honest failure.
- **Qt Quick Test:** not applicable.
- **Existing harnesses:** none affected.
- **Negative control:** flip the failure branch to write a zero-byte file on error → the
  "failure leaves no file" case goes red; restore.

**Test seam status:** available — local fixture URL, no network. **The live-metahub fetch is proven
only at runtime (Slice 3/5), never in a deterministic gate.**

**Lanista actions / signals / visual / regression / evidence:** not applicable (internal).
**Bridge status:** not applicable.

**Completion criterion:** new Qt Test cases green under `-L unit`; negative control red-then-restored;
`-L unit` green overall. Done.

---

## Slice 3 — Artwork resolver + projection rewire (USER-VISIBLE: art appears)

**Purpose:** every browse tile starts wearing the best picture it has — recognized items show their
poster, individual videos show a frame from themselves, folders/shows/seasons get artwork for the
first time.

**Dependencies:** Slices 1 and 2.

**Implementation guidance:** an **artwork resolver** in the Vault engine that, for a browse group,
walks the §2 ladder and returns **one ready-to-paint local ref** (`file://` cache path, or the
existing `image://comiccover|vaultbookcover/…` for comics/books): locked pick (reserved, always empty
now) → local poster file (`VaultEnricher::findLocalArtwork`, already produces a `file://`) → canonical
poster (Slice 2's cache; if absent, request a background fetch of `identityCoverUrl`) → frame-grab
(Slice 1's cache; if absent, request a background grab of the group's video) → "" (typographic). On a
just-produced file the resolver emits a **ready** signal that the page turns into a re-projection
(the existing identify-in-place path: `qml/VaultPage.qml` `syncGridModel`/`gridSyncedLevelKey`).
Rewire `VaultLibrary::browseAt` (Film branch — already prefers `identityCoverUrl` as of `dc8a5fd`),
`items()`, and `series()` to hand the tile the **resolver's local ref**, never the raw remote
`identityCoverUrl`. Extend the projection so **Show / Season / Folder** nodes also carry a resolved
`coverRef` (today only Film does — cause (c) in the spec); a Season with no season art inherits the
Show's. Request art **on demand** when a tile enters the viewport; bounded concurrency; newest-first.
Add the runtime seam: the poster/wide tile exposes `readonly property bool hasArt: coverRef !== ""`.

**Behavior to preserve:** the shipped comic/book covers (`dc8a5fd`) still paint; the §4.7 fallback
(missing art → typographic, never an empty/broken frame) still holds; `away`/offline projection
(`offlineBrowseAt`, `vault_browse_away`) unchanged; the browse grid's node counts and drill/back
behavior (`vault_browse_smoke`) unchanged.

**Baseline:** in an isolated session on the fixture library, `qml-get` a known video tile's `coverRef`
== "" and observe the typographic tile (the current no-art state) before the change.

**Focused tests:**
- **Qt Test:** in `vault_artwork` — resolver **rung selection** given synthesized rows: locked>local>
  canonical>frame>empty precedence; a comic row still yields the `image://comiccover/…` ref (guards
  the `dc8a5fd` behavior); a Show/Season/Folder row now yields a ref when art exists and inherits the
  show poster for a season with none. The resolver's producer calls are **stubbed** (inject the two
  producers) so this stays pure/offline. Assert the **ready signal fires** exactly when a rung
  produces a new local ref.
- **Qt Quick Test:** extend the Vault QML suite (`colosseum.qml` gate) — a `VaultPosterCard`/
  `VaultWideCard` fed a non-empty `coverRef` sets `hasArt === true` and mounts its `Image`; fed "" it
  shows the typographic face and `hasArt === false`.
- **Existing harnesses:** `vault_browse_detail` (7/7), `vault_browse_away` (4/4), `vault_kit` (44/44)
  stay green; `colosseum.qml` stays green.
- **Negative control:** invert the ladder precedence (frame before canonical) in production → the
  precedence case goes red; restore. Then, mutating **production** not the test, force the resolver to
  return "" for a comic row → the `dc8a5fd` guard case goes red; restore (proves the test guards the
  shipped covers, per the vacuous-test lesson).

**Test seam status:** available — resolver is pure with injected producers; the QML seam is a plain
property.

**Lanista actions:** in a fresh isolated session (`lanista session run --tag vault-art-s3
--ready-ms 60000`, isolation proven via `get-state` `appDataRoot`/`cacheRoot` ≠ live), seeded with the
fixture library that contains a **plain (uncatalogued) video** whose frame-grab is deterministic and
offline: navigate to its level, `qml-get` the tile's `coverRef` (baseline "") → `ui-wait-for` the same
tile's **`hasArt` equal to `true`** (the deterministic completion signal; frame-grab is local ffmpeg,
no network) → `qml-get coverRef` again and record the resolved value. `dump-ui` the grid to confirm
every tile is present and none collapsed. Drive **Back / re-enter** and `ui-wait-for hasArt == true`
again to prove the cache serves the second visit.

**Completion signal:** `ui-wait-for` on tile `hasArt == true` (equality; no sleep). The canonical-
poster (network) path is **not** waited on here — it is human-witnessed in Slice 5.

**State / events / probes:** `qml-get` tile `coverRef` (was "", now a local ref); `qml-get` grid
`count`/level key unchanged across the resolve (no accidental re-scan); `get-state` `cacheRoot` shows
the thumbnail file written under the isolated cache.

**Visual evidence:** item-grab of the resolved tile as an **exhibit** (not a pass condition — Qt/D3D
uncapturable; pixels land a later frame than state). The pass is the `hasArt`/`coverRef` state.

**Regression paths:** replay `tests/lanista_scenarios/vault_browse_smoke.json` in a fresh isolated
session — grid node counts, drill-in, detail sheet, and Back-restores-grid all unchanged. Re-run the
known foreign flakes note before blaming a `colosseum.qml` red.

**Evidence artifacts:** the isolated session run dir (grabs + logs) and an eyes-on subfolder under
`agents/eyes-on/2026-08-13-vault-artwork/`.

**Bridge status:** available (every capability above is in AVAILABLE NOW).

**Completion criterion:** `Runtime-validated` — Qt Test + Qt Quick Test green under their gates with
the two negative controls shown; in the isolated session a video tile's `hasArt` goes `true` and its
`coverRef` reads a local cache ref on both first and second visit; `vault_browse_smoke` regression
replays green; `-L unit` green (foreign `vault_forensics` excepted).

---

## Slice 4 — Fade-in on art arrival (USER-VISIBLE polish)

**Purpose:** art doesn't pop harshly — a tile's picture fades in when it resolves, so the wall filling
feels intentional.

**Dependencies:** Slice 3.

**Implementation guidance:** in `VaultPosterCard`/`VaultWideCard`, animate the cover `Image`'s opacity
0→1 on first successful load (`Image.status === Image.Ready`), house-token duration/easing from
`Theme.qml`. No spinner — the typographic face is the complete pre-art state, not a loading state.

**Behavior to preserve:** a tile that already has cached art on first paint shows it immediately (the
fade is for *arrival*, not every paint); typographic tiles are unaffected.

**Baseline:** with Slice 3 only, art snaps in without transition — note it.

**Focused tests:**
- **Qt Test:** not applicable (pure QML presentation).
- **Qt Quick Test:** in the `colosseum.qml` gate — driving the cover `Image` to `Ready` runs the
  opacity transition to 1; a tile constructed already-Ready does not animate from 0 (no flash).
- **Existing harnesses:** `colosseum.qml` stays green.
- **Negative control:** remove the `status === Ready` guard so the animation runs on an empty source →
  the "no animation without a source" case goes red; restore.

**Test seam status:** available (Qt Quick Test).

**Lanista actions:** none new — the fade is motion, and **motion feel is human-witnessed** (Qt/D3D
uncapturable headless; the Qt Quick Test proves the transition *exists and is guarded*, not how it
feels).

**Completion signal / probes:** the Qt Quick Test's transition assertion (deterministic).
**Visual evidence:** folded into Slice 5's eyes-on (the fade is judged live).
**Regression paths:** `colosseum.qml` full gate.
**Evidence artifacts:** `colosseum.qml` result; Slice 5 gallery.
**Bridge status:** not applicable (no bridge action; human-witnessed in Slice 5).

**Completion criterion:** `colosseum.qml` green with the transition test + negative control shown;
feel confirmed as part of Slice 5's eyes-on.

---

## Slice 5 — Eyes-on closeout on the real library (USER-VISIBLE gate)

**Purpose:** Hemanth sees his own wall of art — real posters on recognized shows/movies, real frames
on his clips (incl. Cricket), comics wearing their covers, offline still painted — and gives the
verdict.

**Dependencies:** Slices 3 and 4.

**Implementation guidance:** none — verification only. Build the assembled app from the committed tree
(`native/build-msvc.bat`, not the working copy), launch it on Hemanth's **real** library (not a
fixture), let it resolve art with the network **available** (this is the only place the live metahub
fetch and real Cinemeta posters are exercised).

**Behavior to preserve:** everything shipped in Slices 3–4.

**Baseline:** the pre-artwork wall (mostly typographic) — the Slice-10 browse eyes-on gallery already
captured this; reference it.

**Focused tests:** not applicable (closing human gate; the deterministic layers closed in Slices 1–4).

**Lanista actions:** `human-witnessed` — repro steps for Hemanth's eyes:
1. Open the Vault; **a recognized show/movie wears its real poster** (Cinemeta) — first time with
   network on.
2. Toggle the network off / unplug and re-open — **the same poster still paints** (from cache),
   proving offline.
3. Navigate to a plain clip (e.g. **Cricket**) — **it wears a real frame** from the footage, not type.
4. An **unrecognized movie** shows a **cropped frame** filling its tall tile; card shape matches its
   neighbours.
5. A comic still wears its cover (`dc8a5fd`).
6. Scroll fast — tiles **fade in** as they arrive; nothing shows an empty or broken frame.

**Completion signal:** Hemanth's recorded verdict in the evidence folder (the human-witnessed
contract — his words, not a shrug).

**State / events / probes:** optional `qml-get` on a poster tile's `coverRef` to show it reads a local
cache path even with the network off (state backing the eyes).

**Visual evidence:** an eyes-on gallery under `agents/eyes-on/2026-08-13-vault-artwork/` — mock/spec
beside the real wall, the offline-still shot, a Cricket frame, an unmatched-movie crop.

**Regression paths:** the whole browse face — drill in/out, away/back, restart — unchanged from before
artwork.

**Evidence artifacts:** the gallery folder + Hemanth's verdict line.

**Bridge status:** not applicable (human-witnessed by design — this is taste + a network toggle no
bridge should own).

**Completion criterion:** Hemanth's eyes-on approval recorded. This closes the feature.

---

## Plan self-review (done before presenting)

- Every user-visible slice (3, 4, 5) carries baseline, focused tests split by layer, a **deterministic
  completion signal** (`hasArt == true`; Qt Quick transition; recorded human verdict), regression
  paths, and evidence artifacts. ✓
- Every Lanista action traces to the ledger's AVAILABLE NOW section; **no invented capability**; the
  `invoke-read`/Vault gap and Qt/D3D-uncapturable limits are respected, not worked around. ✓
- No slice is Bridge blocked (nothing needs a Planned/Unavailable capability). ✓
- Internal slices (1, 2) are genuinely invisible and rely on focused tests; no runtime theater. ✓
- **No live network in any deterministic gate** — the metahub fetch is runtime/human-witnessed only. ✓
- Every regression test names a **negative control** that mutates production, per the vacuous-test
  lesson. ✓
- Isolation: every driving slice uses an isolated session (`--tag`, proven via `get-state`), never live
  data. ✓

## Execution routing

- Slices 1–3 (C++ engine + build + own-tool verification) → **Claude** (subagent or main), the caller's
  live build/test tools are needed. Slice 1's ffmpeg-shell and Slice 2's cache I/O are mechanical
  enough to hand off if desired, but the resolver/projection rewire (Slice 3) is integration-critical
  and stays Claude.
- Slices 4–5 (QML + Lanista + eyes-on) → **Claude** with the bridge; Slice 5's verdict is Hemanth's.
- Agent 0 reviews every slice at the gate (own negative control, committed-artifact rebuild) before
  it is `Runtime-validated`.
