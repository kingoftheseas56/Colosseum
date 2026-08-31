# Contract: every comic/manga reader lane must suppress the OS-shell taskbar.
# The lanes are exactly the Loaders wired to the SHARED reader chrome
# (readerMinimizeRequested -> win.minimizeComicReader). Each MUST appear in
# Main.qml's immersiveSurfaceOpen binding, or the taskbar rides in front of that
# reader (comicSeriesLayer regression, Hemanth 2026-07-16: book reader + player
# suppressed it, the LOCG comics reader did not).
param([string]$RootOverride = "")
$ErrorActionPreference = 'Stop'
$root = if ($RootOverride) { $RootOverride } else { Split-Path -Parent $PSScriptRoot }
$main = Get-Content (Join-Path $root 'qml/Main.qml') -Raw

# The immersiveSurfaceOpen binding block (up to the first blank line after it).
$imm = [regex]::Match($main, 'immersiveSurfaceOpen:.*?(?=\r?\n\s*\r?\n)', 'Singleline').Value
if (-not $imm) { throw 'could not locate the immersiveSurfaceOpen binding' }

# Derive every full-screen comic reader host from its semantic minimize wiring.
# Embedded series hosts raise readerMinimizeRequested -> minimizeComicReader;
# the standalone Vault host raises minimizeRequested -> minimizeVaultComic.
$embedded = [regex]::Matches(
    $main,
    'id:\s*(\w+Layer)\b(?:(?!id:\s*\w+Layer\b).)*?readerMinimizeRequested\.connect\(win\.minimizeComicReader\)',
    'Singleline') | ForEach-Object { $_.Groups[1].Value }
$standalone = [regex]::Matches(
    $main,
    'id:\s*(\w+Layer)\b(?:(?!id:\s*\w+Layer\b).)*?minimizeRequested\.connect\(win\.minimizeVaultComic\)',
    'Singleline') | ForEach-Object { $_.Groups[1].Value }
$lanes = @($embedded) + @($standalone) | Select-Object -Unique

if ($lanes.Count -lt 4) {
    throw "expected at least 4 comic reader lanes including standalone Vault, found $($lanes.Count): $($lanes -join ', ')"
}

foreach ($lane in $lanes) {
    if ($imm -notmatch [regex]::Escape($lane)) {
        throw "$lane wires the shared reader chrome but is MISSING from immersiveSurfaceOpen; the taskbar will ride in front of that reader."
    }
}

$joined = $lanes -join ', '
Write-Host "test_taskbar_immersive_readers_p0: PASS ($joined all suppress the taskbar)"
