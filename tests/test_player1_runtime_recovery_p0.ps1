$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$player = Get-Content (Join-Path $root "qml/PlayerPage.qml") -Raw

function Assert-Matches($text, $pattern, $message) {
    if ($text -notmatch $pattern) { throw $message }
}
function Assert-NotMatches($text, $pattern, $message) {
    if ($text -match $pattern) { throw $message }
}

$watchdog = [regex]::Match($player, 'function\s+handleStreamWatchdog\(\)[\s\S]*?function\s+hasAdjacentEpisode').Value
Assert-Matches $watchdog 'if\s*\(!root\.starting\)' `
    "Startup watchdog must remain armed until real position advancement retires starting."
Assert-NotMatches $watchdog '!root\.starting\s*\|\|\s*root\.fileReady' `
    "FILE_LOADED must not short-circuit the startup watchdog."

$recovery = [regex]::Match($player, 'function\s+tickRecoveryWatch\(\)[\s\S]*?function\s+tickWakeReconnect').Value
Assert-Matches $recovery 'mpv\.decodedWidth' `
    "Black/frozen recovery must consume decoded-frame width truth."
Assert-Matches $recovery 'mpv\.decodedHeight' `
    "Black/frozen recovery must consume decoded-frame height truth."
Assert-NotMatches $recovery 'mpv\.mpvProperty\("(?:width|height)"\)' `
    "Recovery must not mistake container dimensions for decoded-frame truth."
$failure = [regex]::Match($player, 'function\s+handlePlaybackFailure\(reason\)[\s\S]*?function\s+handleStreamWatchdog').Value
Assert-Matches $failure 'markStreamDead\(root\.currentStreamIndex[\s\S]*?pickAnotherStream\(\)' `
    "A candidate that fails its retry must be retired before alternate selection."

$resume = [regex]::Match($player, 'function\s+armPlaybackAfterResumeChoice\(label,\s*seekSec\)[\s\S]*?function\s+stop\(\)').Value
Assert-Matches $resume 'root\.starting\s*=\s*true[\s\S]*?mpv\.seekExact\(seekSec\)' `
    "Resume choice must re-arm startup proof before its seek can emit position changes."
Assert-Matches $resume 'root\.resetRecoveryWatch\(\)' `
    "Resume choice must reset recovery clocks before playback resumes."
Assert-Matches $resume 'streamWatchdog\.restart\(\)' `
    "Resume choice must restart bounded startup failure detection."
Assert-Matches $resume 'function\s+acceptResumeChoice[\s\S]*?armPlaybackAfterResumeChoice' `
    "Resume action must use the re-armed start path."
Assert-Matches $resume 'function\s+startOverFromResumeChoice[\s\S]*?armPlaybackAfterResumeChoice' `
    "Start-over action must use the re-armed start path."

Write-Host "PASS: Function 0009 Player 1 runtime recovery contract holds."
