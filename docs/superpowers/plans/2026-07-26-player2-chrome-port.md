# Player 2 Chrome Port — Player 1's QML over the Player 2 Engine

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** PlayerPage.qml — the complete, battle-tested Player 1 chrome — drives the Player 2 engine through a facade that speaks mpv's interface, so every solved problem (hotkeys, focus, menus, progress writes, banner hydration, stats) is inherited rather than reimplemented; the rebuilt Player 2 shell is deleted.

**Architecture:** PlayerPage talks to one object, `id: mpv` (an MpvItem instantiated at PlayerPage.qml:2821). We replace that single declaration with `PlayerEngine.qml` — a wrapper that on an mpv boot hosts the real MpvItem and forwards 1:1, and on a Player 2 boot hosts Player2VideoItem + Player2Backend and adapts the same surface to Player2Session. The surface is finite and machine-enumerated (37 members + 12 `mpvProperty` keys, measured 2026-07-26). Two Player 2 chrome pieces survive by necessity/decree: SubtitleLayer (mpv burns subs into the frame; P2 must paint them in QML) and OverflowMenu (Hemanth's explicit exception — it replaces Player 1's context menu). Everything else in `qml/player2/` and `qml/player2host/` is deleted at the end, with its contracts retired honestly.

**Tech Stack:** Qt 6.11 QML, Player2Backend/Player2Session (C++, unchanged), MpvQt (unchanged), PowerShell contracts, the existing probe pattern (`colosseum.exe <probe.qml>` with `COLOSSEUM_PLAYER2=1`, `QSG_NO_VSYNC=1`).

**Standing rules for every task:**
- Work happens in the worktree from Task 0. Master is not touched until Hemanth's eyes-on approves both boots.
- The mpv boot must remain byte-identical in behaviour — it is his daily driver. Any task that risks it says so and gates on the mpv smoke.
- Build: `native\_reconf2.bat` (greps for `BUILD_OK`; exit codes lie). Kill running `colosseum.exe` first — but `tasklist | grep -i colosseum` BEFORE killing: other brothers run this exe.
- Probes need `COLOSSEUM_PLAYER2=1 QSG_NO_VSYNC=1 QT_FORCE_STDERR_LOGGING=1 QTFRAMEWORK_BYPASS_LICENSE_CHECK=1`.
- Verify session API names against `native/player2/core/Player2Session.h` before writing facade members — do not trust this plan's memory of them over the header.

---

## File Structure

| File | Role |
|---|---|
| `qml/PlayerEngine.qml` (create) | The facade: one component, two internal branches (mpv / Player 2), exposing the enumerated mpv surface |
| `qml/PlayerPage.qml` (modify :2821 + capability gates) | `MpvItem {` → `PlayerEngine {`; ~6 `visible:` gates on capture/live rows |
| `qml/Main.qml` (modify :2013) | The player Loader always loads `PlayerPage.qml`; the Player2Page branch dies |
| `qml/player2/controls/SubtitleLayer.qml` (keep, re-parent) | Mounted inside PlayerEngine's P2 branch, over the video item |
| `qml/player2/controls/OverflowMenu.qml` (keep) | Replaces PlayerPage's overflowPanel (both boots) — Hemanth's exception |
| `qml/player2/controls/Player2Icon.qml` (keep) | Needed by OverflowMenu |
| `tests/player2/player2_engine_facade_contract.ps1` (create) | Drift guard: every `mpv.*` member PlayerPage uses exists in PlayerEngine |
| `tests/player2/player2_facade_probe.qml` (create) | End-to-end: PlayerPage + facade on a P2 boot plays, seeks, writes progress |
| `qml/player2/` shell + `qml/player2host/` (delete at Task 9) | The rebuilt chrome and its host wiring |
| `tests/player2/player2_shell_contract.ps1`, `player2_shortcuts_contract.ps1`, `player2_browser_logic_contract.ps1` + harnesses (delete at Task 9) | Contracts that test deleted code |

Engine C++ (`native/player2/**`), engine gates (frontier seek, unit suite, subtitle schedule/timing/image) and `Colosseum (Player 2).bat` survive untouched.

---

### Task 0: The worktree

**Files:** none (git only)

- [ ] **Step 1: Create the worktree from current master**

```bash
cd "C:/Users/Suprabha/Desktop/Brotherhood/Colosseum"
git worktree add .worktrees/player2-chrome-port -b agent4/player2-chrome-port
cd .worktrees/player2-chrome-port
git log --oneline -1   # note the base SHA in the first commit message
```

- [ ] **Step 2: Configure its build ONCE (stale-cache trap: a copied cache keeps old option values)**

```bash
cd native
cmd //c "$(pwd -W)/_reconf2.bat"   # if _reconf2.bat is untracked, copy it from the main tree first
# Expected: CONFIGURED then BUILD_OK; grep CMakeCache.txt for COLOSSEUM_PLAYER2_IN_APP:BOOL=ON
```

- [ ] **Step 3: Commit a marker so the branch base is recorded**

```bash
git commit --allow-empty -m "chore(player2): chrome-port worktree opened — P1 QML over the P2 engine begins here"
```

---

### Task 1: The facade surface contract (before any facade exists)

The contract is the port's definition of done, so it is written first and it FAILS first.

**Files:**
- Create: `tests/player2/player2_engine_facade_contract.ps1`

- [ ] **Step 1: Write the failing contract**

```powershell
# player2_engine_facade_contract.ps1 — every mpv-surface member PlayerPage actually uses must be
# declared by PlayerEngine.qml. Enumerated FROM PlayerPage each run, so a new mpv.* usage in
# PlayerPage automatically fails this contract until the facade answers it. Both directions of the
# drift are fatal: a missing member is a runtime TypeError mid-playback, not a build error.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$page = Get-Content -Raw (Join-Path $root 'qml/PlayerPage.qml')
$enginePath = Join-Path $root 'qml/PlayerEngine.qml'
if (-not (Test-Path $enginePath)) { Write-Host 'FACADE CONTRACT: FAIL (PlayerEngine.qml missing)'; exit 1 }
$engine = Get-Content -Raw $enginePath

$members = [regex]::Matches($page, '\bmpv\.([A-Za-z]+)') | ForEach-Object { $_.Groups[1].Value } |
           Sort-Object -Unique | Where-Object { $_ -notmatch '^on[A-Z]' }  # signal handlers checked separately
$violations = @()
foreach ($m in $members) {
    if ($engine -notmatch "\b(property\s+\w+\s+$m|function\s+$m\s*\(|signal\s+$m\s*\()") {
        $violations += "PlayerEngine does not declare '$m' (PlayerPage uses mpv.$m)"
    }
}
# The signal handlers PlayerPage installs on the engine object must exist as signals.
foreach ($sig in @('durationChanged', 'chaptersChanged')) {
    if ($engine -notmatch "\b$sig\b") { $violations += "PlayerEngine must expose '$sig'" }
}
# The property keys statsValue queries must be answered by the facade's mpvProperty switch.
foreach ($k in @('video-bitrate','audio-bitrate','frame-drop-count','vo-drop-frame-count',
                 'estimated-vf-fps','container-fps','video-codec','audio-codec','hwdec-current',
                 'cache-buffering-state','width','height')) {
    if ($engine -notmatch [regex]::Escape('"' + $k + '"')) {
        $violations += "PlayerEngine's mpvProperty() does not handle '$k'"
    }
}
if ($violations.Count) { $violations | ForEach-Object { Write-Error $_ -ErrorAction Continue }
    Write-Host "FACADE CONTRACT: FAIL ($($violations.Count))"; exit 1 }
Write-Host 'FACADE CONTRACT: PASS'
```

- [ ] **Step 2: Run it, expect FAIL (PlayerEngine.qml missing)**

```bash
powershell -NoProfile -File tests/player2/player2_engine_facade_contract.ps1
# Expected: FACADE CONTRACT: FAIL (PlayerEngine.qml missing)
```

- [ ] **Step 3: Commit**

```bash
git add tests/player2/player2_engine_facade_contract.ps1
git commit -m "test(player2): facade surface contract — the port's definition of done, failing first"
```

---

### Task 2: PlayerEngine.qml, mpv branch — the zero-behaviour-change wrapper

The riskiest constraint is that HIS daily driver must not change. So the mpv branch is written first, and the smoke for this task is the OLD player working through the wrapper.

**Files:**
- Create: `qml/PlayerEngine.qml`
- Modify: `qml/PlayerPage.qml:2821` (one token: `MpvItem` → `PlayerEngine`; the `id: mpv` and every binding inside the block stay)

- [ ] **Step 1: Write PlayerEngine.qml with the mpv branch and the full forwarded surface**

The component instantiates exactly one inner engine via a Loader keyed on the boot fact, and forwards the enumerated surface. Forwarding is mechanical; write every member, no summarising. Shape:

```qml
import QtQuick
import Colosseum 1.0   // MpvItem's module — match PlayerPage's current import block exactly

// PlayerEngine — the ONE object PlayerPage talks to (`id: mpv` at its instantiation site).
// mpv boot: hosts the real MpvItem and forwards 1:1 (this branch must be behaviourally invisible).
// Player 2 boot: hosts Player2Backend + Player2VideoItem and adapts the same surface (Task 3+).
// The surface is pinned by tests/player2/player2_engine_facade_contract.ps1 — grow it there first.
Item {
    id: engine

    readonly property bool p2: (typeof Player2Available !== "undefined") && Player2Available === true
    readonly property var inner: engineLoader.item

    Loader {
        id: engineLoader
        anchors.fill: parent
        source: engine.p2 ? "PlayerEngineP2.qml" : "PlayerEngineMpv.qml"
    }

    // ---- state (read) — forwarded with safe defaults so bindings never see undefined ----
    readonly property real duration: inner ? inner.duration : 0
    readonly property real position: inner ? inner.position : 0
    readonly property bool pause: inner ? inner.pause : true
    readonly property real speed: inner ? inner.speed : 1
    readonly property real volume: inner ? inner.volume : 100
    readonly property bool mute: inner ? inner.mute : false
    readonly property bool coreSeeking: inner ? inner.coreSeeking : false
    readonly property real cacheTime: inner ? inner.cacheTime : 0
    readonly property string mediaTitle: inner ? inner.mediaTitle : ""
    readonly property string currentUrl: inner ? inner.currentUrl : ""
    readonly property var chapters: inner ? inner.chapters : []
    readonly property var audioTracks: inner ? inner.audioTracks : []
    readonly property var subtitleTracks: inner ? inner.subtitleTracks : []
    readonly property int audioTrack: inner ? inner.audioTrack : -1
    readonly property int subtitleTrack: inner ? inner.subtitleTrack : -1
    readonly property real subDelay: inner ? inner.subDelay : 0
    readonly property real audioDelay: inner ? inner.audioDelay : 0
    readonly property real videoZoom: inner ? inner.videoZoom : 0
    readonly property string videoAspect: inner ? inner.videoAspect : ""
    readonly property real panscan: inner ? inner.panscan : 0

    // ---- capability flags (Task 6 gates PlayerPage's capture/live rows on these) ----
    readonly property bool supportsCapture: !engine.p2   // screenshot / GIF / frame grab
    readonly property bool supportsLive: !engine.p2      // live guide / DVR panels

    // ---- commands (write) — one forwarding function per member PlayerPage calls ----
    function loadFile(url, opts) { if (inner) inner.loadFile(url, opts) }
    function setPause(v) { if (inner) inner.pause = v }   // see Step 2 note on pause writes
    function seekExact(s) { if (inner) inner.seekExact(s) }
    function seekStep(s) { if (inner) inner.seekStep(s) }
    function frameStep() { if (inner) inner.frameStep() }
    function frameBackStep() { if (inner) inner.frameBackStep() }
    function setAudioNormalization(m) { if (inner) inner.setAudioNormalization(m) }
    function addSubtitle(path) { if (inner) inner.addSubtitle(path) }
    function command(args) { if (inner && inner.command) inner.command(args) }
    function mpvProperty(name) { return inner && inner.mpvProperty ? inner.mpvProperty(name) : "" }
    function captureFrame() { if (inner && inner.captureFrame) inner.captureFrame() }
    function startGifRecording() { if (inner && inner.startGifRecording) inner.startGifRecording() }
    function stopGifRecording() { if (inner && inner.stopGifRecording) inner.stopGifRecording() }
    function abortGifRecording() { if (inner && inner.abortGifRecording) inner.abortGifRecording() }
    function revealCaptureFolder() { if (inner && inner.revealCaptureFolder) inner.revealCaptureFolder() }
}
```

`PlayerEngineMpv.qml` is the current `MpvItem` block's element with plain `MpvItem { anchors.fill: parent }` — the properties PlayerPage set ON mpv at :2821-… move with the instantiation (read them from the current block and carry every one).

**IMPORTANT — writable properties:** PlayerPage assigns `mpv.pause = x`, `mpv.speed = x`, `mpv.volume = x`, `mpv.mute = x`, `mpv.subtitleTrack = x`, `mpv.audioTrack = x`, `mpv.subDelay/audioDelay/videoZoom/videoAspect/panscan = x` directly. `readonly` forwarding breaks those. For each ASSIGNED member, use a non-readonly property with a two-way relay:

```qml
property real speed: 1
onSpeedChanged: if (inner && inner.speed !== speed) inner.speed = speed
Connections { target: inner; function onSpeedChanged() { if (engine.speed !== inner.speed) engine.speed = inner.speed } }
```

Enumerate which members are assigned before writing: `grep -nE "mpv\.[a-zA-Z]+ =" qml/PlayerPage.qml` — every hit needs the two-way form; pure reads keep `readonly`.

- [ ] **Step 2: Swap the instantiation site**

At `qml/PlayerPage.qml:2821`: `MpvItem {` → `PlayerEngine {`. Nothing else in the block changes. If the block sets MpvItem-construction properties, move them into `PlayerEngineMpv.qml`.

- [ ] **Step 3: Contract + build + the mpv smoke (his daily driver, through the wrapper)**

```bash
powershell -NoProfile -File tests/player2/player2_engine_facade_contract.ps1   # Expected: PASS
cd native && cmd //c "$(pwd -W)/_reconf2.bat"                                  # Expected: BUILD_OK
cd .. && ./native/build-msvc/colosseum.exe qml/Main.qml
# Eyes: open ANY video on the DEFAULT (mpv) boot. Play, pause with SPACE, seek, volume wheel,
# subtitle menu, stats (D), close. Log line must read "startup backend = mpv (player 1)".
```

- [ ] **Step 4: Commit**

```bash
git add qml/PlayerEngine.qml qml/PlayerEngineMpv.qml qml/PlayerPage.qml
git commit -m "feat(player2): PlayerEngine facade — mpv branch forwards 1:1, PlayerPage swaps one token"
```

---

### Task 3: The Player 2 branch — core transport

**Files:**
- Create: `qml/PlayerEngineP2.qml`

- [ ] **Step 1: Write the P2 inner engine (core transport only this task)**

```qml
import QtQuick
import Colosseum.Player2 1.0

// The Player 2 side of PlayerEngine: Player2Backend + video surface, answering mpv's surface shape.
// Verify EVERY session member name against native/player2/core/Player2Session.h before trusting
// this file's memory of them.
Item {
    id: p2
    Player2Backend { id: backend }
    Player2VideoItem { id: surface; anchors.fill: parent }

    readonly property var s: backend.session
    readonly property real duration: s ? s.duration : 0
    readonly property real position: s ? s.position : 0
    readonly property real cacheTime: s && s.bufferedSeconds >= 0 ? s.bufferedSeconds : 0
    readonly property bool coreSeeking: s ? s.state === 5 : false          // Player2State::Seeking
    property bool pause: false
    onPauseChanged: { if (!s) return; if (pause) s.pause(); else s.play() }
    Connections {
        target: p2.s
        function onStateChanged() {
            var paused = p2.s.state === 4                                   // Player2State::Paused
            if (p2.pause !== paused) p2.pause = paused
        }
    }
    property real speed: 1
    onSpeedChanged: if (s) s.setSpeed(speed)          // verify: Player2Session setter name
    property real volume: 100
    onVolumeChanged: if (s) s.setVolume(volume / 100) // session is 0..1; mpv surface is 0..100
    property bool mute: false
    onMuteChanged: if (s) s.setMuted(mute)

    property string mediaTitle: ""
    property string currentUrl: ""
    function loadFile(url, opts) {
        p2.currentUrl = String(url)
        backend.play({ "url": String(url) })          // verify against Player2Backend::play's map keys
    }
    function seekExact(sec) { if (s) s.seekExact(sec) }
    function seekStep(sec) { if (s) s.seekExact(Math.max(0, s.position + sec)) }
    function frameStep() { if (s) s.frameStep(1) }
    function frameBackStep() { if (s) s.frameStep(-1) }
}
```

- [ ] **Step 2: Probe — PlayerPage drives a real file on a P2 boot**

Create `tests/player2/player2_facade_probe.qml` (pattern of the existing probes): a Window hosting **PlayerPage** (not the facade directly), `playLocalFile` on the av.mkv fixture, then assert via the facade: position advances; `seekExact(2)` lands; pause freezes position. Exit non-zero on any failure — a probe that cannot fail is decoration.

```bash
COLOSSEUM_PLAYER2=1 QSG_NO_VSYNC=1 ./native/build-msvc/colosseum.exe tests/player2/player2_facade_probe.qml \
  ".worktrees/player2-task1-isolation/native/build-player2/player2-fixtures/av.mkv"
# NOTE: if this probe does not reach Playing, there is a KNOWN unexplained main-tree probe failure
# (2026-07-26, A/B-cleared of relation to the pump trim). Diagnose it HERE — it blocks this plan's
# verification spine and was never root-caused.
```

- [ ] **Step 3: Commit**

```bash
git add qml/PlayerEngineP2.qml tests/player2/player2_facade_probe.qml
git commit -m "feat(player2): facade P2 branch — core transport under PlayerPage's own chrome"
```

---

### Task 4: Tracks, subtitles, delays — and the SubtitleLayer mount

**Files:**
- Modify: `qml/PlayerEngineP2.qml`

- [ ] **Step 1: Add the track surface**

Map `audioTracks`/`subtitleTracks` from `s.tracks` (filter by `type`), into the ROW SHAPE PlayerPage's menus read — enumerate that shape first from PlayerPage's `audioRows`/`subRows` usage (`grep -n "audioRows\|subRows" qml/PlayerPage.qml`), then transform. `audioTrack`/`subtitleTrack` are assigned by PlayerPage: on change call `s.selectAudioTrack(String(v))` / `s.selectSubtitleTrack(String(v))`. `subDelay`/`audioDelay` assigned: `s.setSubDelay(v)` / `s.setAudioDelay(v)` (verify names in the header). `addSubtitle(path)`: if the session has no external-subtitle seam yet, the function must set a `supportsExternalSubs: false` capability and PlayerPage's row gates on it (Task 6 pattern) — no silent no-op.

- [ ] **Step 2: Mount SubtitleLayer inside the P2 branch — the one P2 chrome piece that must survive**

```qml
    // mpv burns subtitles into the frame; Player 2 hands cues to QML. This layer IS the subtitle
    // renderer on a P2 boot, mounted over the video item so cues sit on the letterboxed picture.
    SubtitleLayer { anchors.fill: surface; session: p2.s }
```
(Adjust import to reach `qml/player2/controls/SubtitleLayer.qml` — it stays where it is until Task 9 moves it next to PlayerEngine.)

- [ ] **Step 3: Probe extension: enable the fixture's subrip track through PlayerPage's own subtitle menu path (`mpv.subtitleTrack = 2`), assert `s.subtitleText` becomes non-empty within the cue window and empty after it. Run. Commit.**

```bash
git add qml/PlayerEngineP2.qml tests/player2/player2_facade_probe.qml
git commit -m "feat(player2): facade tracks/subtitles — SubtitleLayer rides inside the engine"
```

---

### Task 5: `mpvProperty()` — the stats card's twelve questions

**Files:**
- Modify: `qml/PlayerEngineP2.qml`

- [ ] **Step 1: Implement the map**

```qml
    function mpvProperty(name) {
        var d = s && s.diagnostics ? s.diagnostics() : ({})
        switch (String(name)) {
        case "video-codec":  return d.videoCodec || ""
        case "hwdec-current": return d.hardwareFormat || ""
        case "frame-drop-count": return Number(d.dropped || 0)
        case "vo-drop-frame-count": return Number(d.scheduledLateDrops || 0)
        case "width": case "height": {
            var m = String(d.inputFormat || "").match(/(\d{2,5})\s*x\s*(\d{2,5})/)
            return m ? Number(name === "width" ? m[1] : m[2]) : ""
        }
        case "audio-codec": {
            var rows = s ? s.tracks : []
            for (var i = 0; i < rows.length; i++)
                if (rows[i].type === "audio") return rows[i].codec || ""
            return ""
        }
        // Honest absences until the engine grows the seams (recorded follow-ups, not fakes):
        case "container-fps": case "estimated-vf-fps":
        case "video-bitrate": case "audio-bitrate":
        case "cache-buffering-state":
            return ""
        }
        return ""
    }
```

The five empty keys are the SAME five dead rows from Hemanth's screenshot. Each becomes an engine seam ticket in Task 10's follow-up list — fps and observed bitrates are countable in DemuxSession; do not fake them here.

- [ ] **Step 2: Contract run (the 12-key check goes green), build, commit.**

```bash
powershell -NoProfile -File tests/player2/player2_engine_facade_contract.ps1   # Expected: PASS
git add qml/PlayerEngineP2.qml && git commit -m "feat(player2): facade answers the stats card honestly"
```

---

### Task 6: Capability gates — no control may lie

**Files:**
- Modify: `qml/PlayerPage.qml` (the ONLY edits beyond the Task 2 token swap)

- [ ] **Step 1: Enumerate every mpv-only control, then gate each `visible:` on the facade's capability flags**

```bash
grep -nE "captureFrame|GifRecording|revealCaptureFolder|LiveGuide|DvrPanel|abLoop" qml/PlayerPage.qml
```
For each surfaced control/row (screenshot button, GIF button, overflow rows, live/DVR panels, A-B loop if it drives mpv.command): append `&& mpv.supportsCapture` (or `mpv.supportsLive`) to its `visible:`. On the mpv boot the flags are true — zero change. On a P2 boot the controls are absent, not lying.

- [ ] **Step 2: Eyes-check both boots (mpv: everything present; P2: capture/live rows gone). Commit.**

```bash
git add qml/PlayerPage.qml && git commit -m "feat(player2): capability gates — absent, never lying"
```

---

### Task 7: Main.qml routes both boots to PlayerPage — and progress is inherited

**Files:**
- Modify: `qml/Main.qml:2013` (the Loader source line)
- Modify: `tests/player2/player2_integrated_contract.ps1` (repin: the check demanding the Player2Page branch inverts)

- [ ] **Step 1: The Loader always loads PlayerPage**

```qml
source: "PlayerPage.qml"   // both boots; PlayerEngine inside it picks the engine off Player2Available
```
Delete the `win.usePlayer2 ?` branch. Keep the boot log line (reworded: backend now reported by the facade).

- [ ] **Step 2: Repin the integrated contract** — it currently REQUIRES the one-line branch; change that check to require the single-source form and that `PlayerEngine` exists. Run it. Expected: PASS.

- [ ] **Step 3: THE PROGRESS TEST — the reason this port exists.** Probe: on a P2 boot, PlayerPage plays the fixture for 15s (past the 10s anti-clutter floor), then quit; assert `Progress.get("video", <id>)` returns a record with `position > 10`. This is PlayerPage's OWN `Progress.record` path running over the new engine — inheritance, not reimplementation. Run on BOTH boots.

- [ ] **Step 4: Commit**

```bash
git add qml/Main.qml tests/player2/player2_integrated_contract.ps1 tests/player2/player2_facade_probe.qml
git commit -m "feat(player2): one PlayerPage, two engines — progress tracking inherited, proven"
```

---

### Task 8: The context-menu exception

**Files:**
- Modify: `qml/PlayerPage.qml` (overflowPanel → OverflowMenu)
- Modify: `qml/player2/controls/OverflowMenu.qml` (speak PlayerPage's action vocabulary)

- [ ] **Step 1: Inventory PlayerPage's overflowPanel rows (`grep -n "overflowPanel" -A 40 qml/PlayerPage.qml`) — every row it offers, OverflowMenu must offer, including capture rows gated `visible: mpv.supportsCapture`.**
- [ ] **Step 2: Replace the panel: right-click raises OverflowMenu at the cursor; rows call the SAME PlayerPage functions the old rows called. Keep OverflowMenu's look — that is the point of the exception.**
- [ ] **Step 3: Eyes on both boots (mpv boot gets the new menu too — his stated wish: "replace player 1's context menu"). Commit.**

```bash
git add qml/PlayerPage.qml qml/player2/controls/OverflowMenu.qml
git commit -m "feat(player2): P2's context menu replaces the old panel on both boots — his exception, kept"
```

---

### Task 9: Teardown — isolation dies at the swap

**Files:**
- Move: `qml/player2/controls/SubtitleLayer.qml`, `OverflowMenu.qml`, `Player2Icon.qml`, `Player2Browser.js` → `qml/` (next to PlayerEngine; update imports)
- Delete: the rest of `qml/player2/`, all of `qml/player2host/`
- Delete: `tests/player2/player2_shell_contract.ps1`, `player2_shortcuts_contract.ps1`, `player2_shortcuts_harness.qml`, `player2_browser_logic_contract.ps1`, `player2_browser_logic_harness.qml`, and every probe that instantiates the deleted shell (enumerate: `grep -rln "player2host\|Player2Shell" tests/`)
- Modify: `native/CMakeLists.txt` qrc lists that bundle deleted QML (grep-verify: shared file — check `tasklist` and announce in `agents/chat.md` if other brothers are active)

- [ ] **Step 1: Move the four survivors; fix their imports; build.**
- [ ] **Step 2: Delete the shell + host + retired contracts in ONE commit whose message lists every deleted file — the honest tombstone.**
- [ ] **Step 3: Full remaining gate suite: facade contract, integrated contract, orchestration contract (now trivially clean), engine unit suite, frontier gate. All green.**

```bash
git add -A && git commit -m "chore(player2): delete the rebuilt shell — the lab's scaffolding, retired with its contracts"
```

---

### Task 10: Stability arc — the engine work the chrome was hiding

Only after the port, because every measurement before it confounds engine cost with shell cost.

- [ ] **Step 1: ABBA re-run** (`tests/player2/player2_efficiency_abba.ps1`), both engines through the SAME PlayerPage. Record GPU and CPU against the 2026-07-25 numbers (GPU 21.0% vs 57.7%; CPU 17.9% vs 15.6%). The pump trim (`c73695c`) should move CPU — state the new number either way.
- [ ] **Step 2: Seek-stutter measurement**: extend the frontier probe to record `dropped` deltas across 10 seeks; acceptance = post-seek recovery without a visible stall (his eyes on Tenet's opera scene is the real gate; the counter delta is the regression guard).
- [ ] **Step 3: Engine seams for the five dead stats rows** (fps + observed bitrates in DemuxSession; buffering % from ring fill) — each lands with its facade key flipping from `""` to a real value and the row coming alive.
- [ ] **Step 4: Merge = Hemanth's evening test.** Both boots, same film, stats card closed: if he cannot tell which player he is on until he opens the card, Task 18 (the default flip) may be proposed again. Not before.

---

## Self-Review (run before handoff)

- Spec coverage: worktree ✓(T0) · P1 QML + progress ✓(T2-T7, progress explicitly T7) · frame drops/seek/stability ✓(T10) · context-menu exception ✓(T8) · subtitle survival ✓(T4) · capability dispositions ✓(T6) · teardown + contract retirement ✓(T9).
- Known open risk carried INTO the plan deliberately: the main-tree probe failure (Task 3 Step 2 note) — never root-caused, blocks the verification spine if it recurs; first task to hit it must stop and diagnose, not route around.
- Names flagged `verify:` in code blocks (setSpeed, setSubDelay, backend.play keys) MUST be checked against the headers before use — the plan's memory of the session API is not authority.
