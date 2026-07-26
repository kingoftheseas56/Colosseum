# Contract for the Player 2 QML shell. It enforces the house doctrine — the shell PAINTS and sends
# typed intent; C++ DECIDES. So the shell must never advance playback position with a Timer, read raw
# FFmpeg/mpv property strings, touch production stores/catalogues, or import the production player.
# Run: powershell -NoProfile -File tests/player2/player2_shell_contract.ps1

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$shellDir = Join-Path $root 'qml/player2'

$required = @(
    'Player2Shell.qml',
    'controls/Player2Icon.qml',
    'controls/SeekBar.qml',
    'controls/TransportBar.qml'
)
foreach ($rel in $required) {
    if (-not (Test-Path (Join-Path $shellDir $rel))) { throw "shell file missing: $rel" }
}

$files = Get-ChildItem -Path $shellDir -Recurse -Filter *.qml
$violations = @()

foreach ($file in $files) {
    $text = Get-Content -Raw $file.FullName
    $name = $file.Name

    # 1. No raw FFmpeg/mpv property or command strings in the UI.
    foreach ($pattern in @('mpvProperty', 'mpv\.command', 'setSubOption', 'libmpv', 'AVCOL_', 'avformat', 'av_')) {
        if ($text -match $pattern) { $violations += "${name}: uses engine string '$pattern' (C++ decides, not QML)" }
    }

    # 2. No production store / catalogue / network access from the isolated shell.
    foreach ($pattern in @('Cinemeta', 'Torrentio', 'XMLHttpRequest', 'DownloadStore', 'TheatreApi', 'StremioService')) {
        if ($text -match $pattern) { $violations += "${name}: touches production surface '$pattern'" }
    }

    # 3. No importing the production player or the mpv item. Checked against the CODE with comments
    #    stripped: the rule is "the isolated shell must not DEPEND on production QML", and a comment
    #    citing `PlayerPage.qml:4611` is the opposite of a dependency - it is the parity provenance
    #    that lets the next brother verify a copied element against its source. Deleting those
    #    citations to satisfy a text grep would make the shell less auditable, not more isolated.
    #    Any real reference (import, instantiation, property access) still fails, because it is code.
    $code = [regex]::Replace($text, '(?m)//.*$', '')
    $code = [regex]::Replace($code, '(?s)/\*.*?\*/', '')
    if ($code -match 'PlayerPage' -or $code -match '\bMpvItem\b') {
        $violations += "${name}: references the production player (PlayerPage/MpvItem)"
    }

    # 4. No Timer that advances playback position. Position is engine-fed and read-only; the shell
    #    must never assign it or drive a scrub from a Timer. Flag any assignment to a `position`
    #    property or a seek issued from inside an onTriggered handler.
    if ($text -match '(?m)position\s*(\+=|-=|=[^=])') {
        $violations += "${name}: assigns playback position in QML (must stay engine-fed)"
    }
    # crude onTriggered-contains-seek check: a Timer handler must not seek.
    $triggers = [regex]::Matches($text, 'onTriggered\s*:\s*\{(?<body>[^}]*)\}')
    foreach ($m in $triggers) {
        if ($m.Groups['body'].Value -match 'seek(Exact|Relative)') {
            $violations += "${name}: a Timer.onTriggered issues a seek (no timer may drive position)"
        }
    }
}

if ($violations.Count -gt 0) {
    $violations | ForEach-Object { Write-Error $_ -ErrorAction Continue }
    throw "player2_shell_contract: FAIL ($($violations.Count) violation(s))"
}

Write-Output "player2_shell_contract: PASS ($($files.Count) shell QML files, typed-intent doctrine upheld)"
