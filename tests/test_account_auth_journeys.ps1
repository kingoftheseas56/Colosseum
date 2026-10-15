param(
    [string]$Lanista = "",
    [string]$Exe = "",
    [string]$Qml = "",
    [switch]$SkipLive = $false
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot

function Fail([string]$message) {
    Write-Host "FAIL: $message"
    exit 1
}

function Read-RepoFile([string]$relativePath) {
    Get-Content -Raw -LiteralPath (Join-Path $root $relativePath)
}

function Invoke-Checked([scriptblock]$action, [string]$label) {
    & $action
    if ($LASTEXITCODE -ne 0) {
        Fail "$label failed with exit code $LASTEXITCODE"
    }
}

function Manifest-FromOutput([string]$output) {
    $match = [regex]::Match($output, '\(manifest:\s*(.+?session\.json)\)')
    if (-not $match.Success) { Fail "Lanista output did not expose a session manifest" }
    $path = $match.Groups[1].Value.Trim()
    if (-not [System.IO.Path]::IsPathRooted($path)) { $path = Join-Path $root $path }
    if (-not (Test-Path -LiteralPath $path)) { Fail "Lanista manifest missing: $path" }
    return [System.IO.Path]::GetFullPath($path)
}

Write-Host "[account-auth] phase 1: static/service contracts"
$required = @(
    'tests/mock-account-service/server.mjs',
    'tests/lanista_scenarios/account_local_device_happy_path.json',
    'tests/lanista_scenarios/account_create_happy_path.json',
    'tests/lanista_scenarios/account_signin_happy_path.json',
    'qml/account/AccountFlyout.qml',
    'qml/account/AccountCreate.qml',
    'qml/account/AccountSignIn.qml',
    'tests/test_account_lanista_selector_contract.ps1'
)
foreach ($relative in $required) {
    if (-not (Test-Path -LiteralPath (Join-Path $root $relative))) {
        Fail "required file missing: $relative"
    }
}

$goHandler = Read-RepoFile 'server/account-service/internal/httpserver/account_handlers.go'
if (-not $goHandler.Contains('json:"builtin_avatar_id,omitempty"')) {
    Fail 'production account response is not pinned to builtin_avatar_id'
}
$mockText = Read-RepoFile 'tests/mock-account-service/server.mjs'
$mockMatch = [regex]::Match($mockText, '(?s)function encodeAccount\(account\)\s*\{.*?\n\}')
if (-not $mockMatch.Success) { Fail 'mock encodeAccount function not found' }
$mockAccount = $mockMatch.Value
if (-not $mockAccount.Contains('builtin_avatar_id: account.avatarId')) {
    Fail 'mock account response does not emit builtin_avatar_id'
}
if ([regex]::IsMatch($mockAccount, '(?m)^\s*avatar_id\s*:')) {
    Fail 'mock account response still emits stale avatar_id'
}

Push-Location $root
try {
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests/test_account_lanista_selector_contract.ps1
    if ($LASTEXITCODE -ne 0) { Fail 'selector contract failed' }

    & node tests/mock-account-service/server.mjs --selftest
    if ($LASTEXITCODE -ne 0) { Fail 'mock account service self-test failed' }

    Push-Location server/account-service
    try {
        & go test ./... -count=1
        if ($LASTEXITCODE -ne 0) { Fail 'production account service tests failed' }
    } finally { Pop-Location }
} finally { Pop-Location }

if ($SkipLive) {
    Write-Host 'ACCOUNT_AUTH_STATIC_OK'
    exit 0
}

if (-not $Lanista) { $Lanista = Join-Path $root 'native/build-msvc/lanista.exe' }
if (-not $Exe) { $Exe = Join-Path $root 'native/build-msvc/colosseum.exe' }
if (-not $Qml) { $Qml = Join-Path $root 'qml/Main.qml' }
foreach ($path in @($Lanista, $Exe, $Qml)) {
    if (-not (Test-Path -LiteralPath $path)) { Fail "live dependency missing: $path" }
}

# Real Colosseum launches need the same runtime closure as the repository's
# other Windows app probes. Keep this wrapper independent of the caller shell.
$runtimeBins = @(
    'C:\Qt\6.11.1\msvc2022_64\bin',
    'C:\tools\mpvqt-feasibility\mpvqt-msvc-install\bin',
    'C:\tools\mpvqt-feasibility\libmpv-prefix\bin'
)
foreach ($runtimeBin in $runtimeBins) {
    if (-not (Test-Path -LiteralPath $runtimeBin)) {
        Fail "live runtime directory missing: $runtimeBin"
    }
}
$optionalFfmpegBin = 'C:\tools\ffmpeg-master-latest-win64-gpl-shared\bin'
if (Test-Path -LiteralPath $optionalFfmpegBin) { $runtimeBins += $optionalFfmpegBin }
$env:PATH = (($runtimeBins + @($env:PATH)) -join ';')

$webpPlugin = Join-Path (Split-Path -Parent $Exe) 'imageformats/qwebp.dll'
if (-not (Test-Path -LiteralPath $webpPlugin)) {
    Fail "WebP runtime plugin missing: $webpPlugin (deploy the Colosseum runtime first)"
}

function Run-Scenario([string]$scenario, [string]$tag) {
    $scenarioPath = Join-Path $root $scenario
    Write-Host "[account-auth] Lanista: $scenario (tag=$tag)"
    Push-Location $root
    try {
        $lines = & $Lanista session run $scenarioPath --exe $Exe --qml $Qml `
            --tag $tag --drive --ready-ms 90000 2>&1
        $exitCode = $LASTEXITCODE
    } finally { Pop-Location }
    $output = ($lines | Out-String)
    Write-Host $output
    if ($exitCode -ne 0) { Fail "$scenario exited $exitCode" }
    $manifestPath = Manifest-FromOutput $output
    return [pscustomobject]@{
        Scenario = $scenario
        Tag = $tag
        Output = $output
        ManifestPath = $manifestPath
        Manifest = (Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json)
    }
}

function Run-WarningGate($run) {
    $logs = @()
    $appLog = Join-Path ([string]$run.Manifest.appDataRoot) 'logs/colosseum.log'
    if (Test-Path -LiteralPath $appLog) { $logs += $appLog }
    $stderr = [string]$run.Manifest.stderrPath
    if ($stderr -and (Test-Path -LiteralPath $stderr)) { $logs += $stderr }
    if ($logs.Count -eq 0) { Fail "no warning-gate logs for $($run.Scenario)" }
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root 'tests/warning_gate.ps1') `
        -LogPath ($logs -join ',')
    if ($LASTEXITCODE -ne 0) { Fail "warning gate failed for $($run.Scenario)" }
}

$runId = [guid]::NewGuid().ToString('N').Substring(0, 10)
$mock = $null
$hadServiceUrl = Test-Path Env:COLOSSEUM_ACCOUNT_SERVICE_URL
$oldServiceUrl = $env:COLOSSEUM_ACCOUNT_SERVICE_URL

try {
    Write-Host '[account-auth] phase 2: backend-free local-device proof'
    Remove-Item Env:COLOSSEUM_ACCOUNT_SERVICE_URL -ErrorAction SilentlyContinue

    $localTag = "account-local-$runId"
    $localRun = Run-Scenario 'tests/lanista_scenarios/account_local_device_happy_path.json' $localTag
    if ([string]$localRun.Manifest.tag -ne $localTag) { Fail 'local-device manifest tag mismatch' }
    if (-not [bool]$localRun.Manifest.drive) {
        Fail 'Lanista drive gate was not enabled for local-device session'
    }
    Run-WarningGate $localRun

    $listener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, 0)
    $listener.Start()
    $port = ([System.Net.IPEndPoint]$listener.LocalEndpoint).Port
    $listener.Stop()
    $mockOut = Join-Path $env:TEMP "colosseum-account-mock-$runId.out.log"
    $mockErr = Join-Path $env:TEMP "colosseum-account-mock-$runId.err.log"

    Write-Host "[account-auth] phase 3: start disposable mock on 127.0.0.1:$port"
    $mock = Start-Process node -ArgumentList @(
        'tests/mock-account-service/server.mjs', '--port', $port
    ) -WorkingDirectory $root -PassThru `
        -RedirectStandardOutput $mockOut -RedirectStandardError $mockErr

    $ready = $false
    $deadline = (Get-Date).AddSeconds(10)
    while ((Get-Date) -lt $deadline -and -not $ready) {
        try {
            $response = Invoke-WebRequest -UseBasicParsing -Uri "http://127.0.0.1:$port/healthz" -TimeoutSec 1
            $ready = $response.StatusCode -eq 200
        } catch { Start-Sleep -Milliseconds 100 }
    }
    if (-not $ready) { Fail "mock did not become healthy; stderr: $(Get-Content $mockErr -Raw -ErrorAction SilentlyContinue)" }

    $env:COLOSSEUM_ACCOUNT_SERVICE_URL = "http://127.0.0.1:$port"
    $createTag = "account-create-$runId"
    $signinTag = "account-signin-$runId"
    $createRun = Run-Scenario 'tests/lanista_scenarios/account_create_happy_path.json' $createTag
    $signinRun = Run-Scenario 'tests/lanista_scenarios/account_signin_happy_path.json' $signinTag

    if ([string]$createRun.Manifest.tag -ne $createTag) { Fail 'create manifest tag mismatch' }
    if ([string]$signinRun.Manifest.tag -ne $signinTag) { Fail 'sign-in manifest tag mismatch' }
    if (-not [bool]$createRun.Manifest.drive -or -not [bool]$signinRun.Manifest.drive) {
        Fail 'Lanista drive gate was not enabled for both account sessions'
    }

    $appDataRoots = @(
        [string]$localRun.Manifest.appDataRoot,
        [string]$createRun.Manifest.appDataRoot,
        [string]$signinRun.Manifest.appDataRoot
    )
    if (($appDataRoots | Sort-Object -Unique).Count -ne 3) {
        Fail 'local, create, and sign-in sessions must use three isolated AppData roots'
    }

    $pipes = @(
        [string]$localRun.Manifest.pipe,
        [string]$createRun.Manifest.pipe,
        [string]$signinRun.Manifest.pipe
    )
    if (($pipes | Sort-Object -Unique).Count -ne 3) {
        Fail 'local, create, and sign-in sessions must use three isolated named pipes'
    }

    Write-Host '[account-auth] phase 4: account warning gates'
    Run-WarningGate $createRun
    Run-WarningGate $signinRun

    Write-Host ("LOCAL_DEVICE_MANIFEST=" + $localRun.ManifestPath)
    Write-Host ("CREATE_MANIFEST=" + $createRun.ManifestPath)
    Write-Host ("SIGNIN_MANIFEST=" + $signinRun.ManifestPath)
    Write-Host 'ACCOUNT_AUTH_JOURNEYS_OK'
} finally {
    if ($hadServiceUrl) {
        $env:COLOSSEUM_ACCOUNT_SERVICE_URL = $oldServiceUrl
    } else {
        Remove-Item Env:COLOSSEUM_ACCOUNT_SERVICE_URL -ErrorAction SilentlyContinue
    }
    if ($mock -and -not $mock.HasExited) {
        Stop-Process -Id $mock.Id -Force -ErrorAction SilentlyContinue
        Wait-Process -Id $mock.Id -Timeout 5 -ErrorAction SilentlyContinue
    }
}
