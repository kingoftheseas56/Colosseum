# Orchestration seam contract (Task 14). Player 2 asks the host (Player2HostServices) for episodes,
    # Strip line comments ONLY where the "//" is genuinely a comment. A naive s|//.*|| also erases
    # real code - `"qrc:///qml/PlayerPage.qml"`, any https:// URL - which turned this gate into a
    # false negative (cross-model review, 2026-07-26: it PASSED files that referenced production in
    # a string literal). So walk each line and honour quotes: a "//" inside a string is code.
    function Remove-QmlComments([string]$Source) {
        $out = New-Object System.Text.StringBuilder
        foreach ($line in ($Source -split "`n")) {
            $inSingle = $false; $inDouble = $false; $cut = -1
            for ($i = 0; $i -lt $line.Length; $i++) {
                $ch = $line[$i]
                $prev = if ($i -gt 0) { $line[$i - 1] } else { [char]0 }
                if ($ch -eq "'" -and -not $inDouble -and $prev -ne '`\`') { $inSingle = -not $inSingle; continue }
                if ($ch -eq '"' -and -not $inSingle -and $prev -ne '`\`') { $inDouble = -not $inDouble; continue }
                if (-not $inSingle -and -not $inDouble -and $ch -eq '/' -and $i + 1 -lt $line.Length -and $line[$i + 1] -eq '/') { $cut = $i; break }
            }
            $kept = if ($cut -ge 0) { $line.Substring(0, $cut) } else { $line }
            [void]$out.AppendLine($kept)
        }
        return $out.ToString()
    }

# sources, subtitles, skip segments, downloads, metadata and progress, and renders whatever it
# resolves — it must NEVER reach a production catalogue / stream source / download store / net client
# itself, in EITHER the engine (native/player2) or the chrome (qml/player2). This is what keeps Player 2
# swappable behind any host. Grep-based (shape, not behaviour); comments count too, so the boundary
# reads clean end to end.
# Run: powershell -NoProfile -File tests/player2/player2_orchestration_contract.ps1

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$roots = @(
    (Join-Path $root 'native/player2'),
    (Join-Path $root 'qml/player2')
)

# Production surfaces the isolated player must not name. The host owns all of these.
$forbidden = @(
    'Cinemeta', 'Torrentio', 'TheatreApi', 'StremioService', 'AddonClient',
    'DownloadStore', 'MangaReader', 'PlayerPage', '\bMpvItem\b', 'XMLHttpRequest'
)

$files = Get-ChildItem -Path $roots -Recurse -Include *.cpp,*.h,*.qml,*.js -File |
    Where-Object { $_.FullName -notmatch '[\\/]build-player2' }
$violations = @()
foreach ($file in $files) {
    $text = Get-Content -Raw $file.FullName
    # Comments stripped before the forbidden-surface scan, for the same reason the shell contract
    # does it: the rule is that Player 2 must not TOUCH production surfaces, and a comment citing
    # the shipped player's file and line is parity provenance - the record of what a copied element
    # was copied FROM. A real use is code and still fails here.
    $text = Remove-QmlComments ([regex]::Replace($text, '(?s)/\*.*?\*/', ''))
    foreach ($pattern in $forbidden) {
        if ($text -match $pattern) {
            $violations += "$($file.Name): names production surface '$pattern' (the host owns it, not Player 2)"
        }
    }
}

# The seam itself must exist — the one door the player talks to.
if (-not (Test-Path (Join-Path $root 'native/player2/host/Player2HostServices.h'))) {
    $violations += 'Player2HostServices.h missing: the orchestration seam is the only allowed door'
}

if ($violations.Count -gt 0) {
    $violations | ForEach-Object { Write-Error $_ -ErrorAction Continue }
    throw "player2_orchestration_contract: FAIL ($($violations.Count) violation(s))"
}
Write-Output "player2_orchestration_contract: PASS ($($files.Count) player2 files, host-seam doctrine upheld)"
