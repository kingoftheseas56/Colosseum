[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateScript({ Test-Path -LiteralPath $_ -PathType Leaf })]
    [string]$Lanista,

    [Parameter(Mandatory = $true)]
    [ValidateScript({ Test-Path -LiteralPath $_ -PathType Leaf })]
    [string]$Exe,

    [Parameter(Mandatory = $true)]
    [ValidateScript({ Test-Path -LiteralPath $_ -PathType Leaf })]
    [string]$Qml
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$receiptRoot = Join-Path $repoRoot ".superpowers\sdd\2026-09-08-directional-navigation-execution"
$runId = "{0}-{1}" -f (Get-Date -Format "yyyyMMdd-HHmmss"), ([guid]::NewGuid().ToString("N").Substring(0, 8))
$aggregateReceipt = Join-Path $receiptRoot "repair13-wave5-six-family-aggregate-$runId.txt"
$settingsHelper = Join-Path $repoRoot "tests\lanista-seeds\arc41-settings-local-onboarding-v1\prepare.ps1"
$scenarioTimeoutMs = 180000

$families = @(
    [pscustomobject]@{
        Name = "family1-world-featured-continue"
        Scenario = "tests\lanista_scenarios\keyboard_directional_scroll_continuation.json"
        Seed = "tests\lanista_fixtures\arc41-directional-v1"
        Scale = $null
        SettingsSeed = $false
    },
    [pscustomobject]@{
        Name = "family2-reader-long-scroll"
        Scenario = "tests\lanista_scenarios\arc41_reader_long_scroll.json"
        Seed = "tests\lanista-seeds\biblio-long-epub-v1"
        Scale = $null
        SettingsSeed = $false
    },
    [pscustomobject]@{
        Name = "family3-biblio-poster-route-return"
        Scenario = "tests\lanista_scenarios\arc41_biblio_poster_route_return.json"
        Seed = "tests\lanista-seeds\biblio-downloaded-epub-v1"
        Scale = $null
        SettingsSeed = $false
    },
    [pscustomobject]@{
        Name = "family4-settings-overflow"
        Scenario = "tests\lanista_scenarios\arc41_settings_overflow.json"
        Seed = "tests\lanista-seeds\arc41-settings-local-onboarding-v1"
        Scale = "1.5"
        SettingsSeed = $true
    },
    [pscustomobject]@{
        Name = "family5-virtualized-collection"
        Scenario = "tests\lanista_scenarios\arc41_virtualized_collection.json"
        Seed = "tests\lanista-seeds\biblio-virtualized-v1"
        Scale = $null
        SettingsSeed = $false
    },
    [pscustomobject]@{
        Name = "family6-overlay-focus-containment"
        Scenario = "tests\lanista_scenarios\arc41_overlay_focus_containment.json"
        Seed = "tests\lanista-seeds\biblio-virtualized-v1"
        Scale = $null
        SettingsSeed = $false
    }
)

New-Item -ItemType Directory -Force -Path $receiptRoot | Out-Null
Set-Location $repoRoot

function Write-Aggregate([string]$line) {
    [System.IO.File]::AppendAllText(
        $aggregateReceipt,
        ($line + [Environment]::NewLine),
        [System.Text.UTF8Encoding]::new($false))
    Write-Output $line
}

function Write-ReceiptLine([string]$path, [string]$line) {
    [System.IO.File]::AppendAllText(
        $path,
        ($line + [Environment]::NewLine),
        [System.Text.UTF8Encoding]::new($false))
}

function Append-ProcessOutput([string]$path, [string]$outputPath) {
    if (Test-Path -LiteralPath $outputPath -PathType Leaf) {
        foreach ($line in (Get-Content -LiteralPath $outputPath)) {
            Write-ReceiptLine $path ([string]$line)
        }
    }
}

Write-Aggregate "Arc41 directional runtime aggregate"
Write-Aggregate "runId=$runId"
Write-Aggregate "lanista=$Lanista"
Write-Aggregate "exe=$Exe"
Write-Aggregate "qml=$Qml"
Write-Aggregate "started=$(Get-Date -Format o)"

$oldScale = [Environment]::GetEnvironmentVariable("QT_SCALE_FACTOR", "Process")
$completed = 0
$failed = $false

foreach ($family in $families) {
    $tag = "arc41-wave5-aggregate-$runId-$($family.Name)"
    $receipt = Join-Path $receiptRoot "repair13-wave5-$($family.Name)-aggregate-$runId.txt"
    $scenario = Join-Path $repoRoot $family.Scenario
    $seed = Join-Path $repoRoot $family.Seed
    $prepared = $false

    if (-not (Test-Path -LiteralPath $scenario -PathType Leaf)) {
        Write-Aggregate "FAIL $($family.Name): missing scenario $scenario"
        $failed = $true
        break
    }
    if (-not (Test-Path -LiteralPath $seed -PathType Container)) {
        Write-Aggregate "FAIL $($family.Name): missing seed $seed"
        $failed = $true
        break
    }

    try {
        if ($family.Scale) {
            $env:QT_SCALE_FACTOR = $family.Scale
        } else {
            Remove-Item Env:QT_SCALE_FACTOR -ErrorAction SilentlyContinue
        }

        if ($family.SettingsSeed) {
            $helperOutput = @(& $settingsHelper -Tag $tag 2>&1)
            $helperExitCode = $LASTEXITCODE
            foreach ($line in $helperOutput) {
                Write-Aggregate ([string]$line)
            }
            if ($helperExitCode -ne 0) {
                throw "Settings onboarding helper failed with exit code $helperExitCode"
            }
            $prepared = $true
        }

        $args = @(
            "--verbose", "session", "run", $scenario,
            "--exe", $Exe,
            "--qml", $Qml,
            "--tag", $tag,
            "--seed", $seed,
            "--drive",
            "--ready-ms", "90000",
            "--keep-going"
        )
        Write-Aggregate "START $($family.Name) tag=$tag receipt=$receipt"
        [System.IO.File]::WriteAllText(
            $receipt,
            ("Arc41 family=$($family.Name) tag=$tag timeout_ms=$scenarioTimeoutMs" + [Environment]::NewLine),
            [System.Text.UTF8Encoding]::new($false))
        $stdoutPath = "$receipt.stdout.tmp"
        $stderrPath = "$receipt.stderr.tmp"
        Remove-Item -LiteralPath $stdoutPath, $stderrPath -Force -ErrorAction SilentlyContinue
        $lanistaProcess = Start-Process -FilePath $Lanista -ArgumentList $args -NoNewWindow -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath -PassThru
        $deadline = (Get-Date).AddMilliseconds($scenarioTimeoutMs)
        while (-not $lanistaProcess.HasExited -and (Get-Date) -lt $deadline) {
            Start-Sleep -Milliseconds 250
        }
        if (-not $lanistaProcess.HasExited) {
            Write-ReceiptLine $receipt "TIMEOUT after ${scenarioTimeoutMs}ms; terminating Lanista process id=$($lanistaProcess.Id)"
            & taskkill.exe /PID $lanistaProcess.Id /T /F 2>&1 | ForEach-Object { Write-ReceiptLine $receipt ([string]$_) }
            try {
                $lanistaProcess.Kill($true)
            } catch {
                try { $lanistaProcess.Kill() } catch { }
            }
            while (-not $lanistaProcess.HasExited) { Start-Sleep -Milliseconds 100 }
            Append-ProcessOutput $receipt $stdoutPath
            Append-ProcessOutput $receipt $stderrPath
            Remove-Item -LiteralPath $stdoutPath, $stderrPath -Force -ErrorAction SilentlyContinue
            throw "Lanista timed out after ${scenarioTimeoutMs}ms"
        }
        $lanistaProcess.WaitForExit()
        $lanistaProcess.Refresh()
        Append-ProcessOutput $receipt $stdoutPath
        Append-ProcessOutput $receipt $stderrPath
        $exitCode = $lanistaProcess.ExitCode
        if ($null -eq $exitCode) {
            Write-ReceiptLine $receipt "process_exit_code=unavailable; receipt summary remains mandatory"
            $exitCode = 0
        } else {
            Write-ReceiptLine $receipt "process_exit_code=$exitCode"
        }
        Remove-Item -LiteralPath $stdoutPath, $stderrPath -Force -ErrorAction SilentlyContinue
        if ($exitCode -ne 0) {
            throw "Lanista exited with code $exitCode"
        }

        $summary = Get-Content -LiteralPath $receipt -Raw
        if ($summary -notmatch "\d+ steps, 0 failed") {
            throw "receipt does not contain a zero-failure scenario summary"
        }
        $completed += 1
        Write-Aggregate "PASS $($family.Name) receipt=$receipt"
    } catch {
        Write-Aggregate "FAIL $($family.Name): $($_.Exception.Message) receipt=$receipt"
        $failed = $true
    } finally {
        if ($prepared) {
            $cleanupOutput = @(& $settingsHelper -Tag $tag -Clean 2>&1)
            $cleanupExitCode = $LASTEXITCODE
            foreach ($line in $cleanupOutput) {
                Write-Aggregate ([string]$line)
            }
            if ($cleanupExitCode -ne 0) {
                Write-Aggregate "FAIL $($family.Name): Settings onboarding cleanup failed with exit code $cleanupExitCode"
                $failed = $true
            }
        }
        if ($null -eq $oldScale) {
            Remove-Item Env:QT_SCALE_FACTOR -ErrorAction SilentlyContinue
        } else {
            $env:QT_SCALE_FACTOR = $oldScale
        }
    }

    if ($failed) {
        break
    }
}

if ($failed -or $completed -ne $families.Count) {
    Write-Aggregate "AGGREGATE FAIL completed=$completed expected=$($families.Count) finished=$(Get-Date -Format o)"
    exit 1
}

Write-Aggregate "AGGREGATE PASS completed=$completed expected=$($families.Count) finished=$(Get-Date -Format o)"
exit 0
