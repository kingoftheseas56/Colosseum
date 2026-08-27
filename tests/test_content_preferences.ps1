$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot

function Assert-Contains($hay, $needle, $why) {
    if ($hay -notlike "*$needle*") { throw "MISSING [$why]: expected to find -> $needle" }
}

# ---- (a) persistence: run the ContentPreferences QML harness offscreen ----
# qml.exe is a GUI-subsystem exe: its console.log never reaches redirected stdout unless
# QT_FORCE_STDERR_LOGGING forces it to stderr, so we set that and capture the merged stream,
# then gate on BOTH the OK marker AND a clean exit (0).
$qmlExe = "C:\Qt\6.11.1\msvc2022_64\bin\qml.exe"
if (!(Test-Path -LiteralPath $qmlExe)) { throw "qml.exe not found at $qmlExe" }

# fixed temp INI — deleted before AND after so the run starts clean and leaves no residue
$iniPath = Join-Path $env:TEMP "colosseum_content_prefs_test.ini"
Remove-Item -LiteralPath $iniPath -ErrorAction SilentlyContinue

$env:QT_FORCE_STDERR_LOGGING = "1"
$harness = Join-Path $root "tests\content_preferences_harness.qml"
$qmlInc  = Join-Path $root "qml"
$output  = cmd /c "`"$qmlExe`" -platform offscreen -I `"$qmlInc`" `"$harness`" 2>&1" | Out-String
$code    = $LASTEXITCODE

Write-Host $output

if ($code -ne 0 -or $output -notlike "*CONTENT_PREFERENCES_OK*") {
    Remove-Item -LiteralPath $iniPath -ErrorAction SilentlyContinue
    throw "content preferences persistence harness failed (exit $code):`n$output"
}
Remove-Item -LiteralPath $iniPath -ErrorAction SilentlyContinue

# ---- (b) static shell + contract wiring ----
$taskbar = Get-Content -Raw (Join-Path $root "qml\Taskbar.qml")
$main    = Get-Content -Raw (Join-Path $root "qml\Main.qml")
$page    = Get-Content -Raw (Join-Path $root "qml\SettingsPage.qml")

Assert-Contains $taskbar 'signal settingsClicked()' 'taskbar exposes settings door'
Assert-Contains $taskbar '../assets/icons/preferences.svg' 'distinct sliders icon (not the wallpaper gear) for global settings'
Assert-Contains $main 'id: contentPreferences' 'one global preference instance'
Assert-Contains $main 'source: "SettingsPage.qml"' 'settings surface is host-owned'
Assert-Contains $page 'Show sexually explicit titles across Theatre, Tankoban, and Biblio.' 'locked helper copy'

Write-Host "content preferences OK"
exit 0
