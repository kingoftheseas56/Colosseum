# Colosseum mpv Zero-Drop Experiment Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make Colosseum's integrated mpv use the standalone-proven synchronization settings, then prove whether it sustains zero additional decoder and output drops in two consecutive five-minute Tenet runs after warm-up.

**Architecture:** Keep the existing `MpvItem`/QtQuick presentation path and change only two mpv properties first. Add an environment-gated measurement seam inside `MpvItem`, then drive the real `qml/Main.qml` player through the existing `COLOSSEUM_ABBA_CLIP` route. A PowerShell gate launches two fresh visible app processes, captures their structured results, and refuses to pass on any non-zero measured drop delta.

**Tech Stack:** Qt 6.11.1, MpvQt/libmpv, QML, C++20, PowerShell 5.1, MSVC 2022

## Global Constraints

- Media is `C:\Users\Suprabha\Downloads\Colosseum\Tenet - 20260726_184029.mp4`.
- Every measured run uses a fresh Colosseum launch, a 30-second warm-up, and a 300-second measurement window.
- Passing requires zero additional decoder drops and zero additional output drops in both runs.
- `video-sync=display-resample` and `interpolation=yes` are the only playback-policy changes.
- mpv remains the daily-driver engine; launch with `COLOSSEUM_MPV=1`.
- Do not modify Player 2 files, change the default backend, or begin direct-present work.
- Preserve all pre-existing dirty files and untracked artifacts in the worktree.
- Do not kill any pre-existing `colosseum.exe`; stop only a process launched by the gate if that exact process times out.
- Hemanth supplies the eyes-on smoothness verdict; agents own builds, logs, and counter arithmetic.

## File Structure

- Modify `native/player/mpvitem.cpp`: set the two smoothness properties and implement the environment-gated probe.
- Modify `native/player/mpvitem.h`: hold the probe timers, baseline snapshot, and private probe methods.
- Create `tests/test_player_mpv_smoothness_p0.ps1`: lock the two mpv properties and the opt-in probe boundary.
- Create `tests/mpv_zero_drop_gate.ps1`: launch the real app twice, preserve logs, parse structured results, and enforce zero deltas.
- Create `tests/test_mpv_zero_drop_gate_parser.ps1`: prove missing/invalid telemetry, drops, and stalls fail closed.
- Create `docs/superpowers/specs/2026-07-28-colosseum-mpv-zero-drop-report.md`: record exact build identity, both runs, and the eyes-on verdict.

---

### Task 1: Enable the standalone-proven mpv synchronization policy

**Files:**
- Create: `tests/test_player_mpv_smoothness_p0.ps1`
- Modify: `native/player/mpvitem.cpp:45-76`

**Interfaces:**
- Consumes: `MpvAbstractItem::setProperty(const QString &, const QVariant &)` during `MpvItem` construction.
- Produces: every `MpvItem` starts with mpv properties `video-sync=display-resample` and `interpolation=yes`.

- [ ] **Step 1: Write the failing source contract**

Create `tests/test_player_mpv_smoothness_p0.ps1`:

```powershell
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$source = Get-Content -Raw (Join-Path $root 'native/player/mpvitem.cpp')

function Require-Literal([string]$haystack, [string]$needle, [string]$message) {
    if (-not $haystack.Contains($needle)) { throw $message }
}

Require-Literal $source 'setProperty(QStringLiteral("video-sync"), QStringLiteral("display-resample"));' `
    'MpvItem must synchronize video to the display clock.'
Require-Literal $source 'setProperty(QStringLiteral("interpolation"), QStringLiteral("yes"));' `
    'MpvItem must interpolate non-integer source/display cadence.'

Write-Output 'test_player_mpv_smoothness_p0: PASS'
```

- [ ] **Step 2: Run the contract and prove RED**

Run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests\test_player_mpv_smoothness_p0.ps1
```

Expected: non-zero exit with `MpvItem must synchronize video to the display clock.`

- [ ] **Step 3: Apply the minimal mpv policy**

Immediately after the existing `hwdec` property in `MpvItem::MpvItem`, add:

```cpp
    // Match the standalone mpv policy that sustained a zero-drop five-minute Tenet interval:
    // lock video to the real display cadence and synthesize the in-between presentation samples.
    setProperty(QStringLiteral("video-sync"), QStringLiteral("display-resample"));
    setProperty(QStringLiteral("interpolation"), QStringLiteral("yes"));
```

Do not change `hwdec`, normalization, cache, scaling, or any Player 2 setting in this task.

- [ ] **Step 4: Prove GREEN and build the real application**

Run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests\test_player_mpv_smoothness_p0.ps1
cmd /c native\_reconf2.bat
```

Expected: contract prints `PASS`; build output contains `BUILD_OK`.

- [ ] **Step 5: Commit only Task 1**

```powershell
git add -- tests/test_player_mpv_smoothness_p0.ps1 native/player/mpvitem.cpp
git diff --cached --check
git commit -m "fix(player): synchronize mpv to display cadence"
```

---

### Task 2: Add a fail-closed zero-drop measurement gate

**Files:**
- Modify: `tests/test_player_mpv_smoothness_p0.ps1`
- Modify: `native/player/mpvitem.h:10-14,161-198`
- Modify: `native/player/mpvitem.cpp:4-18,76-80,97-124`
- Create: `tests/mpv_zero_drop_gate.ps1`

**Interfaces:**
- Consumes:
  - `COLOSSEUM_MPV_DROP_PROBE=30,300` (`warmupSeconds,measureSeconds`).
  - Existing `COLOSSEUM_ABBA_CLIP` real-player autoplay route.
  - mpv properties `decoder-frame-drop-count`, `frame-drop-count`, `hwdec-current`, `avsync`, `video-sync`, and `interpolation`.
- Produces:
  - One stderr line beginning `MPV_DROP_PROBE RESULT ` followed by compact JSON.
  - JSON keys `decoderStart`, `decoderEnd`, `decoderDelta`, `outputStart`, `outputEnd`,
    `outputDelta`, `hwdec`, `avsyncStart`, `avsyncEnd`, `positionStart`,
    `positionEnd`, `videoSync`, and `interpolation`.
  - Process exit after the measurement result is flushed.

- [ ] **Step 1: Extend the failing contract**

Append these assertions to `tests/test_player_mpv_smoothness_p0.ps1` before its `PASS` line:

```powershell
$header = Get-Content -Raw (Join-Path $root 'native/player/mpvitem.h')
Require-Literal $source 'qEnvironmentVariable("COLOSSEUM_MPV_DROP_PROBE")' `
    'The drop probe must be explicitly environment-gated.'
Require-Literal $source 'MPV_DROP_PROBE RESULT ' `
    'The drop probe must publish one machine-readable final result.'
if ($header -notmatch 'void startDropProbe\(\);' -or
    $header -notmatch 'QVariantMap dropProbeSnapshot\(\) const;') {
    throw 'MpvItem must expose private start/snapshot probe seams.'
}
```

Move `Write-Output 'test_player_mpv_smoothness_p0: PASS'` after the new assertions.

- [ ] **Step 2: Run the contract and prove RED**

Run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests\test_player_mpv_smoothness_p0.ps1
```

Expected: non-zero exit with `The drop probe must be explicitly environment-gated.`

- [ ] **Step 3: Declare the private probe state**

In `native/player/mpvitem.h`, include `QVariantMap` and add:

```cpp
    void startDropProbe();
    QVariantMap dropProbeSnapshot() const;
    void captureDropProbeBaseline();
    void finishDropProbe();

    QTimer m_dropProbeWarmupTimer;
    QTimer m_dropProbeMeasureTimer;
    QVariantMap m_dropProbeStart;
    int m_dropProbeWarmupSeconds = 0;
    int m_dropProbeMeasureSeconds = 0;
    bool m_dropProbeArmed = false;
```

These remain private and do not enlarge the QML player API.

- [ ] **Step 4: Parse the opt-in probe and wire its timers**

In `native/player/mpvitem.cpp`, include `QJsonDocument` and `QLoggingCategory`. At the end of the
constructor, parse only the exact two-positive-integer format:

```cpp
    const QString dropProbeSpec = qEnvironmentVariable("COLOSSEUM_MPV_DROP_PROBE");
    const QStringList dropProbeParts = dropProbeSpec.split(QLatin1Char(','));
    bool warmupOk = false;
    bool measureOk = false;
    if (dropProbeParts.size() == 2) {
        m_dropProbeWarmupSeconds = dropProbeParts.at(0).toInt(&warmupOk);
        m_dropProbeMeasureSeconds = dropProbeParts.at(1).toInt(&measureOk);
    }
    m_dropProbeArmed = warmupOk && measureOk
        && m_dropProbeWarmupSeconds > 0 && m_dropProbeWarmupSeconds <= 86'400
        && m_dropProbeMeasureSeconds > 0 && m_dropProbeMeasureSeconds <= 86'400;
    if (!dropProbeSpec.isEmpty() && !m_dropProbeArmed)
        qWarning() << "MPV_DROP_PROBE INVALID" << dropProbeSpec;

    m_dropProbeWarmupTimer.setSingleShot(true);
    m_dropProbeMeasureTimer.setSingleShot(true);
    connect(this, &MpvItem::fileLoaded, this, &MpvItem::startDropProbe);
    connect(&m_dropProbeWarmupTimer, &QTimer::timeout,
            this, &MpvItem::captureDropProbeBaseline);
    connect(&m_dropProbeMeasureTimer, &QTimer::timeout,
            this, &MpvItem::finishDropProbe);
```

- [ ] **Step 5: Implement snapshots and delta publication**

Implement the private methods with this behavior:

```cpp
QVariantMap MpvItem::dropProbeSnapshot() const
{
    auto *self = const_cast<MpvItem *>(this);
    return {
        // Keep the raw QVariant values. An unavailable property must reach the gate as JSON null,
        // never coerce to the same numeric zero as a healthy counter.
        {QStringLiteral("decoder"), self->getProperty(QStringLiteral("decoder-frame-drop-count"))},
        {QStringLiteral("output"), self->getProperty(QStringLiteral("frame-drop-count"))},
        {QStringLiteral("hwdec"), self->getProperty(QStringLiteral("hwdec-current")).toString()},
        {QStringLiteral("avsync"), self->getProperty(QStringLiteral("avsync")).toDouble()},
        {QStringLiteral("position"), self->getProperty(QStringLiteral("time-pos")).toDouble()},
        {QStringLiteral("videoSync"), self->getProperty(QStringLiteral("video-sync")).toString()},
        {QStringLiteral("interpolation"), self->getProperty(QStringLiteral("interpolation")).toBool()},
    };
}

void MpvItem::startDropProbe()
{
    if (!m_dropProbeArmed)
        return;
    m_dropProbeArmed = false;
    m_dropProbeWarmupTimer.start(m_dropProbeWarmupSeconds * 1000);
}

void MpvItem::captureDropProbeBaseline()
{
    m_dropProbeStart = dropProbeSnapshot();
    m_dropProbeMeasureTimer.start(m_dropProbeMeasureSeconds * 1000);
}

void MpvItem::finishDropProbe()
{
    const QVariantMap end = dropProbeSnapshot();
    QVariantMap result = {
        {QStringLiteral("decoderStart"), m_dropProbeStart.value(QStringLiteral("decoder"))},
        {QStringLiteral("decoderEnd"), end.value(QStringLiteral("decoder"))},
        {QStringLiteral("decoderDelta"), end.value(QStringLiteral("decoder")).toLongLong()
                                         - m_dropProbeStart.value(QStringLiteral("decoder")).toLongLong()},
        {QStringLiteral("outputStart"), m_dropProbeStart.value(QStringLiteral("output"))},
        {QStringLiteral("outputEnd"), end.value(QStringLiteral("output"))},
        {QStringLiteral("outputDelta"), end.value(QStringLiteral("output")).toLongLong()
                                        - m_dropProbeStart.value(QStringLiteral("output")).toLongLong()},
        {QStringLiteral("hwdec"), end.value(QStringLiteral("hwdec"))},
        {QStringLiteral("avsyncStart"), m_dropProbeStart.value(QStringLiteral("avsync"))},
        {QStringLiteral("avsyncEnd"), end.value(QStringLiteral("avsync"))},
        {QStringLiteral("positionStart"), m_dropProbeStart.value(QStringLiteral("position"))},
        {QStringLiteral("positionEnd"), end.value(QStringLiteral("position"))},
        {QStringLiteral("videoSync"), end.value(QStringLiteral("videoSync"))},
        {QStringLiteral("interpolation"), end.value(QStringLiteral("interpolation"))},
    };
    qInfo().noquote() << "MPV_DROP_PROBE RESULT "
                      << QJsonDocument::fromVariant(result).toJson(QJsonDocument::Compact);
    QTimer::singleShot(0, QCoreApplication::instance(), &QCoreApplication::quit);
}
```

If compilation shows that MpvQt's `getProperty` is non-const, retain the scoped `const_cast` shown
above rather than changing the public method signature.

- [ ] **Step 6: Create the two-run real-app gate**

Create `tests/mpv_zero_drop_gate.ps1` with parameters `-Exe`, `-Clip`, `-WarmupSeconds` (default
30), `-MeasureSeconds` (default 300), and `-Runs` (default 2). It must:

```powershell
$env:COLOSSEUM_MPV = '1'
$env:COLOSSEUM_ABBA_CLIP = $Clip
$env:COLOSSEUM_MPV_DROP_PROBE = "$WarmupSeconds,$MeasureSeconds"
```

For each run, launch visible with:

```powershell
$process = Start-Process -FilePath $Exe -ArgumentList @('qml\Main.qml') `
    -WorkingDirectory $root -PassThru `
    -RedirectStandardOutput $stdoutLog -RedirectStandardError $stderrLog
```

Wait no longer than `WarmupSeconds + MeasureSeconds + 60`. On timeout, stop only `$process.Id` and
fail. Parse the final stderr line using:

```powershell
$match = [regex]::Match($text, 'MPV_DROP_PROBE RESULT\s+(\{[^\r\n]+\})')
if (-not $match.Success) { throw "run $run has no structured probe result" }
$result = $match.Groups[1].Value | ConvertFrom-Json
$validated = Test-MpvProbeResult -Result $result -MeasureSeconds $MeasureSeconds
if ($validated.DecoderDelta -ne 0 -or $validated.OutputDelta -ne 0) {
    throw "run $run dropped frames: decoder=$($validated.DecoderDelta), output=$($validated.OutputDelta)"
}
```

`Test-MpvProbeResult` must reject null/non-numeric counters and positions, non-finite A/V sync,
missing hardware decode/policy fields, a policy other than `display-resample` + boolean `true`,
emitted deltas that disagree with recomputed start/end deltas, and playback progress below 90% of
the requested measurement window.

Preserve both logs under `artifacts/mpv-zero-drop/<timestamp>/`. Restore every changed environment
variable, including `PATH`, in a `finally` block. Print one summary line per run and a final
`MPV ZERO DROP GATE: PASS`.

- [ ] **Step 7: Prove the parser fails closed**

Create `tests/test_mpv_zero_drop_gate_parser.ps1`. Invoke the gate's validation-only file path with
fixture JSON for:

- a valid zero-drop result with at least 90% playback progress (must pass);
- a missing or null counter (must fail);
- a non-zero decoder or output delta (must fail);
- insufficient playback progress (must fail);
- non-finite A/V synchronization (must fail).

- [ ] **Step 8: Prove the contract and compile**

Run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests\test_player_mpv_smoothness_p0.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File tests\test_mpv_zero_drop_gate_parser.ps1
cmd /c native\_reconf2.bat
```

Expected: contract `PASS`; build contains `BUILD_OK`.

- [ ] **Step 9: Run a short instrumentation smoke**

Run with a 2-second warm-up and 5-second measurement:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests\mpv_zero_drop_gate.ps1 `
  -Exe native\build-msvc\colosseum.exe `
  -Clip 'C:\Users\Suprabha\Downloads\Colosseum\Tenet - 20260726_184029.mp4' `
  -WarmupSeconds 2 -MeasureSeconds 5 -Runs 1
```

Expected: one structured result is parsed. A non-zero drop delta is allowed to fail this short
smoke; the required proof comes from the full two-run gate. Inspect the log to confirm the backend
line says `mpv (player 1)` and the Tenet path auto-opened.

- [ ] **Step 10: Commit only the measurement gate**

```powershell
git add -- native/player/mpvitem.h native/player/mpvitem.cpp `
  tests/test_player_mpv_smoothness_p0.ps1 tests/mpv_zero_drop_gate.ps1 `
  tests/test_mpv_zero_drop_gate_parser.ps1
git diff --cached --check
git commit -m "test(player): measure integrated mpv drop deltas"
```

---

### Task 3: Run the two-pass proof and write the decision

**Files:**
- Create: `docs/superpowers/specs/2026-07-28-colosseum-mpv-zero-drop-report.md`
- Preserve as untracked evidence: `artifacts/mpv-zero-drop/<timestamp>/*`

**Interfaces:**
- Consumes: exact `native/build-msvc/colosseum.exe` produced from Tasks 1-2 and the gate script.
- Produces: a committed evidence report with one honest PASS or FAIL verdict.

- [ ] **Step 1: Record the exact build identity**

Record:

```powershell
git rev-parse HEAD
(Get-Item native\build-msvc\colosseum.exe).FullName
(Get-Item native\build-msvc\colosseum.exe).LastWriteTime.ToString('o')
Get-CimInstance Win32_VideoController | Select-Object Name,DriverVersion
```

- [ ] **Step 2: Run the required two visible passes**

Run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests\mpv_zero_drop_gate.ps1 `
  -Exe native\build-msvc\colosseum.exe `
  -Clip 'C:\Users\Suprabha\Downloads\Colosseum\Tenet - 20260726_184029.mp4' `
  -WarmupSeconds 30 -MeasureSeconds 300 -Runs 2
```

Each pass lasts about 5.5 minutes and exits itself. Keep the app visible so Hemanth can watch
motion. Do not run the two processes concurrently.

- [ ] **Step 3: Classify any failure before proposing more work**

If either run has a non-zero delta, retain its exact start/end values and inspect timestamps/log
context for steady accumulation versus clustered drops. Do not begin direct-present work.

- [ ] **Step 4: Write the evidence report**

Create `docs/superpowers/specs/2026-07-28-colosseum-mpv-zero-drop-report.md` with:

```markdown
# Colosseum mpv Zero-Drop Report

## Build identity
## Controlled inputs
## Run 1
## Run 2
## A/V synchronization and hardware decode
## Hemanth eyes-on verdict
## Result
```

The Result must be exactly one of:

- `PASS - tuned integrated mpv sustains zero additional drops`
- `FAIL - direct-present design required`

Do not write PASS while Hemanth's eyes-on verdict is missing.

- [ ] **Step 5: Re-run the static and build gates**

Run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests\test_player_mpv_smoothness_p0.ps1
cmd /c native\_reconf2.bat
git diff --check
```

Expected: contract `PASS`, build contains `BUILD_OK`, and no whitespace errors.

- [ ] **Step 6: Commit the evidence report**

```powershell
git add -- docs/superpowers/specs/2026-07-28-colosseum-mpv-zero-drop-report.md
git diff --cached --check
git commit -m "docs(player): record integrated mpv zero-drop result"
```

Do not stage runtime logs, Player 2 changes, `qml/Main.qml`, or pre-existing untracked helpers.
