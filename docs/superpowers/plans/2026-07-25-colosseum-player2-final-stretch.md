# Player 2 — Final Stretch Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Take the ABBA-proven Player 2 integration (63.6% less GPU) from "plays but rough" to Task-18-ready: torrent seek fixed, boot-mode traps closed, the CPU regression trimmed, the three queued parity items built, and the integrated ledger re-verified with Hemanth's eyes.

**Architecture:** Everything stays inside the shape Task 17 established — engine in `native/player2` (pure C++, D3D11), production host in `qml/player2host` (shares the app's JS orchestration), one Loader line in `qml/Main.qml`. The one architectural correction: the backend is a **boot choice** (D3D11 vs OpenGL process-wide RHI), so all "fall back to mpv at runtime" machinery inside a Player 2 boot is a lie and gets replaced by the error surface.

**Tech Stack:** Qt 6.11 / QML / MSVC / FFmpeg (lab build) / D3D11. Build via `native/_appbuild.bat` (regenerated in Task 0). Tests: player2 ctest suite + PowerShell contracts + on-disk QML probes run through the real `colosseum.exe`.

**Model routing:** This plan was written on Fable. **Execute on Opus** (`/model opus`), per standing routing.

**Non-negotiables carried from the session:**
- Never hand Hemanth an unverified launcher/build — every task ends with the thing actually run.
- `instrument before claiming a root cause` — Task 3 is diagnosis-gated, not guess-gated.
- Contracts are right when they fail us. Never weaken one to make a task pass.
- Eyes-on is Hemanth's gate. Probes prove "renders/loads"; only he closes ledger rows.

**Explicitly OUT of this plan:** the Task 18 default flip (his explicit go), discrete-GPU matrix row (no second GPU), mpvqt removal, screenshot/GIF + live/DVR (accepted exceptions).

---

## File map

| Area | Files |
|---|---|
| Pending fixes (already on disk, uncommitted) | `qml/player2/Player2Shell.qml` (TopBar re-parent), `native/player2/Player2Backend.{h,cpp}` (real error surfacing) |
| Boot coherence | `qml/Main.qml`, `qml/player2host/Player2Page.qml`, `native/player2/Player2Backend.{h,cpp}`, `tests/player2/player2_integrated_contract.ps1`, Desktop launcher |
| Torrent seek | `native/player/streamserver.cpp` (READ ONLY — evidence), `tests/player2/player2_http_fixture_server.ps1`, `native/player2/network/HttpMediaSource.{h,cpp}`, `tests/player2/player2_http_media_test.cpp` |
| CPU trim | `native/player2/Player2Backend.cpp` |
| Chapters | `qml/player2/controls/SeekBar.qml`, new `qml/player2/controls/ChapterMenu.qml`, `qml/player2/controls/TransportBar.qml` |
| Cache fill | `native/player2/network/HttpMediaSource.{h,cpp}`, `native/player2/core/DemuxSession.{h,cpp}`, `native/player2/core/Player2Session.{h,cpp}`, `qml/player2/controls/SeekBar.qml`, `tests/player2/player2_http_media_test.cpp` |
| Compact folds | `qml/player2/controls/TransportBar.qml` |
| Debt | `qml/player2/controls/TopBar.qml` + `TransportBar.qml` (BarButton unification) |
| Cross-lane | `agents/chat.md` (Brotherhood repo), books smoke via launcher |

---

### Task 0: Land the two fixes already sitting on disk

Uncommitted right now: the TopBar re-parent (fixes the layout bleed Hemanth photographed — the bar was mounted inside `bottomDock`, so `anchors.top` meant the top of the *bottom* cluster, and it duplicated the chrome's scrim + clock) and the Player2Backend real-error surfacing (`errorOccurred` → `m_lastError`, so failures report the engine's actual reason instead of "entered an error state"). Task 3 depends on the second.

**Files:**
- Commit as-is: `qml/player2/Player2Shell.qml`, `native/player2/Player2Backend.h`, `native/player2/Player2Backend.cpp`
- Create: `native/_appbuild.bat` (was cleaned; recreate, keep untracked)

- [ ] **Step 1: Recreate the build script** (untracked helper, do not commit):

```bat
@echo off
setlocal
cd /d "%~dp0"
set "QTFRAMEWORK_BYPASS_LICENSE_CHECK=1"
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul || (echo vcvars64 FAILED & exit /b 1)
"C:\Qt\Tools\CMake_64\bin\cmake.exe" --build build-msvc --target colosseum || (echo BUILD FAILED & exit /b 1)
echo BUILD_OK
```

- [ ] **Step 2: Kill any running `colosseum.exe` by PID, then rebuild.** Run `cmd //c "<abs path>\native\_appbuild.bat"`; grep the log for `BUILD_OK` AND absence of `error C|error LNK` (exit codes lie — standing trap).
- [ ] **Step 3: Re-verify the shell still loads and plays** (QML + C++ changed → real binary):

```bash
COLOSSEUM_PLAYER2=1 ./native/build-msvc/colosseum.exe tests/player2/player2_play_probe.qml
# Expected: "PLAYER2 PLAY PROBE: PASS decoded=N presented=M deviceErrors=0"
```

(Full env per the Desktop launcher: Qt bin + mpvqt + libmpv + ffmpeg on PATH, `QTFRAMEWORK_BYPASS_LICENSE_CHECK=1`.)
- [ ] **Step 4: Contracts:** run `player2_shell_contract.ps1`, `player2_orchestration_contract.ps1`, `player2_integrated_contract.ps1` — all must PASS. If the shell contract objects to the re-parent, the contract wins; stop and re-read.
- [ ] **Step 5: Structural check** — exactly one top scrim: `grep -c "top scrim" qml/player2/Player2Shell.qml` → 0 (comment removed) and `grep -c "TopBar {" qml/player2/Player2Shell.qml` → 1, parented under `chrome` (verify with `grep -n -B2 "TopBar {"`).
- [ ] **Step 6: Commit both fixes, explicit pathspec:**

```bash
git add qml/player2/Player2Shell.qml native/player2/Player2Backend.h native/player2/Player2Backend.cpp
git commit -m "[Agent 4 (Claude), player] Task 17 fix: TopBar re-parented to chrome (layout bleed) + engine errors surfaced verbatim"
git push origin agent4/player2-task8-seek
```

---

### Task 1: Boot-mode coherence — a Player 2 boot has no mpv

The RHI is process-wide: a `COLOSSEUM_PLAYER2=1` boot runs D3D11, where **mpv cannot render** (its own log line: "The graphics api must be set to opengl"). Two live traps follow:

1. `handlePlayer2Fallback` flips the Loader to `PlayerPage.qml` → guaranteed black mpv. The "fallback" is a lie in this boot.
2. The ini double-gate: booting D3D11 with `player.ini` opt-in false (or missing) routes to mpv → black. The env var **is** the choice; the ini gate must die.

Also in scope: the subtitle image provider dangles if the page is ever destroyed (`addImageProvider` holds a raw session pointer) — fix lifetime while we're in the backend.

**Files:**
- Modify: `qml/Main.qml` (the Task-17 block: Settings, `usePlayer2`, both handlers, the two `player2FallbackActive = false` lines in `openMovieSession`/`openLocalVideoSession`)
- Modify: `qml/player2host/Player2Page.qml` (fallback → error surface)
- Modify: `native/player2/Player2Backend.{h,cpp}` (provider lifetime)
- Modify: `tests/player2/player2_integrated_contract.ps1` (pin the new shape)
- Modify: `C:\Users\Suprabha\Desktop\Colosseum (Player 2).bat` (comment only: ini no longer consulted)
- Post: `agents/chat.md` update (Main.qml semantics changed beyond the declared one-liner)

- [ ] **Step 1: Simplify Main.qml.** Delete the `playerBackendSettings` Settings block and `player2FallbackActive`; replace the derivation and handlers with:

```qml
// The backend is a BOOT fact: COLOSSEUM_PLAYER2 selects the D3D11 RHI in C++, and
// Player2Available reports what this process actually booted on. There is no runtime
// fallback in a Player 2 boot - mpv cannot render on D3D11 - so failures surface on the
// player page's error screen instead of swapping engines.
readonly property bool usePlayer2: Player2Available === true
```

Remove `handlePlayer2Fallback` and `handlePlayer2Restart` entirely. In the Loader `onLoaded`, replace both signal connections with log-only lambdas:

```qml
if (item.backendFallback)
    item.backendFallback.connect(function(reason, request) {
        console.warn("[player2] declined/failed before first frame: " + reason)
    })
if (item.backendRestartRequired)
    item.backendRestartRequired.connect(function(reason) {
        console.warn("[player2] failed after first frame: " + reason)
    })
```

Delete the two `win.player2FallbackActive = false` lines in `openMovieSession` / `openLocalVideoSession`.
- [ ] **Step 2: Page shows the error on BOTH failure paths.** In `Player2Page.qml`, the backend `Connections` block becomes:

```qml
Connections {
    target: backend
    function onFallbackRequested(reason) {
        // In a Player 2 boot there is nowhere to fall back TO (mpv cannot render on
        // D3D11), so a pre-first-frame failure surfaces here exactly like a late one.
        page.errorText = reason
        page.backendFallback(reason, page._lastRequest)
    }
    function onRestartRequired(reason) {
        page.errorText = reason
        page.backendRestartRequired(reason)
    }
}
```

And the `Stream` `onStreamError` handler likewise sets `page.errorText = String(message || "the stream could not be started")` before emitting.
- [ ] **Step 3: Provider lifetime.** `Player2Backend.h`: add `QPointer<QQmlEngine> m_engine;` (include `<QtQml/QQmlEngine>` moves up from the .cpp — keep the .cpp include too) and a destructor declaration `~Player2Backend() override;`. `Player2Backend.cpp`:

```cpp
Player2Backend::~Player2Backend()
{
    // The image provider holds a raw pointer to THIS object's session. If the page is ever
    // torn down while the engine lives, the provider must go with it or the next
    // image://player2subtitle request is a use-after-free.
    if (m_engine)
        m_engine->removeImageProvider(QStringLiteral("player2subtitle"));
}
```

and in `attachVideoItem`, capture the engine before installing: `m_engine = qmlEngine(this);` then use `m_engine->addImageProvider(...)`.
- [ ] **Step 4: Contract pins.** In `player2_integrated_contract.ps1`: (a) keep the `Player2Available === true` check; (b) add: Main.qml must NOT reference `playerBackendSettings` or `player2FallbackActive` (both grep → violation if found); (c) add: `qml/player2host/Player2Page.qml` must set `errorText` in both `onFallbackRequested` and `onRestartRequired` (grep each handler body). ASCII only — PS 5.1 chokes on em-dashes in BOM-less .ps1.
- [ ] **Step 5: Rebuild (`_appbuild.bat`, grep the log), run the play probe (PASS), run all four contracts (PASS).** Then a negative check: boot WITHOUT `COLOSSEUM_PLAYER2` and grep the startup line for `backend = mpv` — the stock path must be untouched.
- [ ] **Step 6: Governance.** Append to the existing Great Swap thread in Brotherhood `agents/chat.md`: Main.qml's Task-17 block simplified (Settings block + fallback handlers removed, `usePlayer2` = build/boot fact); commit + push that file alone.
- [ ] **Step 7: Commit + push** (explicit pathspec: the four repo files).

---### Task 2: Torrent seek — instrument, then fix what the evidence names

The HTTP-layer retry fix (186c4b3) passed its unit tests and the real app STILL errors on seek — so the mechanism is something that fix does not cover. Do not guess. Two named suspects, each with a coded fix; the evidence picks.

- **Suspect A — range ignored, not refused:** the sidecar answers a not-yet-downloaded offset with `200` from byte 0. `HttpMediaSource` treats `rangeStart != seekTarget` as **instant terminal Failed** (data-integrity path, no retry). My fix only covered *refused* starts.
- **Suspect B — read stall / mid-body failure at the demuxer:** the sidecar accepts the range then stalls or drops; FFmpeg's read errors bubble up as `DemuxEndReason::Failed`, and `Player2Session` treats non-device errors as terminal (`Player2Session.cpp:83-90`).

**Files:**
- Read only: `native/player/streamserver.cpp` (how ranges are actually served)
- Modify: `tests/player2/player2_http_fixture_server.ps1` (new `window*` modes)
- Modify: `native/player2/network/HttpMediaSource.cpp` (+`.h` if policy grows)
- Test: `tests/player2/player2_http_media_test.cpp`

- [ ] **Step 1: Read the sidecar's truth.** `grep -n -i "range\|206\|content-range\|partial" native/player/streamserver.cpp` and read the serving loop. Record with line citations: for a requested offset the torrent hasn't downloaded, does it (a) refuse/error, (b) serve 200-from-0, (c) accept 206 and stall until pieces arrive? This single answer picks the branch.
- [ ] **Step 2: Reproduce deterministically.** Extend the fixture server with the observed behaviour as a mode (`-Mode window -WindowBytes N`): bytes `[0,N)` served honestly; a request beyond N behaves exactly as Step 1 documented (refuse / 200-from-0 / stall-until-released). Then reproduce end-to-end with the REAL binary — start the fixture server on the Wire clip, and drive the seek probe against it via `playRemoteUrl`:

```qml
// in a copy of player2_seek_probe.qml (player2_stream_seek_probe.qml), replace playLocalFile with:
Component.onCompleted: page.playRemoteUrl({ "id": "probe:stream", "title": "stream seek probe",
                                            "streamUrl": "http://127.0.0.1:8791/media" })
```

Seek to a fraction beyond the window. Expected BEFORE the fix: session state → Error, and — thanks to Task 0 — the log now prints the engine's real reason. Save that line; it is the root-cause record.
- [ ] **Step 3a (if evidence = Suspect A): retry the mismatch.** In `HttpMediaSource.cpp`'s seek block, the `response.rangeStart != seekTarget` branch stops being instant-Failed **for servers that advertised range support at open** (capabilities.rangeSupported): close the transport and treat it exactly like a refused start — same `Buffering` state, same `maxSeekOpenAttempts` budget, same backoff, same supersede-by-newer-seek rules (it joins the existing retry loop rather than duplicating it: convert the mismatch into `continue`-with-attempt++ against the loop added in 186c4b3). A server that NEVER honoured ranges keeps the instant-fail (that is real corruption protection).
- [ ] **Step 3b (if evidence = Suspect B): classify stream read-failures as recoverable.** In `Player2Session.cpp:83-90`, when `DemuxEndReason::Failed` arrives while the request is a stream (`m_request.stream`) and the media source's network state is `Buffering`/`Recovering` (plumb the source state up through `DemuxSession` as a getter), route into the existing recovery machinery (`reopenAtSavedPosition` via `attemptDeviceRecovery`'s reopen path, bounded attempts) instead of `transition(Error)`. Bounded: exhaustion still errors.
- [ ] **Step 4: Unit test red→green.** Add the fixture-transport equivalent of the observed behaviour to `player2_http_media_test.cpp` (e.g. `honorRange=true` at open, then `serveFromZeroOnRangedStarts = K` for A). Prove it fails against the pre-fix code path (temporarily set the budget/branch back, run, FAIL) then passes. Run the full ctest player2 suite — no new failures (the pre-existing `player2_seek_generation_test` flake stands, do not chase it here).
- [ ] **Step 5: End-to-end re-run of Step 2's probe** — seek beyond the window now ends in `Playing` at the target (window released / retries land), never Error. Rebuild colosseum, rerun `player2_play_probe` + contracts.
- [ ] **Step 6: Commit + push** with the root-cause line quoted in the message.

---

### Task 3: Eyes-on integrated pass — Hemanth closes rows

Everything since the ABBA run is probe-verified only. This is the session where his eyes update the ledger's `Integrated` column from `NOT RUN`.

**Files:**
- Modify: `docs/superpowers/specs/player2-parity-ledger.md` (Integrated column, from his verdicts only)

- [ ] **Step 1: Prepare.** Kill stray `colosseum.exe`; confirm the Desktop launcher points at this worktree's fresh build; clear `player2-run.log`.
- [ ] **Step 2: Hand him this checklist** (he drives, one sitting, ~15 min — a real torrent episode, ideally the same What We Do in the Shadows season):
  1. Start playback → loading screen (backdrop + logo + status), then video.
  2. Top bar: back arrow, NOW PLAYING + title, clock, minimize, close — nothing bleeding into the bottom HUD, auto-hides with chrome.
  3. Seek forward/backward repeatedly, including far ahead → buffering, never a dead app (Task 2's proof, his eyes).
  4. Episode drawer (E): season pills, episode rows with progress, tap an episode → it plays.
  5. Sources tab: rows present; current marked.
  6. Subtitles: embedded track on/off; delay nudge.
  7. Pause card; speed 1.5× (ears: pitch); volume + mute; fullscreen toggle both directions; windowed-mode behaviour.
  8. Minimize to taskbar → reopen (warm resume). Close → confirm prompt while playing.
  9. Quit app, relaunch, same episode → resumes at position (Progress round-trip).
- [ ] **Step 3: Triage.** Anything broken that is a wiring defect gets fixed in-session (small) or becomes a named follow-up task appended to this plan (large). Layout polish → screenshot + note, batch at the end.
- [ ] **Step 4: Update the ledger's Integrated column** row by row — `PASS (eyes-on 2026-07-25)` only where he said so, verbatim quotes for exceptions. Commit + push the ledger.

---

### Task 4: Trim the CPU regression (15% vs mpv)

Prime suspect: `Player2Backend::pump()` calls `m_item->update()` at 60Hz from open to close — including while **paused**, when mpv's cost drops to ~0 but ours cannot. (The cadence itself mirrors the lab's 16ms frame timer and is the presentation driver — do NOT remove it while playing; gate it.)

**Files:**
- Modify: `native/player2/Player2Backend.cpp`

- [ ] **Step 1: Measure the baseline** (evidence, not narrative). Launch via the launcher playing the Wire clip; sample 60s of normalized CPU with the ABBA runner's counter method — once playing, once paused. Record both.
- [ ] **Step 2: Gate the pump on state:**

```cpp
void Player2Backend::pump()
{
    if (!m_item)
        return;
    const Player2State st = m_session.state();
    // The pump is the presentation driver while the picture advances, and the open-gate
    // while a request waits on the device. While PAUSED or parked (Idle/Ended/Error) there
    // is no new frame to present - repainting at 60Hz there is pure heat, and it is the
    // prime suspect in the measured 15% CPU regression vs mpv (ABBA 2026-07-25).
    const bool driving = m_hasPending
        || st == Player2State::Opening   || st == Player2State::Buffering
        || st == Player2State::Playing   || st == Player2State::Seeking
        || st == Player2State::Recovering;
    if (!driving)
        return;                      // timer stays armed; stateChanged wakes real work
    m_item->update();
    if (!m_hasPending)
        return;
    // ... (existing adapterMatch wait + open, unchanged)
}
```

One repaint on leaving the driving set so the last frame/chrome settles: in the constructor's `stateChanged` connect, add `if (m_item) m_item->update();` before the error check.
- [ ] **Step 3: Re-measure Step 1 both ways.** Expected: paused CPU collapses toward mpv's; playing CPU unchanged or better; **frame pacing unharmed** — rerun the seek soak's smoke equivalent (`player2_play_probe`, presented climbing at the same rate) and the A/V sync gate smoke (`player2_av_sync_gate.ps1`) to prove the gate didn't starve presentation.
- [ ] **Step 4: Rerun the full ABBA gate** (16 min, idle laptop, his knowledge). Update the gate report + ledger CPU row with the new number, honest either way.
- [ ] **Step 5: Commit + push.**

---

### Task 5: Chapters — labels, current chapter, menu (queued parity #2)

`session.chapters` already feeds SeekBar's ticks, so the data shape is proven on-screen; this adds the labels and the menu, matched to the current player's chapter UI.

**Files:**
- Read first: chapter UI in the production player (`grep -n -i "chapter" qml/PlayerPage.qml`) and the tick shape (`grep -n -i "chapter" qml/player2/controls/SeekBar.qml`)
- Create: `qml/player2/controls/ChapterMenu.qml`
- Modify: `qml/player2/controls/SeekBar.qml`, `qml/player2/controls/TransportBar.qml`, `qml/player2/Player2Shell.qml`

- [ ] **Step 1: Extract both shapes.** From SeekBar: the exact property names chapters carry (title/startSeconds or equivalent — cite lines). From PlayerPage: where the chapter label shows (hover? transient on change?) and how its menu lists/joins (cite lines). The next steps' bindings use THOSE names — do not invent.
- [ ] **Step 2: Hover label.** SeekBar's existing hover time tooltip gains the chapter line: nearest chapter at the hovered fraction, `title` above the timestamp, only when chapters exist. Pure binding off the already-present `chapters` list (helper in `Player2Browser.js`, unit-testable):

```js
// Player2Browser.js
function chapterAt(chapters, seconds) {
    var best = null
    for (var i = 0; i < (chapters || []).length; i++) {
        var c = chapters[i]
        if (Number(c.startSeconds) <= seconds && (!best || Number(c.startSeconds) > Number(best.startSeconds)))
            best = c
    }
    return best
}
```

(If Step 1 shows the key is not `startSeconds`, use the real key everywhere.) Add a case to the browser-logic harness (`player2_browser_logic_harness.qml`) covering: empty list → null, mid-list, before-first, after-last.
- [ ] **Step 3: ChapterMenu.qml** — clone TrackMenu's presentation (same glass, same row style): one row per chapter (`title` + formatted start), current chapter highlighted gold (derived via `chapterAt(chapters, session.position)`), click → `session.seekExact(startSeconds)` + close. Mount in the shell beside TrackMenu; trigger = a chapters button in TransportBar's right cluster, `visible: (session.chapters || []).length > 0`, icon `"episodes"` roster or the P1 glyph per Step 1's reading.
- [ ] **Step 4: Verify** — play probe on the Wire clip (it has chapters? if not, use a chaptered mkv from Downloads; record which). Contracts (shell + shortcuts drift-guard: adding a button must not break the sheet contract — if it lists buttons, update it deliberately). Browser-logic gate green.
- [ ] **Step 5: Commit + push.** Ledger chapters row → built, `Eyes-on: pending` (closes at the next eyes-on sitting).

---

### Task 6: Seek-bar buffer/cache fill (queued parity #1 — needs an engine seam)

The SeekBar rect exists; nothing feeds it. Truth lives in `HttpMediaSource` (contiguous read-ahead frontier). Local files read as fully buffered.

**Files:**
- Modify: `native/player2/network/HttpMediaSource.{h,cpp}`, `native/player2/core/DemuxSession.{h,cpp}`, `native/player2/core/Player2Session.{h,cpp}`, `qml/player2/controls/SeekBar.qml`
- Test: `tests/player2/player2_http_media_test.cpp`

- [ ] **Step 1: Source-level truth.** `HttpMediaSource` gains a thread-safe snapshot:

```cpp
// HttpMediaSource.h (public)
// Contiguous buffered frontier as a fraction of the whole resource: how far the picture
// could seek without waiting. 1.0 for a fully-read or unknown-better source; 0 when
// nothing is known. Thread-safe (mutex snapshot); cheap enough to poll at UI cadence.
double bufferedFraction() const;
```

```cpp
// HttpMediaSource.cpp
double HttpMediaSource::bufferedFraction() const
{
    std::scoped_lock lock(m_mutex);
    const qint64 total = knownSize();
    if (total <= 0)
        return 0.0;                                   // unknown length: claim nothing
    const qint64 frontier = m_fetchPosition;          // bytes contiguously fetched
    return std::clamp(static_cast<double>(frontier) / static_cast<double>(total), 0.0, 1.0);
}
```

(Adjust member names to the real ring bookkeeping read at implementation time — the frontier is fetch position; cite the line you bind to.) Unit test in `player2_http_media_test.cpp`: after draining half a known-size body, `bufferedFraction()` ≈ 0.5 (±one chunk); unknown-length → 0.
- [ ] **Step 2: Plumb up.** `DemuxSession` exposes `double sourceBufferedFraction() const` (atomic snapshot updated in its read loop, or a direct pass-through if the source pointer is stably owned — read the ownership first and follow it). `Player2Session` gains `Q_PROPERTY(double bufferedFraction READ bufferedFraction NOTIFY bufferedFractionChanged)`, refreshed wherever `positionChanged` is already emitted (same cadence, no new timer), value = 1.0 for local-file requests, else the demux passthrough.
- [ ] **Step 3: Paint it.** SeekBar's cache-fill rect binds `session.bufferedFraction` (dim white ahead of the gold progress, under the handle — mirror the production bar's layering per its source, cite lines).
- [ ] **Step 4: Verify** — unit tests green; rebuild; fixture-server `slow` mode run: the fill visibly leads the playhead and grows (his eyes at next sitting; probe asserts the property climbs: extend `player2_stream_seek_probe` to log `session.bufferedFraction` each tick and require it monotonic-ish > 0).
- [ ] **Step 5: Commit + push.** Ledger row → built, eyes-on pending.

---

### Task 7: Compact folds (queued parity #4)

**Files:**
- Read first: the production fold rules — `grep -n "tight\|snug\|tiny" qml/PlayerPage.qml` (thresholds + exactly which controls each fold hides)
- Modify: `qml/player2/controls/TransportBar.qml`

- [ ] **Step 1: Extract the production thresholds and hide-lists** (cite lines). Mirror them as `readonly property bool tight/snug/tiny` off `width` in TransportBar, then gate the same control classes P1 gates (e.g. seconds-badges drop first, then delay nudgers, then labels — per the citation, not this guess).
- [ ] **Step 2: Verify by resize** — run the app windowed (the launcher, then un-fullscreen), narrow the window through each threshold; nothing overlaps, hero cluster survives to the narrowest fold. Screenshot each fold for the eyes-on batch.
- [ ] **Step 3: Contracts (shell + shortcuts) green. Commit + push.** Ledger row → built, eyes-on pending.

---

### Task 8: Unify BarButton (recorded debt, small)

TopBar carries a duplicate of TransportBar's inline round icon button.

**Files:**
- Create: `qml/player2/controls/RoundIconButton.qml` (the TransportBar variant verbatim: icon/seconds/hero/active/size/tooltip/tapped, theme passed in as `property QtObject theme`)
- Modify: `qml/player2/controls/TransportBar.qml` (inline `component RoundButton` → the shared file; keep the local name via `RoundIconButton { ... }` at each site), `qml/player2/controls/TopBar.qml` (drop `BarButton`, use the shared one)

- [ ] **Step 1: Extract, swap both files, grep** `component RoundButton` → 0 hits and `component BarButton` → 0 hits.
- [ ] **Step 2: Play probe + all contracts green (this touches verified transport chrome — the probe is the guard).**
- [ ] **Step 3: Commit + push.**

---

### Task 9: Books under D3D11 — smoke + cross-lane flag (A2's lane)

The EPUB reader rides QtWebEngine on OpenGL. A Player 2 boot changes the RHI process-wide; whether WebEngine survives it decides whether Task 18's default flip can ever include readers.

- [ ] **Step 1: Smoke it.** Launch via the Player 2 launcher; open any epub in Biblio; capture `player2-run.log` WebEngine/GL errors and whether pages render (his eyes for one screenshot).
- [ ] **Step 2: Post to Brotherhood `agents/chat.md`** — `[Agent 4 (Claude), player] → A2`: what was observed, log excerpt, and the standing fact (D3D11 boot is opt-in; stock boots unchanged). A2 owns any fix; we own not sitting on the finding. Commit + push chat.md.
- [ ] **Step 3: Record the result in the plan's decision log and the promotion-gate report** (Task 18 precondition: books verdict known).

---

### Task 10: Long gates queue (scheduled on Hemanth's call — machine-hours)

Not run inside this plan's working session; listed so nothing silently drops:

- [ ] 2h memory soak: `powershell -NoProfile -File tests/player2/player2_memory_soak.ps1` (window VISIBLE — hidden windows never render).
- [ ] 30-min release A/V sync run: `player2_av_sync_gate.ps1` long mode on the committed binary.
- [ ] Re-baseline ABBA after Task 4 (already inside Task 4 Step 4 — cross-check it happened).
- [ ] Ledger + `player2-promotion-gate-report.md` updated from the committed artifacts; then Task 18 remains gated on his explicit go.

---

## Self-review (done at write time)

- **Coverage vs the session's open items:** layout bleed → T0; real error text → T0; boot traps/black-mpv/ini gate → T1; torrent seek → T2; eyes-on + integrated ledger → T3; CPU regression → T4; queued parity (chapters/cache-fill/folds) → T5/T6/T7; loading screen row closes via T3 (built in 0441cab); BarButton debt → T8; books/A2 → T9; soaks → T10. Screenshot/GIF, live/DVR, discrete GPU, Task 18 flip: explicitly out.
- **Placeholder scan:** T2 is deliberately evidence-gated with both branches coded; T5/T6/T7 contain read-the-source-first steps because parity means *their* shapes, not invented ones — each names the exact grep and forbids invention. No TBDs.
- **Type consistency:** `bufferedFraction` (T6) named identically at source/session/QML; `chapterAt` helper matches the tick shape extracted in T5 Step 1; `RoundIconButton` replaces both inline components (T8).

---

**Execution note:** flip back with `/model opus` before executing. Tasks 0→2 are strictly ordered; T3 wants 0–2 done; T4–T8 are independent after T3; T9 any time; T10 on his call.
