# Colosseum Home — AF2 Redesign Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:subagent-driven-development` (recommended) or `superpowers:executing-plans` to implement this plan task-by-task with review checkpoints. Steps use checkbox (`- [ ]`) syntax.

**Goal:** Replace the current static Home body with an AF2-anchored front door: a featured **spotlight that recedes** as a **frosted glass board** rises over the living wallpaper, carrying **all three worlds** (Continue spans them + a rail each) on AF2's `view_row` cadence.

**Architecture:** New focused QML components (`HomeWorldsBar`, `HomeSpotlight`, `HomeRail`, `HomeCard`) composed into the existing Home surface in `Main.qml`. Reuse existing stores/components (`Progress.recent()` for Continue, `Catalog.*` for per-world featured, `ContinueTile`) — no C++ or backend change. The recede is scroll-driven QML transforms, reduced-motion-aware.

**Tech Stack:** Qt6 QML/QtQuick, QtQuick.Effects (glass/blur already used), PowerShell P0 grep-contract tests (house style: shape not behavior), Figtree font (OFL). Verification = grep contracts + `native/build-msvc` build + Hemanth eyes-on (Qt/D3D is uncapturable; pixels are his eyes).

**Design source of truth:** `docs/superpowers/specs/2026-07-19-colosseum-home-af2-redesign-design.md` and the approved mock `agents/colosseum-home-af2-audit-mock.html`. Read both before starting.

## Global Constraints

- Work in `C:\Users\Suprabha\Desktop\Brotherhood\Colosseum` (nested git repo — `cd` in before git).
- Commit by explicit pathspec; another brother (A4) may be live in the tree. Never `git add -A`. Push after each green task.
- Composition/cadence/motion only — never AF2 assets, never TMDbHelper/fanart.tv. Art from our lanes.
- Glass over wallpaper: the board stays translucent (art visible behind). Gold only on progress + the primary action.
- QML paints, C++ decides: no raw network on the GUI thread; reuse existing stores.

## File Structure

- Modify `qml/Theme.qml` — add cadence tokens (`homePad`, `rowH`) + a sans display token (`displaySans`).
- Modify `qml/Main.qml` — bundle+load Figtree; replace the Home body (spotlight + glass board + rails + recede).
- Create `qml/HomeWorldsBar.qml` — glass top menu (Home·Tankoban·Theatre·Biblio), recede input.
- Create `qml/HomeSpotlight.qml` — featured hero (metahub logo + fallback, fact line, actions, resume), recede input.
- Create `qml/HomeRail.qml` — reusable section (header + horizontal track) on the cadence.
- Create `qml/HomeCard.qml` — one glass card with three shapes: `landscape` / `portrait` / `jacket`.
- Create `assets/fonts/Figtree-*.ttf` — the OFL display sans.
- Create `tests/test_home_af2_shell_p0.ps1`, `tests/test_home_af2_recede_p0.ps1`, `tests/test_home_af2_worlds_p0.ps1`.

---

### Task 1: Theme cadence tokens + Figtree display font

**Files:**
- Modify: `qml/Theme.qml`
- Create: `assets/fonts/Figtree-Regular.ttf`, `Figtree-SemiBold.ttf`, `Figtree-Bold.ttf`, `Figtree-ExtraBold.ttf` (OFL)
- Modify: `qml/Main.qml` (FontLoader registration)
- Create: `tests/test_home_af2_shell_p0.ps1`

- [ ] **Step 1: Write the failing shell contract test**

Create `tests/test_home_af2_shell_p0.ps1`:
```powershell
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$theme = Get-Content (Join-Path $root "qml/Theme.qml") -Raw
function Has($t,$n,$m){ if($t -notlike "*$n*"){throw $m} }
Has $theme 'readonly property int homePad' "Theme must expose the AF2 page gutter token (homePad)."
Has $theme 'readonly property int rowH'    "Theme must expose the AF2 view_row rail cadence token (rowH)."
Has $theme 'readonly property string displaySans' "Theme must expose a sans display face token (displaySans)."
Write-Host "home AF2 shell tokens ok"
```

- [ ] **Step 2: Run it, verify RED**

Run: `powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests/test_home_af2_shell_p0.ps1`
Expected: throws "Theme must expose ... homePad".

- [ ] **Step 3: Download Figtree (OFL) into assets/fonts**

Run (fetches the static weights from Google Fonts' repo):
```bash
cd ~/Desktop/Brotherhood/Colosseum
for w in Regular SemiBold Bold ExtraBold; do
  curl -sL "https://github.com/google/fonts/raw/main/ofl/figtree/static/Figtree-$w.ttf" -o "assets/fonts/Figtree-$w.ttf"
done
ls -la assets/fonts/Figtree-*.ttf   # 4 files, non-zero
```

- [ ] **Step 4: Register Figtree in Main.qml and add Theme tokens**

In `qml/Main.qml`, beside the existing Fraunces `FontLoader`s, add:
```qml
FontLoader { source: Qt.resolvedUrl("../assets/fonts/Figtree-Regular.ttf") }
FontLoader { source: Qt.resolvedUrl("../assets/fonts/Figtree-SemiBold.ttf") }
FontLoader { source: Qt.resolvedUrl("../assets/fonts/Figtree-Bold.ttf") }
FontLoader { source: Qt.resolvedUrl("../assets/fonts/Figtree-ExtraBold.ttf") }
```
In `qml/Theme.qml`, after `margin`:
```qml
    // AF2 cadence (Includes_Constants.xml, scaled from the skin's 1080 grid to our canvas)
    readonly property int homePad: 64          // view_pad=80
    readonly property int rowH: 300            // view_row=510 -> landscape rail height
    // sans display face for the AF2 Home headers/hero (Figtree, OFL — AF2 ships it)
    readonly property string displaySans: "Figtree"
```

- [ ] **Step 5: Verify GREEN + build + commit**

Run the test (Step 2 command) → prints "home AF2 shell tokens ok".
Run: `cmake --build native/build-msvc --target colosseum` (or `native/build-msvc.bat`) → exit 0, boot smoke shows Figtree registers (no font warning for "Figtree").
```bash
git add -- qml/Theme.qml qml/Main.qml assets/fonts/Figtree-Regular.ttf assets/fonts/Figtree-SemiBold.ttf assets/fonts/Figtree-Bold.ttf assets/fonts/Figtree-ExtraBold.ttf tests/test_home_af2_shell_p0.ps1
git commit -m "[Agent 4 (Claude), player] feat(home): AF2 cadence tokens + Figtree display font"
git push origin master
```

---

### Task 2: HomeWorldsBar — the glass top menu that recedes

**Files:**
- Create: `qml/HomeWorldsBar.qml`
- Create: `tests/test_home_af2_worlds_p0.ps1`

- [ ] **Step 1: Failing contract test**

Create `tests/test_home_af2_worlds_p0.ps1`:
```powershell
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$b = Get-Content (Join-Path $root "qml/HomeWorldsBar.qml") -Raw
function Has($t,$n,$m){ if($t -notlike "*$n*"){throw $m} }
Has $b 'property real recede'  "WorldsBar must take a 0..1 recede input (fades/lifts on scroll)."
Has $b 'backdrop-filter'.Replace('backdrop-filter','layer.enabled') "WorldsBar must be frosted glass (MultiEffect blur behind)."
Has $b 'signal worldPicked'   "WorldsBar must emit worldPicked(string world) for routing."
foreach($w in @('Home','Tankoban','Theatre','Biblio')){ Has $b "`"$w`"" "WorldsBar must list the world: $w" }
Has $b 'signal searchRequested' "WorldsBar must expose search."
Write-Host "home worlds bar ok"
```

- [ ] **Step 2: Run, verify RED** (missing file). Run: `powershell.exe ... -File tests/test_home_af2_worlds_p0.ps1`.

- [ ] **Step 3: Implement `qml/HomeWorldsBar.qml`**

A glass pill bar. Public API: `property real recede: 0`, `property string activeWorld: "Home"`, `signal worldPicked(string world)`, `signal searchRequested()`, `signal wallpaperRequested()`. Frosted glass background via a `Rectangle` + `MultiEffect` blur of the wallpaper behind (mirror the player top-scrim glass pattern), with `theme.edge` hairline. Row of world pills (`Home Tankoban Theatre Biblio`), the active one filled with `theme.ink` (dark text), the rest `theme.inkDim`. Right group: search + wallpaper `RoundButton`-style glass icons. Bind `opacity: 1 - Math.min(1, recede*1.15)` and `y`-lift `-recede*10`. Follow the mock's `.topbar` composition exactly (spacing, radius 16, padding). Text uses `theme.displaySans`.

- [ ] **Step 4: Verify GREEN + commit**

Run the test → "home worlds bar ok".
```bash
git add -- qml/HomeWorldsBar.qml tests/test_home_af2_worlds_p0.ps1
git commit -m "[Agent 4 (Claude), player] feat(home): glass worlds top-bar (recedes)"
git push origin master
```

---

### Task 3: HomeSpotlight — the featured hero that recedes

**Files:**
- Create: `qml/HomeSpotlight.qml`
- Modify: `tests/test_home_af2_recede_p0.ps1` (created here)

- [ ] **Step 1: Failing contract test**

Create `tests/test_home_af2_recede_p0.ps1`:
```powershell
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$s = Get-Content (Join-Path $root "qml/HomeSpotlight.qml") -Raw
function Has($t,$n,$m){ if($t -notlike "*$n*"){throw $m} }
Has $s 'property real recede'  "Spotlight must take a 0..1 recede input."
Has $s 'property url logoUrl'  "Spotlight must show the metahub-derived logo."
Has $s 'root.title'            "Spotlight must fall back to a text title when no logo."
Has $s 'signal primaryRequested'   "Spotlight must emit the primary action (Play/Watch)."
Has $s 'signal secondaryRequested' "Spotlight must emit a world-appropriate secondary (Read/Details)."
Has $s 'theme.gold'           "Resume progress must use gold."
Has $s 'translateY'.Replace('translateY','transform') "Spotlight must lift on recede."
Write-Host "home spotlight ok"
```

- [ ] **Step 2: Run, verify RED.**

- [ ] **Step 3: Implement `qml/HomeSpotlight.qml`**

Public API: `property real recede: 0`, `property string title`, `property url logoUrl`, `property string factLine`, `property string blurb`, `property string primaryLabel: "Watch"`, `property bool hasSecondary: false`, `property string secondaryLabel: "Read"`, `property real resumeFraction: 0`, `property string resumeLabel`, `signal primaryRequested()`, `signal secondaryRequested()`, `signal detailsRequested()`. Compose: eyebrow (`theme.displaySans`, letter-spaced), an `Image` logo (`logoUrl`, async, `PreserveAspectFit`, drop-shadow via `MultiEffect`) with a text-title fallback (`visible: logo.status !== Image.Ready`), fact line, blurb, an actions Row (white primary `Play` button + glass ghost secondary/details), and a resume line with a gold progress track (`visible: resumeFraction > 0`). Bind `opacity: 1 - Math.min(1, recede*1.35)` and `transform: Translate { y: -recede*170 }`. Match the mock's `.spotlight` block. Logo URL is passed in by Main (built from the imdb id via metahub — the existing player pattern).

- [ ] **Step 4: Verify GREEN + commit**

```bash
git add -- qml/HomeSpotlight.qml tests/test_home_af2_recede_p0.ps1
git commit -m "[Agent 4 (Claude), player] feat(home): featured spotlight hero (recedes)"
git push origin master
```

---

### Task 4: HomeRail + HomeCard — reusable rail and the three card shapes

**Files:**
- Create: `qml/HomeRail.qml`, `qml/HomeCard.qml`
- Modify: `tests/test_home_af2_shell_p0.ps1` (extend)

- [ ] **Step 1: Extend the shell test with rail/card contracts**

Append to `tests/test_home_af2_shell_p0.ps1`:
```powershell
$rail = Get-Content (Join-Path $root "qml/HomeRail.qml") -Raw
$card = Get-Content (Join-Path $root "qml/HomeCard.qml") -Raw
Has $rail 'property string worldTag' "Rail must carry an optional world tag."
Has $rail 'theme.rowH'               "Rail must obey the AF2 view_row cadence (theme.rowH)."
Has $rail 'theme.homePad'            "Rail must use the AF2 page gutter (theme.homePad)."
Has $card 'shape'                    "Card must switch shape (landscape/portrait/jacket)."
foreach($sh in @('landscape','portrait','jacket')){ Has $card "`"$sh`"" "Card must support shape: $sh" }
Has $card 'theme.gold'               "Card progress bar must be gold."
Write-Host "home rail+card ok"
```

- [ ] **Step 2: Run the shell test, verify RED on the new rail/card lines.**

- [ ] **Step 3: Implement `qml/HomeCard.qml`**

Public API: `property string shape: "landscape"`, `property url art`, `property string worldTag: ""`, `property string titleText`, `property string subText`, `property real progress: 0`, `property string jacketTitle`, `property string jacketAuthor`, `property color jacketColor: "#2b2350"`, `signal activated()`. One glass frame (`theme.glassTint` fill, `theme.edge` border, soft shadow, hover-lift). `landscape` = 296×167 art; `portrait` = 146×216 art; `jacket` = 146×216 typographic panel (no art) with `jacketTitle`/`jacketAuthor` over `jacketColor` (Biblio). Optional world-tag pill top-left, gold progress bar bottom when `progress>0`. Sizes/composition from the mock's `.card`/`.thumb`/`.jacket`.

- [ ] **Step 4: Implement `qml/HomeRail.qml`**

Public API: `property string worldTag: ""`, `property string railTitle`, `property var model: []`, `property string cardShape: "landscape"`, `signal seeAll()`, `signal cardActivated(int index)`. A header Row (`homePad` gutters: optional colored world tag + `railTitle` in `displaySans` + "See all") over a horizontal `ListView`/`Flickable` of `HomeCard` on the `rowH` cadence, 15px gaps. Reuse `WidgetHeader` if it already matches; otherwise a thin local header.

- [ ] **Step 5: Verify GREEN + commit**

```bash
git add -- qml/HomeRail.qml qml/HomeCard.qml tests/test_home_af2_shell_p0.ps1
git commit -m "[Agent 4 (Claude), player] feat(home): reusable rail + 3-shape glass card"
git push origin master
```

---

### Task 5: Compose the Home body — glass board, worlds content, recede wiring

**Files:**
- Modify: `qml/Main.qml` (the Home surface body)
- Modify: `tests/test_home_af2_recede_p0.ps1` (extend for the Main wiring)

- [ ] **Step 1: Extend the recede test with the Main-side wiring contract**

Append to `tests/test_home_af2_recede_p0.ps1`:
```powershell
$main = Get-Content (Join-Path $root "qml/Main.qml") -Raw
Has $main 'HomeWorldsBar'  "Home body must mount the glass worlds bar."
Has $main 'HomeSpotlight'  "Home body must mount the featured spotlight."
Has $main 'HomeRail'       "Home body must mount rails."
Has $main 'homeRecede'     "Home must compute a 0..1 recede from scroll and feed the spotlight+bar."
Has $main 'prefers.*reduced|reducedMotion|Accessibility' 'Home recede must respect reduced motion.'
# all three worlds present as rails
foreach($w in @('Tankoban','Theatre','Biblio')){ Has $main "worldTag: `"$w`"" "Home must carry a $w rail." }
Has $main 'Progress.recent' "Continue rail must be fed by the cross-world Progress store."
Write-Host "home body wiring ok"
```

- [ ] **Step 2: Run, verify RED.**

- [ ] **Step 3: Implement the Home body in `qml/Main.qml`**

Replace the current Home Column body (the `contCol` Continue block + mode-intro widgets — locate via `id: contCol` and the `CONTINUE` comment) with:
- A `property real homeRecede: Math.min(1, homeFlick.contentY / (height*0.8))` (0 when reduced-motion via `reducedMotion` gate → keep at a stepped 0/1 cross-fade).
- `HomeWorldsBar { recede: homeRecede; activeWorld: "Home"; onWorldPicked: win.routeToWorld(world); onSearchRequested: win.openSearch("Home"); onWallpaperRequested: win.openWallpaperSearch("Home") }` — reuse existing routing (`worldStack`, `openWallpaperSearch`).
- `HomeSpotlight { recede: homeRecede; ... }` fed from the most-recently-touched Progress entry: `logoUrl` built as `"https://live.metahub.space/logo/medium/" + ttId + "/img"` (extract ttId from the entry art, the player pattern), `primaryRequested → win.resumeContinue(top)`, `hasSecondary` when the title also exists in another world.
- The glass board = a frosted `Rectangle` (`theme.glassTint`, `MultiEffect` blur of the wallpaper, top `theme.edge` hairline) rising at `margin-top ~ 98vh`, holding:
  - `HomeRail { railTitle: "Continue"; cardShape: "landscape"; model: <Progress.recent("",12) mapped to world-tagged cards>; onSeeAll: win.openContinueSeeAll("home") }`
  - `HomeRail { worldTag: "Theatre"; railTitle: "Featured this week"; model: Catalog.theatreFeatured; cardShape: "landscape" }`
  - `HomeRail { worldTag: "Tankoban"; railTitle: "New volumes"; cardShape: "portrait"; model: <manga volume index> }`
  - `HomeRail { worldTag: "Biblio"; railTitle: "On your shelf"; cardShape: "jacket"; model: <Biblio shelf entries> }`
- Keep the wallpaper layer + world routing intact.

Follow the approved mock for exact spacing/composition. Reuse `Progress.recent`, `Catalog.*`, and the manga/Biblio sources already used by the world pages.

- [ ] **Step 4: Verify GREEN + commit**

```bash
git add -- qml/Main.qml tests/test_home_af2_recede_p0.ps1
git commit -m "[Agent 4 (Claude), player] feat(home): compose glass board + 3-world rails + recede"
git push origin master
```

---

### Task 6: Build, regression, and eyes-on parity

**Files:** modify only on a verified defect in files above.

- [ ] **Step 1: Run all home + regression P0 tests**
```bash
for t in tests/test_home_af2_*_p0.ps1; do powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$t"; done
```
Expected: all print their ok line.

- [ ] **Step 2: Build the app**
Run: `cmake --build native/build-msvc --target colosseum` (or `native/build-msvc.bat`). Expected exit 0, boot smoke clean (no QML type/reference errors; Figtree registers).

- [ ] **Step 3: Eyes-on parity (Hemanth; Claude launches, no terminal work for him)**
Launch the app; verify against `agents/colosseum-home-af2-audit-mock.html`:
1. Featured spotlight recedes (lifts + fades) as the glass board rises on scroll; reduced-motion cross-fades.
2. Board is frosted glass over the persistent wallpaper — art visible behind, never flat black.
3. Continue spans all three worlds (world-tagged); a Theatre, Tankoban, Biblio rail each; correct card shapes.
4. Hero shows the metahub logo (text fallback) and world-appropriate actions.
5. Gold only on progress + primary; per-world tint a faint cue; ink ramp holds.
6. Tapping a world routes correctly; keep-alive world pages unbroken.

- [ ] **Step 4: Cross-substrate review + close findings**
Package a Codex review of the diff against the 8 acceptance criteria (spec). Address P0/P1, rerun affected tests/build, then flag ready.

---

## Self-Review notes (author pass)

- **Spec coverage:** spotlight recede (T3+T5), glass board over wallpaper (T5), 3 worlds/Continue-spanning (T4+T5), view_row cadence (T1+T4), metahub logo + world actions (T3+T5), gold/tint discipline (T2/T3/T4), routing intact (T5), no new networking (T5, reuse stores) — all mapped. Font decision resolved as bundling Figtree (T1).
- **Open build detail deferred to execution (not a placeholder):** the exact Biblio shelf + manga volume-index model bindings — use whatever `TankobanWorld.qml`/`BiblioWorld.qml` already bind (read those two files first in T5). Named, not vague.
- **Type consistency:** `recede` (real 0..1), `worldTag` (string), `shape`/`cardShape` (string) used consistently across HomeSpotlight/WorldsBar/HomeRail/HomeCard/Main.
