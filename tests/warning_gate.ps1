# tests/warning_gate.ps1 - the warning verdict gate (Slice W0, 2026-08-12).
#
# A journey or session run FAILS on an unsuppressed Qt warning/critical/fatal and passes on
# info - warnings stop being incidental noise only a human notices. Runner-side parser, no
# app change, no second qInstallMessageHandler (native/engine/AppLog.cpp already chains).
#
# NOTE: this file is plain ASCII on purpose (no em-dashes/smart quotes). Windows PowerShell
# 5.1 reads a BOM-less .ps1 using the system ANSI codepage; a multi-byte UTF-8 character
# (an em-dash included) gets misdecoded and can silently corrupt a later string literal's
# terminator, producing wrong behavior with NO parse error at all (ground-truthed empirically
# 2026-08-12 while building this file - a stray em-dash swallowed several statements into an
# unterminated string and the gate silently always passed). Keep this file ASCII-only.
#
# Sources it reads (both are honest - read the ground-truth pin before touching this file):
#   - <sessionRoot>/logs/colosseum.log - AppLog's own leveled, timestamped record:
#     "yyyy-MM-dd HH:mm:ss.zzz [D|I|W|C|F] message". Level-classified and authoritative:
#     a [W]/[C]/[F] line is offending unless an allowlist pattern matches it; [D]/[I] lines
#     (and multi-line continuations / the session-start banner, which carry no level marker
#     at all) are never offending and are folded into the "known safe" text set below.
#   - the runner's stderr.log - Qt's console mirror PLUS raw third-party writes AppLog never
#     sees at all (e.g. "Cannot load nvcuda.dll" from the CUDA probe - confirmed empirically
#     2026-08-12 to appear in stderr.log only, never in colosseum.log). This stream carries
#     NO level marker, so a stderr.log line is offending UNLESS it is either (a) allowlisted,
#     or (b) text-identical to a line colosseum.log already proved was [D]/[I]/continuation -
#     the cross-reference is exact-text, not fuzzy, so a genuinely new stderr-only line always
#     surfaces rather than silently riding along with known chatter.
#
# House sentinel contract (docs/colosseum-lanista-verification.md / -test-verification.md):
# stdout sentinel + exit code, so this composes with every existing .ps1 runner.
#   exit 0  "WARNING_GATE_OK"      - no offending line found.
#   exit 1  "FAIL: <line>" (one per offending line, all printed - never averaged away)
#   exit 2  "FAIL: <schema msg>"   - the allowlist itself is malformed (missing pattern/
#           owner/reason/date on some entry, or not valid JSON). A malformed suppression
#           entry is REJECTED, never silently honored - see tests/lanista-warning-allowlist.json.
#
# Usage:
#   pwsh tests/warning_gate.ps1 -LogPath <sessionRoot>/logs/colosseum.log -LogPath <runDir>/stderr.log
#   pwsh tests/warning_gate.ps1 -LogPath <path> -AllowlistPath <path>   # override for testing

param(
    [Parameter(Mandatory = $true)]
    [string[]]$LogPath,

    [string]$AllowlistPath
)

$ErrorActionPreference = "Stop"

# Defensive normalization: some callers (notably a shell that has already collapsed a
# quoted argument before PowerShell ever sees it) end up passing one array element that
# itself contains commas instead of a true multi-element array. Split defensively so
# -LogPath "a,b" and -LogPath "a","b" behave identically either way.
$LogPath = @($LogPath | ForEach-Object { $_ -split ',' } | Where-Object { $_ })
$root = Split-Path -Parent $PSScriptRoot
if (-not $AllowlistPath) {
    $AllowlistPath = Join-Path $root "tests/lanista-warning-allowlist.json"
}

# ---- 1. load + validate the allowlist. Schema: [{pattern, owner, reason, date}, ...].
#      An entry missing pattern/owner/reason/date is invalid BY SCHEMA and the whole gate
#      run is refused (exit 2) rather than silently dropping just that one bad entry - a
#      malformed suppression is worse than none, because it hides itself too. ----
$allowPatterns = @()
if (Test-Path -LiteralPath $AllowlistPath) {
    $raw = Get-Content -LiteralPath $AllowlistPath -Raw
    if ([string]::IsNullOrWhiteSpace($raw)) {
        $entries = @()
    } else {
        try {
            $parsed = $raw | ConvertFrom-Json
        } catch {
            Write-Host "FAIL: allowlist $AllowlistPath is not valid JSON: $($_.Exception.Message)"
            exit 2
        }
        if ($null -eq $parsed) { $entries = @() } else { $entries = @($parsed) }
    }
    for ($i = 0; $i -lt $entries.Count; $i++) {
        $e = $entries[$i]
        $missing = @()
        foreach ($field in @("pattern", "owner", "reason", "date")) {
            $has = $e.PSObject.Properties.Name -contains $field
            if (-not $has -or [string]::IsNullOrWhiteSpace([string]$e.$field)) {
                $missing += $field
            }
        }
        if ($missing.Count -gt 0) {
            $ident = if ($e.PSObject.Properties.Name -contains "pattern") { $e.pattern } else { "<no pattern>" }
            Write-Host "FAIL: allowlist entry $i ($ident) missing required field(s): $($missing -join ', ') - invalid by schema, refusing to run the gate"
            exit 2
        }
        $allowPatterns += [string]$e.pattern
    }
} else {
    Write-Host "FAIL: allowlist not found at $AllowlistPath"
    exit 2
}

function Test-Allowed([string]$line) {
    foreach ($pat in $allowPatterns) {
        if ($line -match $pat) { return $true }
    }
    return $false
}

# ---- 2. read every log, classify. ----
$knownSafe = New-Object 'System.Collections.Generic.HashSet[string]'
$appLogLevelPattern = '^\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3} \[(W|C|F)\]'
$appLogAnyLevelPrefix = '^\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3} \[(D|I|W|C|F)\] '
$offenses = New-Object 'System.Collections.Generic.List[string]'
$seenOffenseText = New-Object 'System.Collections.Generic.HashSet[string]'

# AppLog files first (any path literally named colosseum.log or colosseum.N.log) so their
# [D]/[I] lines populate the known-safe set BEFORE stderr.log is cross-referenced against it.
$appLogPaths = @($LogPath | Where-Object { (Split-Path -Leaf $_) -match '^colosseum(\.\d+)?\.log$' })
$otherPaths  = @($LogPath | Where-Object { -not ((Split-Path -Leaf $_) -match '^colosseum(\.\d+)?\.log$') })

# A vacuous pass is worse than a red: if every path handed to us is missing, there is
# nothing to gate at all - that must not silently print WARNING_GATE_OK.
$foundAny = $false
foreach ($p in $LogPath) { if (Test-Path -LiteralPath $p) { $foundAny = $true } }
if (-not $foundAny) {
    Write-Host "FAIL: none of the given log paths exist - nothing was gated: $($LogPath -join ', ')"
    exit 2
}

foreach ($path in $appLogPaths) {
    if (-not (Test-Path -LiteralPath $path)) {
        Write-Host "(warning_gate: $path not found, skipped)"
        continue
    }
    foreach ($line in (Get-Content -LiteralPath $path)) {
        if ($line -match $appLogLevelPattern) {
            if (-not (Test-Allowed $line)) {
                if ($seenOffenseText.Add($line)) { $offenses.Add("[$path] $line") | Out-Null }
            }
        } else {
            # [D]/[I] lines, the session-start banner, and multi-line continuations (a
            # multi-line qInfo's follow-on lines carry no timestamp/level at all) are safe
            # by construction - AppLog only elevates W/C/F to a bracketed level. Store BOTH
            # the raw line and the message with its timestamp+level prefix stripped, because
            # stderr.log mirrors the same message with NO prefix at all (Qt's own un-leveled
            # console formatting, confirmed empirically 2026-08-12 to differ from AppLog's file
            # format) - the stripped form is what a stderr.log line will actually equal.
            $knownSafe.Add($line) | Out-Null
            $stripped = $line -replace $appLogAnyLevelPrefix, ''
            if ($stripped -ne $line) { $knownSafe.Add($stripped) | Out-Null }
        }
    }
}

foreach ($path in $otherPaths) {
    if (-not (Test-Path -LiteralPath $path)) {
        Write-Host "(warning_gate: $path not found, skipped)"
        continue
    }
    foreach ($line in (Get-Content -LiteralPath $path)) {
        if ([string]::IsNullOrWhiteSpace($line)) { continue }
        if ($knownSafe.Contains($line)) { continue }
        if (Test-Allowed $line) { continue }
        if ($seenOffenseText.Add($line)) { $offenses.Add("[$path] $line") | Out-Null }
    }
}

if ($offenses.Count -gt 0) {
    foreach ($o in $offenses) { Write-Host "FAIL: $o" }
    exit 1
}
Write-Host "WARNING_GATE_OK"
exit 0
