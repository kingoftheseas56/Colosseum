$ErrorActionPreference = 'Stop'
$gate = Join-Path $PSScriptRoot 'mpv_zero_drop_gate.ps1'

function Invoke-Validation([string]$json) {
    $jsonFile = Join-Path ([System.IO.Path]::GetTempPath()) `
        ("colosseum-mpv-probe-" + [guid]::NewGuid().ToString('N') + '.json')
    try {
        [System.IO.File]::WriteAllText($jsonFile, $json)
        $savedErrorAction = $ErrorActionPreference
        $ErrorActionPreference = 'Continue'
        try {
            $output = & powershell -NoProfile -ExecutionPolicy Bypass -File $gate `
                -ValidateResultJson $jsonFile -MeasureSeconds 10 2>&1 | Out-String
        }
        finally {
            $ErrorActionPreference = $savedErrorAction
        }
        return [pscustomobject]@{
            ExitCode = $LASTEXITCODE
            Output = $output
        }
    }
    finally {
        [System.IO.File]::Delete($jsonFile)
    }
}

function Require-Pass([string]$name, [string]$json) {
    $run = Invoke-Validation $json
    if ($run.ExitCode -ne 0 -or $run.Output -notmatch 'MPV ZERO DROP RESULT VALIDATION: PASS') {
        throw "$name should pass validation; exit=$($run.ExitCode) output=$($run.Output)"
    }
}

function Require-Fail([string]$name, [string]$json, [string]$messagePattern) {
    $run = Invoke-Validation $json
    if ($run.ExitCode -eq 0) {
        throw "$name should fail validation"
    }
    if ($run.Output -notmatch $messagePattern) {
        throw "$name failed for the wrong reason: $($run.Output)"
    }
}

# --- -QmlEntry forwarding is verified WITHOUT launching a process, via the gate's
# --- -ResolveQmlEntry side-mode (same no-launch pattern as -ValidateResultJson).
function Invoke-EntryResolution([string]$qmlEntry) {
    $savedErrorAction = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        if ([string]::IsNullOrWhiteSpace($qmlEntry)) {
            $output = & powershell -NoProfile -ExecutionPolicy Bypass -File $gate `
                -ResolveQmlEntry 2>&1 | Out-String
        } else {
            $output = & powershell -NoProfile -ExecutionPolicy Bypass -File $gate `
                -ResolveQmlEntry -QmlEntry $qmlEntry 2>&1 | Out-String
        }
    }
    finally {
        $ErrorActionPreference = $savedErrorAction
    }
    return [pscustomobject]@{
        ExitCode = $LASTEXITCODE
        Output = $output
    }
}

function Require-Entry([string]$name, [string]$qmlEntry, [string]$expected) {
    $run = Invoke-EntryResolution $qmlEntry
    $line = ($run.Output -split "`r?`n" | Where-Object { $_ -like 'QML_ENTRY=*' } | Select-Object -First 1)
    if ([string]::IsNullOrEmpty($line)) {
        throw "$name did not emit a QML_ENTRY line; exit=$($run.ExitCode) output=$($run.Output)"
    }
    $resolved = $line.Substring('QML_ENTRY='.Length)
    if ($resolved -cne $expected) {
        throw "$name resolved QmlEntry '$resolved', expected '$expected'"
    }
}

$valid = '{"decoderStart":4,"decoderEnd":4,"decoderDelta":0,"outputStart":2,"outputEnd":2,"outputDelta":0,"hwdec":"d3d11va-copy","avsyncStart":0.001,"avsyncEnd":-0.002,"positionStart":20.0,"positionEnd":29.1,"videoSync":"display-resample","interpolation":true}'
Require-Pass 'valid zero-drop result with sufficient progress' $valid

Require-Fail 'missing counter' `
    '{"decoderEnd":4,"outputStart":2,"outputEnd":2,"hwdec":"d3d11va-copy","avsyncStart":0.001,"avsyncEnd":0.002,"positionStart":20.0,"positionEnd":29.1,"videoSync":"display-resample","interpolation":true}' `
    'decoderStart.*missing or null'

Require-Fail 'null counter' `
    '{"decoderStart":4,"decoderEnd":4,"outputStart":null,"outputEnd":2,"hwdec":"d3d11va-copy","avsyncStart":0.001,"avsyncEnd":0.002,"positionStart":20.0,"positionEnd":29.1,"videoSync":"display-resample","interpolation":true}' `
    'outputStart.*missing or null'

Require-Fail 'nonzero drop' `
    '{"decoderStart":4,"decoderEnd":5,"outputStart":2,"outputEnd":2,"hwdec":"d3d11va-copy","avsyncStart":0.001,"avsyncEnd":0.002,"positionStart":20.0,"positionEnd":29.1,"videoSync":"display-resample","interpolation":true}' `
    'dropped frames: decoder=1, output=0'

Require-Fail 'stalled playback' `
    '{"decoderStart":4,"decoderEnd":4,"outputStart":2,"outputEnd":2,"hwdec":"d3d11va-copy","avsyncStart":0.001,"avsyncEnd":0.002,"positionStart":20.0,"positionEnd":28.9,"videoSync":"display-resample","interpolation":true}' `
    'insufficient playback progress'

Require-Fail 'invalid avsync' `
    '{"decoderStart":4,"decoderEnd":4,"outputStart":2,"outputEnd":2,"hwdec":"d3d11va-copy","avsyncStart":"Infinity","avsyncEnd":0.002,"positionStart":20.0,"positionEnd":29.1,"videoSync":"display-resample","interpolation":true}' `
    'avsyncStart.*finite number'

# --- -QmlEntry: the default full-app entry is preserved and the optional probe entry is
# --- forwarded safely (blank falls back to the default; forward slashes normalize to back
# --- slashes, matching the existing 'qml\Main.qml' argument form on Windows).
Require-Entry 'default QmlEntry is qml/Main.qml' '' 'qml\Main.qml'
Require-Entry 'explicit default forwards unchanged' 'qml/Main.qml' 'qml\Main.qml'
Require-Entry 'optional probe entry forwarded with slash normalization' `
    'tests/mpv_qtquick_tenet_probe.qml' 'tests\mpv_qtquick_tenet_probe.qml'
Require-Entry 'blank QmlEntry falls back to the default (safe)' '   ' 'qml\Main.qml'

Write-Output 'test_mpv_zero_drop_gate_parser: PASS'
