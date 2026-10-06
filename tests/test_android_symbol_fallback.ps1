param([string]$RootOverride = "")
$ErrorActionPreference = "Stop"

$root = if ($RootOverride) { $RootOverride } else { Split-Path -Parent $PSScriptRoot }
$mainPath = Join-Path $root "native\main.cpp"
$cmakePath = Join-Path $root "native\CMakeLists.txt"
$fontPath = Join-Path $root "assets\fonts\Unifont-17.0.05.otf"
$licensePath = Join-Path $root "assets\fonts\Unifont-17.0.05-OFL.txt"

foreach ($path in @($mainPath, $cmakePath, $fontPath, $licensePath)) {
    if (-not (Test-Path -LiteralPath $path)) { throw "MISSING FILE: $path" }
}

$main = Get-Content -LiteralPath $mainPath -Raw
$cmake = Get-Content -LiteralPath $cmakePath -Raw
$license = Get-Content -LiteralPath $licensePath -Raw

function Need([bool]$condition, [string]$message) {
    if (-not $condition) { throw $message }
}

$expectedFontSha256 = "85701ab9b1e251ee16f4df00b13f22eac311d72b7dab427a7d975fe7f5064702"
$actualFontSha256 = (Get-FileHash -LiteralPath $fontPath -Algorithm SHA256).Hash.ToLowerInvariant()
Need ($actualFontSha256 -eq $expectedFontSha256) `
    "Android symbol fallback font bytes must match the coverage-qualified Unifont 17.0.05 artifact."
Need ($license.Contains("SIL OPEN FONT LICENSE Version 1.1")) `
    "Unifont's bundled OFL 1.1 licence must remain beside the font."

$includeMatch = [regex]::Match(
    $main,
    '(?s)#if defined\(Q_OS_ANDROID\)\s*#include <QFontDatabase>\s*#endif'
)
Need ($includeMatch.Success) `
    "QFontDatabase must be included only on the Android compile path."

$runtimeMatch = [regex]::Match(
    $main,
    '(?s)#if defined\(Q_OS_ANDROID\)\s*// Android packages the filtered source-shaped runtime(?<body>.*?)#endif'
)
Need ($runtimeMatch.Success) `
    "Main must retain one Android runtime-materialization block."
$body = $runtimeMatch.Groups["body"].Value

Need ($body.Contains('assets/fonts/Unifont-17.0.05.otf')) `
    "Android boot must load the coverage-qualified symbol font from the materialized runtime."
Need ($body.Contains('QFontDatabase::addApplicationFont(symbolFontPath)')) `
    "Android boot must register the application font."
Need ($body.Contains('QFontDatabase::applicationFontFamilies(symbolFontId)')) `
    "Android boot must ask Qt for the family actually reported by the loaded font."
Need ($body.Contains('QFontDatabase::addApplicationFallbackFontFamily(QChar::Script_Common,')) `
    "Android boot must register the loaded family as the Script_Common fallback."
Need ($body.Contains('Android symbol fallback font registered: family=%s source=%s')) `
    "Android boot must log the reported fallback family and source for device qualification."
Need ($body.Contains('Android symbol fallback font failed to load: source=%s')) `
    "Android boot must log a direct load failure for BLOCKED evidence."

Need (([regex]::Matches($main, 'QFontDatabase::addApplicationFont\(')).Count -eq 1) `
    "There must be exactly one application-font registration."
Need (([regex]::Matches($main, 'QFontDatabase::addApplicationFallbackFontFamily\(')).Count -eq 1) `
    "There must be exactly one application-fallback registration."

Need ($cmake.Contains('foreach(_runtime_dir IN ITEMS assets resources)')) `
    "Android packaging must continue staging the full assets tree into the runtime bundle."
Need ($cmake.Contains('"${_runtime_source_root}/*"')) `
    "Android runtime staging must continue including files added under assets/fonts."

Write-Host "android symbol fallback source contract: PASS"
