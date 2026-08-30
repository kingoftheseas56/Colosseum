# Comic Reader - ACCEPTANCE.
#
# One command, one verdict. Builds every native comic reader harness from the CURRENT tree, runs
# them, runs every QML gate, and prints COMICREADER_ACCEPTANCE_OK only if all of them pass.
#
# WHY IT EXISTS. The reader's gates had grown to fourteen separate scripts and executables, each run
# by hand. That is how a red gate hides: one gets skipped, the rest are green, and "the suite passes"
# becomes true-ish rather than true. It also rebuilds the native harnesses rather than trusting
# whatever .exe happens to be lying in build-msvc, because a stale harness passing an old contract
# is the most convincing kind of false green.
#
# WHAT IT IS NOT. It is not eyes-on. Every gate here can be green while the reader still feels wrong
# in the hand - that has happened repeatedly on this project, most recently with the fullscreen
# transition, which was invisible to the whole suite until it was measured deliberately. Hemanth's
# eyes remain the gate this script cannot be.
#
# Usage:
#   powershell -NoProfile -ExecutionPolicy Bypass -File tests/test_comicreader_acceptance.ps1
#   ...          -SkipBuild     run against the harness .exe files already built (fast iteration)
#   ...          -QmlOnly       skip the native half entirely
#
# This file is deliberately pure ASCII: PowerShell 5.1 reading a BOM-less UTF-8 script mis-frames
# multi-byte characters inside quoted strings and reports bogus parse errors.

param(
    [switch]$SkipBuild,
    [switch]$QmlOnly
)

$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$qmlExe   = "C:/Qt/6.11.1/msvc2022_64/bin/qml.exe"
$buildBat = Join-Path $repoRoot "native/build-target.bat"
$buildDir = Join-Path $repoRoot "native/build-msvc"

# The QML gates. Each is a sibling test_comicreader_*.ps1 that exits 0 on success.
$qmlGates = @(
    "chrome",       # HUD, toast, back action, auto-hide, positioner-anchor warnings
    "surfaces",     # strip + double geometry, wheel drain, glide, atEnd
    "shell",        # orchestration, persistence, crossings, end-of-volume
    "contract",     # public surface the callers depend on
    "overlays",     # settings sheet and friends
    "state",        # pure store/reading-mode logic
    "migration",    # the MangaReader -> ComicReaderShell cutover
    "sync_resume",  # imported Progress resolver/bridge contract
    "sync_resume_shell", # imported Progress reaches the real active shell
    "fullscreen"    # the transition ordering (needs a REAL window; see its header)
)

# The native harnesses. Target name == executable name.
$nativeHarnesses = @(
    "comicreader_core_harness",
    "comicreader_pairing_harness",
    "comicreader_cache_harness",
    "comicreader_decode_harness",
    "comicreader_coupling_harness",
    "comicreader_strip_harness"
)

$failures = @()
$results  = @()

function Record($name, $ok, $detail) {
    $script:results += [PSCustomObject]@{ Name = $name; Ok = $ok; Detail = $detail }
    if (-not $ok) { $script:failures += ("{0}: {1}" -f $name, $detail) }
    $tag = if ($ok) { "PASS" } else { "FAIL" }
    Write-Host ("  {0,-34} {1}" -f $name, $tag)
}

if (!(Test-Path -LiteralPath $qmlExe)) {
    Write-Host "COMICREADER_ACCEPTANCE_FAIL: qml.exe not found at $qmlExe"
    exit 1
}

$env:QT_FORCE_STDERR_LOGGING = "1"
$env:QTFRAMEWORK_BYPASS_LICENSE_CHECK = "1"
# The Qt bin directory must lead PATH so the harnesses resolve their own Qt DLLs.
$env:PATH = "C:\Qt\6.11.1\msvc2022_64\bin;" + $buildDir + ";" + $env:PATH

$started = Get-Date

# ---------------------------------------------------------------- native half
if (-not $QmlOnly) {
    Write-Host ""
    Write-Host "NATIVE HARNESSES"
    foreach ($h in $nativeHarnesses) {
        $exe = Join-Path $buildDir "$h.exe"

        if (-not $SkipBuild) {
            if (!(Test-Path -LiteralPath $buildBat)) {
                Record $h $false "build-target.bat not found at $buildBat"
                continue
            }
            # build-target.bat needs an ABSOLUTE path invocation (house trap).
            #
            # ErrorActionPreference is dropped to Continue around the call ON PURPOSE. vcvars64
            # emits a harmless "'vswhere.exe' is not recognized" line on stderr, and under "Stop"
            # PowerShell promotes ANY native stderr output to a terminating error - so a build that
            # printed TARGET_BUILD_OK was being reported as a failed build. The log is the source of
            # truth here, which is the same house rule that says build exit codes lie.
            $log = Join-Path $env:TEMP "cr_acc_build_$h.log"
            $prevBuild = $ErrorActionPreference
            $ErrorActionPreference = "Continue"
            & cmd.exe /c "`"$buildBat`" $h" > $log 2>&1
            $ErrorActionPreference = $prevBuild
            # Backgrounded/again-shelled build exit codes lie on this toolchain - grep the log for
            # the success marker the batch prints, and for the two failure signatures.
            $logText = Get-Content -LiteralPath $log -Raw -ErrorAction SilentlyContinue
            if (-not $logText -or $logText -notmatch "TARGET_BUILD_OK") {
                $sig = ""
                if ($logText) {
                    $m = [regex]::Matches($logText, "(?m)^.*(error C\d+|LNK\d+|ninja: build stopped|fatal error).*$")
                    if ($m.Count -gt 0) { $sig = $m[0].Value.Trim() }
                }
                Record $h $false ("build failed" + $(if ($sig) { " - $sig" } else { " (see $log)" }))
                continue
            }
        }

        if (!(Test-Path -LiteralPath $exe)) {
            Record $h $false "executable missing at $exe (run without -SkipBuild)"
            continue
        }

        $prev = $ErrorActionPreference
        $ErrorActionPreference = "Continue"
        $out = & $exe 2>&1 | Out-String
        $code = $LASTEXITCODE
        $ErrorActionPreference = $prev

        if ($code -ne 0) {
            $line = ($out -split "`n" | Where-Object { $_ -match "FAIL" } | Select-Object -First 1)
            if (-not $line) { $line = ($out -split "`n" | Select-Object -Last 3) -join " / " }
            Record $h $false ("exit $code - " + $line.Trim())
        } else {
            Record $h $true "ok"
        }
    }
}

# ------------------------------------------------------------------- QML half
Write-Host ""
Write-Host "QML GATES"
foreach ($g in $qmlGates) {
    $script = Join-Path $PSScriptRoot "test_comicreader_$g.ps1"
    if (!(Test-Path -LiteralPath $script)) {
        Record $g $false "gate script missing at $script"
        continue
    }
    $prev = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    $out = & powershell -NoProfile -ExecutionPolicy Bypass -File $script 2>&1 | Out-String
    $code = $LASTEXITCODE
    $ErrorActionPreference = $prev

    if ($code -ne 0) {
        $line = ($out -split "`n" | Where-Object { $_ -match "FAIL" } | Select-Object -First 1)
        if (-not $line) { $line = ($out -split "`n" | Select-Object -Last 3) -join " / " }
        Record $g $false ("exit $code - " + $line.Trim())
    } else {
        Record $g $true "ok"
    }
}

# --------------------------------------------------------------------- verdict
$elapsed = [int]((Get-Date) - $started).TotalSeconds
$total = $results.Count
$passed = ($results | Where-Object { $_.Ok }).Count

Write-Host ""
Write-Host ("{0}/{1} gates passed in {2}s" -f $passed, $total, $elapsed)

if ($failures.Count -gt 0) {
    Write-Host ""
    Write-Host "FAILURES:"
    foreach ($f in $failures) { Write-Host ("  - " + $f) }
    Write-Host ""
    Write-Host "COMICREADER_ACCEPTANCE_FAIL"
    exit 1
}

# A green run that exercised nothing is the failure mode this guards against: if the gate list is
# ever emptied or every entry silently skipped, the script must not report success.
$expected = $(if ($QmlOnly) { $qmlGates.Count } else { $qmlGates.Count + $nativeHarnesses.Count })
if ($total -lt $expected) {
    Write-Host ("COMICREADER_ACCEPTANCE_FAIL: expected {0} gates, only {1} ran" -f $expected, $total)
    exit 1
}

Write-Host "COMICREADER_ACCEPTANCE_OK"
exit 0
