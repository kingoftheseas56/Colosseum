param(
    [Parameter(Mandatory = $true)]
    [string]$ScratchRoot
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$repoRoot = (git rev-parse --show-toplevel).Trim()
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($repoRoot)) {
    throw "run_mutations.ps1 must run inside the Colosseum Git worktree"
}
$repoRoot = [IO.Path]::GetFullPath($repoRoot)
$scratchBase = [IO.Path]::GetFullPath($ScratchRoot)
if ($scratchBase -eq $repoRoot) {
    throw "ScratchRoot must not be the repository root"
}

$runRoot = Join-Path $scratchBase ("k06-f2-mutations-" + [guid]::NewGuid().ToString("N"))
$sourceRoot = Join-Path $runRoot "source"
$buildRoot = Join-Path $runRoot "build"
$logsRoot = Join-Path $runRoot "logs"
$archivePath = Join-Path $runRoot "source.zip"
New-Item -ItemType Directory -Path $sourceRoot, $logsRoot -Force | Out-Null

git archive --format=zip --output=$archivePath HEAD
if ($LASTEXITCODE -ne 0) { throw "git archive failed" }
Expand-Archive -LiteralPath $archivePath -DestinationPath $sourceRoot

# Overlay the candidate files so the runner works both before and after the
# additive repair commit.
$candidateFiles = @(
    "native/colosseum_server_v1/src/storage/PersistentPieceStore.cpp",
    "native/colosseum_server_v1/src/storage/VerificationBitmap.cpp",
    "native/colosseum_server_v1/tests/test_persistent_store.cpp",
    "native/colosseum_server_v1/tests/cmake/K06.cmake"
)
foreach ($relative in $candidateFiles) {
    $destination = Join-Path $sourceRoot $relative
    New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $repoRoot $relative) -Destination $destination -Force
}

function Invoke-Captured([string]$LogPath, [scriptblock]$Action) {
    $savedErrorAction = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        $lines = @(& $Action 2>&1 | ForEach-Object { $_.ToString() })
        $exitCode = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $savedErrorAction
    }
    [IO.File]::WriteAllLines($LogPath, $lines)
    return [pscustomobject]@{ ExitCode = $exitCode; Lines = $lines }
}

$configure = Invoke-Captured (Join-Path $logsRoot "CONFIGURE.txt") {
    cmake -S (Join-Path $sourceRoot "native/colosseum_server_v1") `
          -B $buildRoot -G Ninja -DCMAKE_BUILD_TYPE=Release `
          -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL
}
if ($configure.ExitCode -ne 0) {
    $configure.Lines | ForEach-Object { Write-Output $_ }
    throw "mutation configure failed"
}

$controls = @(
    [pscustomobject]@{ Id="M01"; Patch="M01-omit-restored-committed.patch"; Expected="valid persisted first piece restores verified and committed only" },
    [pscustomobject]@{ Id="M02"; Patch="M02-skip-physical-length.patch"; Expected="short physical destination invalidates persisted true bit" },
    [pscustomobject]@{ Id="M03"; Patch="M03-infer-false-bit.patch"; Expected="physical bytes never promote a persisted false bit" },
    [pscustomobject]@{ Id="M04"; Patch="M04-restore-assembled.patch"; Expected="valid persisted first piece restores verified and committed only" },
    [pscustomobject]@{ Id="M05"; Patch="M05-skip-invalidation-persist.patch"; Expected="missing-destination invalidation is persisted" },
    [pscustomobject]@{ Id="M06"; Patch="M06-skip-logical-coverage.patch"; Expected="logical destination gap invalidates persisted true bit" },
    [pscustomobject]@{ Id="M07"; Patch="M07-skip-staged-read.patch"; Expected="staged bytes remain the memory-first read source" },
    [pscustomobject]@{ Id="M08"; Patch="M08-silence-bitmap-open-failure.patch"; Expected="bitmap persistence reports destination-open failure" }
)

$artifactRoot = Join-Path $repoRoot "artifacts/server1/K06/K06-F2"
$summary = [System.Collections.Generic.List[string]]::new()
$allPassed = $true
foreach ($control in $controls) {
    $patchPath = Join-Path (Join-Path $artifactRoot "mutations") $control.Patch
    git -C $sourceRoot apply --check -- $patchPath
    if ($LASTEXITCODE -ne 0) { throw "$($control.Id) patch check failed" }
    git -C $sourceRoot apply -- $patchPath
    if ($LASTEXITCODE -ne 0) { throw "$($control.Id) patch failed" }
    try {
        $buildLog = Join-Path $logsRoot ($control.Id + "-BUILD.txt")
        $testLog = Join-Path $logsRoot ($control.Id + "-CTEST.txt")
        $build = Invoke-Captured $buildLog {
            cmake --build $buildRoot --target server1_k06_persistent_store_test --parallel 1
        }
        if ($build.ExitCode -eq 0) {
            $test = Invoke-Captured $testLog {
                ctest --test-dir $buildRoot -R "^K06-F2-persistent-store$" --output-on-failure
            }
        } else {
            $test = [pscustomobject]@{ ExitCode = -1; Lines = @("test not run because build failed") }
            [IO.File]::WriteAllLines($testLog, $test.Lines)
        }
        $matched = @($test.Lines | Where-Object { $_ -like ("*" + $control.Expected + "*") }).Count -gt 0
        $rejected = $build.ExitCode -eq 0 -and $test.ExitCode -ne 0 -and $matched
        $allPassed = $allPassed -and $rejected
        $summary.Add("$($control.Id) build_exit=$($build.ExitCode) test_exit=$($test.ExitCode) expected_match=$matched rejected=$rejected")
    } finally {
        git -C $sourceRoot apply --reverse -- $patchPath
        if ($LASTEXITCODE -ne 0) { throw "$($control.Id) reverse patch failed" }
    }
}

$summaryPath = Join-Path $logsRoot "MUTATION-SUMMARY.txt"
[IO.File]::WriteAllLines($summaryPath, $summary)
Write-Output "MUTATION_RUN_ROOT=$runRoot"
$summary | ForEach-Object { Write-Output $_ }
foreach ($control in $controls) {
    Write-Output ("----- " + $control.Id + " RAW CTEST -----")
    Get-Content -LiteralPath (Join-Path $logsRoot ($control.Id + "-CTEST.txt"))
}
if (-not $allPassed) { exit 1 }
Write-Output "K06_F2_MUTATIONS_OK 8/8"
