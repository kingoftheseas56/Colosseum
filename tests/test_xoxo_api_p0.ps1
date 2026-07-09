$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$qmlExe = "C:\Qt\6.11.1\msvc2022_64\bin\qml.exe"
if (!(Test-Path $qmlExe)) { throw "qml.exe not found at $qmlExe - update the Qt path in this test." }

# qml.exe's file:// XMLHttpRequest returns empty under -platform offscreen (async hangs,
# sync yields length 0), so the parser harness can't read fixtures at runtime. Instead we
# GENERATE a .pragma-library JS file embedding each captured fixture as a JSON-escaped
# string (ConvertTo-Json = bulletproof escaping), which the harness .imports. Ephemeral —
# regenerated from the committed .html fixtures each run, deleted after.
$fixDir = Join-Path $PSScriptRoot "fixtures/xoxo"
$genPath = Join-Path $PSScriptRoot "xoxo_fixtures.gen.js"
$names = @("search.html", "series_p1.html", "series_p2.html", "issue_all.html", "genre.html", "homepage.html")
$sb = New-Object System.Text.StringBuilder
[void]$sb.AppendLine(".pragma library")
[void]$sb.AppendLine("var _d = {};")
foreach ($n in $names) {
    $path = Join-Path $fixDir $n
    if (!(Test-Path $path)) { throw "fixture missing: $path (run the Task 1 curl block to capture it)" }
    # [IO.File]::ReadAllText returns a PLAIN .NET string; Get-Content -Raw decorates the
    # string with PS metadata (PSPath/PSDrive/…) which ConvertTo-Json would serialize as an
    # object instead of a bare string literal.
    $raw = [System.IO.File]::ReadAllText($path)
    $json = $raw | ConvertTo-Json   # emits a valid JS/JSON string literal, fully escaped
    [void]$sb.AppendLine("_d[" + ($n | ConvertTo-Json) + "] = " + $json + ";")
}
[void]$sb.AppendLine("function get(n) { return _d[n] || ''; }")
Set-Content -Path $genPath -Value $sb.ToString() -Encoding UTF8

try {
    $harness = Join-Path $PSScriptRoot "xoxo_api_harness.qml"
    # Verdict rides the exit code (Qt.exit(0) pass / non-zero fail).
    $out = cmd /c "`"$qmlExe`" -platform offscreen `"$harness`" 2>&1" | Out-String
    if ($LASTEXITCODE -ne 0) { throw "xoxo api harness failed (exit $LASTEXITCODE):`n$out" }
    Write-Host "test_xoxo_api_p0 PASS"
} finally {
    Remove-Item -Path $genPath -ErrorAction SilentlyContinue
}
