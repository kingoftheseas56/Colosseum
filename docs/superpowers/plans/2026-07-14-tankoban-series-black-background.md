# Tankoban Series Black Background Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give comic and manga series pages the exact pitch-black background stack used by Theatre series pages.

**Architecture:** Lock the shared visual contract with a focused static test, then replace only the base color, wallpaper opacity, and scrim colors in two QML files. No component structure or interaction changes.

**Tech Stack:** Qt 6 QML, PowerShell contract tests, MSVC 2022 native build.

## Global Constraints

- Modify only `qml/ComicSeries.qml`, `qml/MangaSeries.qml`, and the new focused test.
- Match `TheatreSeries.qml`: `#000000`, wallpaper opacity `0.5`, black gradient alpha `0.50 / 0.78 / 0.95` at `0.0 / 0.42 / 1.0`.
- Preserve every content, layout, navigation, download, and reader contract.
- Preserve unrelated A2/A5 working-tree changes and stage surgically.

---

### Task 1: Lock and implement Theatre background parity

**Files:**
- Create: `tests/test_tankoban_series_background.ps1`
- Modify: `qml/ComicSeries.qml`
- Modify: `qml/MangaSeries.qml`

**Interfaces:**
- Consumes: Theatre background tokens in `qml/TheatreSeries.qml`.
- Produces: identical background tokens in comic and manga series pages.

- [ ] **Step 1: Write the failing static contract test**

The test reads all three QML files and asserts each contains:

```qml
Rectangle { anchors.fill: parent; color: "#000000" }
opacity: 0.5
GradientStop { position: 0.0; color: Qt.rgba(0, 0, 0, 0.5) }
GradientStop { position: 0.42; color: Qt.rgba(0, 0, 0, 0.78) }
GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.95) }
```

- [ ] **Step 2: Run it and confirm RED**

Run: `powershell -NoProfile -File tests/test_tankoban_series_background.ps1`

Expected: failure naming `ComicSeries.qml` or `MangaSeries.qml` as missing the pitch-black contract.

- [ ] **Step 3: Apply the minimal two-file QML change**

Replace the existing `#07080c` bases and blue-tinted gradient stops with the exact Theatre tokens, and add `opacity: 0.5` to both wallpaper mirrors.

- [ ] **Step 4: Run the focused test and existing page-load contracts**

Run:

```powershell
powershell -NoProfile -File tests/test_tankoban_series_background.ps1
powershell -NoProfile -File tests/test_back_action_p0.ps1
C:\Qt\6.11.1\msvc2022_64\bin\qml.exe -platform offscreen tests/parity_load_harness.qml
```

Expected: background test reports `tankoban series backgrounds: OK`, BackAction contracts pass, and the load harness exits 0.

### Task 2: Build and ship

**Files:**
- No additional production files.

- [ ] **Step 1: Run diff checks and the direct native build**

Stop only returned `colosseum.exe` PIDs, then run `native/build-msvc.bat` directly. Expected: exit 0 and `BUILD_OK`.

- [ ] **Step 2: Commit and push surgically**

Stage only the design, plan, focused test, and two QML files. Commit with substrate attribution, refresh `origin/master`, and push only when the branch has not diverged.
