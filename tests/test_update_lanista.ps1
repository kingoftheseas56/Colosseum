# Production update wiring gate.
# Builds the explicit test-key configuration, replays the two isolated updater
# scenarios, preserves their manifests, and always restores the shipping build.
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$native = Join-Path $root "native"
$build = Join-Path $native "build-msvc"
$cmake = "C:/Qt/Tools/CMake_64/bin/cmake.exe"
$ninja = "C:/Qt/Tools/Ninja/ninja.exe"
$qtPrefix = "C:/Qt/6.11.1/msvc2022_64"
$vcvars = "C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Auxiliary/Build/vcvars64.bat"
$lanista = Join-Path $build "lanista.exe"
$exe = Join-Path $build "colosseum.exe"
$sessionLog = Join-Path $root "artifacts/update-lanista-session-paths.txt"
$previousTesting = $env:COLOSSEUM_UPDATE_TESTING
$previousPath = $env:Path
$env:QTFRAMEWORK_BYPASS_LICENSE_CHECK = "1"
$env:Path = "$qtPrefix/bin;$env:Path"

function Invoke-Checked([string]$file, [string[]]$arguments) {
    & $file @arguments
    if ($LASTEXITCODE -ne 0) { throw "$file failed with exit code $LASTEXITCODE" }
}
function Invoke-CMakeChecked([string[]]$arguments) {
    $argText = ($arguments | ForEach-Object { '"' + $_ + '"' }) -join ' '
    cmd /c "call `"$vcvars`" >nul && `"$cmake`" $argText"
    if ($LASTEXITCODE -ne 0) { throw "$cmake failed with exit code $LASTEXITCODE" }
}

try {
    Push-Location $root
    $env:COLOSSEUM_UPDATE_TESTING = "ON"
    Invoke-CMakeChecked @(
        "-S", $native, "-B", $build, "-G", "Ninja",
        "-DCMAKE_MAKE_PROGRAM=$ninja", "-DCMAKE_BUILD_TYPE=Release",
        "-DCMAKE_PREFIX_PATH=$qtPrefix", "-DCOLOSSEUM_UPDATE_TESTING=ON"
    )
    Invoke-CMakeChecked @("--build", $build, "--target", "colosseum", "lanista")
    if (!(Test-Path -LiteralPath $exe) -or !(Test-Path -LiteralPath $lanista)) {
        throw "test-configured colosseum/lanista binaries are missing"
    }

    $sessionDir = Split-Path -Parent $sessionLog
    New-Item -ItemType Directory -Force -Path $sessionDir | Out-Null
    Remove-Item -LiteralPath $sessionLog -Force -ErrorAction SilentlyContinue
    $runStamp = Get-Date -Format "yyyyMMdd-HHmmss"
    foreach ($case in @(
        @{ Scenario = "tests/lanista_scenarios/update_available.json"; Tag = "updater-available-$runStamp"; Seed = "tests/lanista_fixtures/update-available" },
        @{ Scenario = "tests/lanista_scenarios/update_up_to_date.json"; Tag = "updater-current-$runStamp"; Seed = "tests/lanista_fixtures/update-up-to-date" }
    )) {
        $scenarioPath = Join-Path $root $case.Scenario
        $seedPath = Join-Path $root $case.Seed
        if ($case.Tag -like "updater-current-*") {
            $env:COLOSSEUM_UPDATE_TEST_INSTALLED_VERSION = "1.1.1"
        } else {
            Remove-Item Env:COLOSSEUM_UPDATE_TEST_INSTALLED_VERSION -ErrorAction SilentlyContinue
        }
        $output = & $lanista --verbose session run $scenarioPath --exe $exe --tag $case.Tag --drive --seed $seedPath 2>&1 | Tee-Object -Variable runOutput | Out-String
        $code = $LASTEXITCODE
        Add-Content -LiteralPath $sessionLog -Value $output
        if ($code -ne 0) { throw "Lanista updater scenario failed: $($case.Scenario)`n$output" }
        $manifest = [regex]::Match($output, 'manifest:\s*(\S+/session\.json)').Groups[1].Value
        if ([string]::IsNullOrWhiteSpace($manifest)) {
            throw "Lanista scenario did not report its session manifest: $($case.Scenario)"
        }
        Add-Content -LiteralPath $sessionLog -Value ("{0} => {1}" -f $case.Tag, $manifest)
    }
    Write-Host "test_update_lanista: PASS (isolated Available and UpToDate sessions; paths in $sessionLog)"
}
finally {
    Pop-Location -ErrorAction SilentlyContinue
    $env:COLOSSEUM_UPDATE_TESTING = "OFF"
    Remove-Item Env:COLOSSEUM_UPDATE_TEST_INSTALLED_VERSION -ErrorAction SilentlyContinue
    try {
        Invoke-CMakeChecked @(
            "-S", $native, "-B", $build, "-G", "Ninja",
            "-DCMAKE_MAKE_PROGRAM=$ninja", "-DCMAKE_BUILD_TYPE=Release",
            "-DCMAKE_PREFIX_PATH=$qtPrefix", "-DCOLOSSEUM_UPDATE_TESTING=OFF"
        )
        Invoke-CMakeChecked @("--build", $build, "--target", "colosseum")
    }
    catch {
        Write-Error "FAILED TO RESTORE SHIPPING BUILD: $_"
        throw
    }
    $env:Path = $previousPath
    if ($null -eq $previousTesting) { Remove-Item Env:COLOSSEUM_UPDATE_TESTING -ErrorAction SilentlyContinue }
    else { $env:COLOSSEUM_UPDATE_TESTING = $previousTesting }
}
