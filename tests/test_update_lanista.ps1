param(
    [ValidateSet("all", "up-to-date", "idle")]
    [string]$Scenario = "all"
)

# Production update wiring gate.
# Builds the explicit test-key configuration, replays the three isolated updater
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
$previousInstalledVersion = $env:COLOSSEUM_UPDATE_TEST_INSTALLED_VERSION
$previousPresentationState = $env:COLOSSEUM_UPDATE_TEST_PRESENTATION_STATE
$previousReceivedBytes = $env:COLOSSEUM_UPDATE_TEST_RECEIVED_BYTES
$previousTotalBytes = $env:COLOSSEUM_UPDATE_TEST_TOTAL_BYTES
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

    # Idle scenario (installed-release chronicle, Slice 4): runs on a SHIPPING
    # build (COLOSSEUM_UPDATE_TESTING unset) with no update fixture and no test
    # presentation state. The bundled production-signed chronicle in the qrc
    # renders the installed 1.1.0 chapters at rest. This must NOT run under the
    # test-key build (the production bundle verifies against the production key).
    if ($Scenario -eq "idle") {
        Remove-Item Env:COLOSSEUM_UPDATE_TESTING -ErrorAction SilentlyContinue
        Remove-Item Env:COLOSSEUM_UPDATE_TEST_INSTALLED_VERSION -ErrorAction SilentlyContinue
        Remove-Item Env:COLOSSEUM_UPDATE_TEST_PRESENTATION_STATE -ErrorAction SilentlyContinue
        Remove-Item Env:COLOSSEUM_UPDATE_TEST_RECEIVED_BYTES -ErrorAction SilentlyContinue
        Remove-Item Env:COLOSSEUM_UPDATE_TEST_TOTAL_BYTES -ErrorAction SilentlyContinue
        Invoke-CMakeChecked @(
            "-S", $native, "-B", $build, "-G", "Ninja",
            "-DCMAKE_MAKE_PROGRAM=$ninja", "-DCMAKE_BUILD_TYPE=Release",
            "-DCMAKE_PREFIX_PATH=$qtPrefix", "-DCOLOSSEUM_UPDATE_TESTING=OFF"
        )
        Invoke-CMakeChecked @("--build", $build, "--target", "colosseum", "lanista")
        if (!(Test-Path -LiteralPath $exe) -or !(Test-Path -LiteralPath $lanista)) {
            throw "shipping colosseum/lanista binaries are missing"
        }
        $sessionDir = Split-Path -Parent $sessionLog
        New-Item -ItemType Directory -Force -Path $sessionDir | Out-Null
        Remove-Item -LiteralPath $sessionLog -Force -ErrorAction SilentlyContinue
        $runStamp = Get-Date -Format "yyyyMMdd-HHmmss"
        $idleTag = "updater-idle-$runStamp"
        $idleScenario = Join-Path $root "tests/lanista_scenarios/update_idle.json"
        # No --seed: the idle scenario has no update fixture; the bundled qrc
        # chronicle is the source. --drive for a real desktop session.
        $output = & $lanista --verbose session run $idleScenario --exe $exe --tag $idleTag --drive 2>&1 | Tee-Object -Variable runOutput | Out-String
        $code = $LASTEXITCODE
        Add-Content -LiteralPath $sessionLog -Value $output
        if ($code -ne 0) { throw "Lanista idle scenario failed: $idleScenario`n$output" }
        $manifest = [regex]::Match($output, 'manifest:\s*(\S+/session\.json)').Groups[1].Value
        if ([string]::IsNullOrWhiteSpace($manifest)) {
            throw "Lanista idle scenario did not report its session manifest: $idleScenario"
        }
        Add-Content -LiteralPath $sessionLog -Value ("{0} => {1}" -f $idleTag, $manifest)
        Write-Host "test_update_lanista: PASS (isolated idle shipping session; paths in $sessionLog)"
        return
    }

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
    $cases = @(
        @{ Scenario = "tests/lanista_scenarios/update_available.json"; Tag = "updater-available-$runStamp"; Seed = "tests/lanista_fixtures/update-available" },
        @{ Scenario = "tests/lanista_scenarios/update_downloading.json"; Tag = "updater-downloading-$runStamp"; Seed = "tests/lanista_fixtures/update-available" },
        @{ Scenario = "tests/lanista_scenarios/update_up_to_date.json"; Tag = "updater-current-$runStamp"; Seed = "tests/lanista_fixtures/update-up-to-date" }
    )
    if ($Scenario -eq "up-to-date") {
        $cases = @($cases | Where-Object { $_.Scenario -eq "tests/lanista_scenarios/update_up_to_date.json" })
    }
    foreach ($case in $cases) {
        $scenarioPath = Join-Path $root $case.Scenario
        $seedPath = Join-Path $root $case.Seed
        if ($case.Tag -like "updater-current-*") {
            $env:COLOSSEUM_UPDATE_TEST_INSTALLED_VERSION = "1.1.1"
            Remove-Item Env:COLOSSEUM_UPDATE_TEST_PRESENTATION_STATE -ErrorAction SilentlyContinue
            Remove-Item Env:COLOSSEUM_UPDATE_TEST_RECEIVED_BYTES -ErrorAction SilentlyContinue
            Remove-Item Env:COLOSSEUM_UPDATE_TEST_TOTAL_BYTES -ErrorAction SilentlyContinue
        } elseif ($case.Tag -like "updater-downloading-*") {
            Remove-Item Env:COLOSSEUM_UPDATE_TEST_INSTALLED_VERSION -ErrorAction SilentlyContinue
            $env:COLOSSEUM_UPDATE_TEST_PRESENTATION_STATE = "Downloading"
            $env:COLOSSEUM_UPDATE_TEST_RECEIVED_BYTES = "224395264"
            $env:COLOSSEUM_UPDATE_TEST_TOTAL_BYTES = "330301440"
        } else {
            Remove-Item Env:COLOSSEUM_UPDATE_TEST_INSTALLED_VERSION -ErrorAction SilentlyContinue
            Remove-Item Env:COLOSSEUM_UPDATE_TEST_PRESENTATION_STATE -ErrorAction SilentlyContinue
            Remove-Item Env:COLOSSEUM_UPDATE_TEST_RECEIVED_BYTES -ErrorAction SilentlyContinue
            Remove-Item Env:COLOSSEUM_UPDATE_TEST_TOTAL_BYTES -ErrorAction SilentlyContinue
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
    Write-Host "test_update_lanista: PASS (isolated $Scenario updater session(s); paths in $sessionLog)"
}
finally {
    Pop-Location -ErrorAction SilentlyContinue
    $env:COLOSSEUM_UPDATE_TESTING = "OFF"
    if ($null -eq $previousInstalledVersion) { Remove-Item Env:COLOSSEUM_UPDATE_TEST_INSTALLED_VERSION -ErrorAction SilentlyContinue }
    else { $env:COLOSSEUM_UPDATE_TEST_INSTALLED_VERSION = $previousInstalledVersion }
    if ($null -eq $previousPresentationState) { Remove-Item Env:COLOSSEUM_UPDATE_TEST_PRESENTATION_STATE -ErrorAction SilentlyContinue }
    else { $env:COLOSSEUM_UPDATE_TEST_PRESENTATION_STATE = $previousPresentationState }
    if ($null -eq $previousReceivedBytes) { Remove-Item Env:COLOSSEUM_UPDATE_TEST_RECEIVED_BYTES -ErrorAction SilentlyContinue }
    else { $env:COLOSSEUM_UPDATE_TEST_RECEIVED_BYTES = $previousReceivedBytes }
    if ($null -eq $previousTotalBytes) { Remove-Item Env:COLOSSEUM_UPDATE_TEST_TOTAL_BYTES -ErrorAction SilentlyContinue }
    else { $env:COLOSSEUM_UPDATE_TEST_TOTAL_BYTES = $previousTotalBytes }
    try {
        Invoke-CMakeChecked @(
            "-S", $native, "-B", $build, "-G", "Ninja",
            "-DCMAKE_MAKE_PROGRAM=$ninja", "-DCMAKE_BUILD_TYPE=Release",
            "-DCMAKE_PREFIX_PATH=$qtPrefix", "-DCOLOSSEUM_UPDATE_TESTING=OFF"
        )
        Invoke-CMakeChecked @("--build", $build, "--target", "colosseum")
        $shippingCache = Get-Content -LiteralPath (Join-Path $build "CMakeCache.txt") -Raw
        if ($shippingCache -notmatch '(?m)^COLOSSEUM_UPDATE_TESTING:BOOL=OFF\r?$') {
            throw "shipping CMake cache is not COLOSSEUM_UPDATE_TESTING=OFF"
        }
        Write-Host "test_update_lanista: shipping cache restored COLOSSEUM_UPDATE_TESTING=OFF"
    }
    catch {
        Write-Error "FAILED TO RESTORE SHIPPING BUILD: $_"
        throw
    }
    $env:Path = $previousPath
    $env:COLOSSEUM_UPDATE_TESTING = "OFF"
}
