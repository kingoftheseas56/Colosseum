param([switch]$NativeOnly)

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$localH = Get-Content (Join-Path $root 'native/engine/LocalDownloads.h') -Raw
$localC = Get-Content (Join-Path $root 'native/engine/LocalDownloads.cpp') -Raw
$audioH = Get-Content (Join-Path $root 'native/engine/AudiobookDownloader.h') -Raw
$audioC = Get-Content (Join-Path $root 'native/engine/AudiobookDownloader.cpp') -Raw
$bookPage = Get-Content (Join-Path $root 'qml/BiblioBook.qml') -Raw
$downloadsPage = Get-Content (Join-Path $root 'qml/DownloadsPage.qml') -Raw
$main = Get-Content (Join-Path $root 'qml/Main.qml') -Raw
$reader = Get-Content (Join-Path $root 'qml/reader2/ReaderShell.qml') -Raw

function Require-Text($text, $needle, $message) {
    if ($text -notlike "*$needle*") { throw $message }
}

function Reject-Pattern($text, $pattern, $message) {
    if ($text -match $pattern) { throw $message }
}

# Native truth seams.
Require-Text $localH 'dismissFailure' 'LocalDownloads must retain and dismiss terminal non-Theatre failures.'
Require-Text $localH 'Q_INVOKABLE QVariantMap remove' 'LocalDownloads must return deletion success or failure to QML.'
foreach ($field in @('canRetry', 'canPause', 'canResume', 'canPlay', 'canDismiss')) {
    Require-Text $localC "QStringLiteral(`"$field`")" "LocalDownloads active rows are missing $field."
}
Require-Text $audioH 'dismissFailure' 'Audiobook failures must remain visible until dismissed.'
Require-Text $audioH 'bookPath' 'Completed audiobook entries must retain their paired book path.'
Require-Text $audioC 'QStringLiteral("error")' 'Audiobook failure rows must expose their real reason.'
Require-Text $bookPage 'detail.localPath || ""' 'Biblio must pass the local book path into audiobook downloads.'

if ($NativeOnly) {
    Write-Host 'downloads native essentials contract: OK'
    exit 0
}

# Page truth and safety seams.
Require-Text $downloadsPage 'property var downloadsApi' 'DownloadsPage must expose its existing facade through an injectable property.'
Require-Text $downloadsPage 'property var audiobooksApi' 'DownloadsPage must expose its audiobook facade through an injectable property.'
Require-Text $downloadsPage 'Delete local copy' 'Destructive Remove wording must be replaced.'
Require-Text $downloadsPage 'function confirmAction' 'Destructive page actions must pass through one inline confirmation.'
Require-Text $downloadsPage 'openAudiobookRequested' 'Completed audiobooks need the existing-reader route.'
Reject-Pattern $downloadsPage 'text:\s*"Remove"' 'A destructive action is still labelled Remove.'
Reject-Pattern $downloadsPage 'LocalDownloads\.retry\(' 'DownloadsPage must not bypass the row capability gate for Retry.'

# Narrow completed-audiobook route.
Require-Text $main 'routeDownloadedAudiobook' 'Main must route a completed audiobook into its paired book.'
Require-Text $main 'openAudiobookRequested.connect' 'DownloadsPage audiobook open signal is not connected.'
Require-Text $reader 'function openAudioPanel' 'ReaderShell needs the narrow Audio-panel entry point.'

# Arc 41 keyboard-only contract: the old anti-keyboard scope is superseded.
Require-Text $downloadsPage 'KeyboardScrollController' 'Downloads must expose keyboard scrolling.'
Require-Text $downloadsPage 'activeFocusOnTab' 'Downloads must expose keyboard focus entry.'
Require-Text $downloadsPage 'Accessible.' 'Downloads must expose accessible semantics for custom actions.'
Require-Text $downloadsPage 'Keys.onEscapePressed' 'Downloads confirmations must be dismissible with Escape.'
Require-Text $downloadsPage 'KeyboardCollectionController' 'Downloads media rails must support indexed keyboard navigation.'
Require-Text $downloadsPage 'KeyboardAction' 'Downloads pointer actions must converge on semantic keyboard actions.'
Reject-Pattern $downloadsPage 'DownloadsContextMenu|captureState\(|restoreState\(' 'Unrelated framework/state-restoration scope crept into Downloads.'

Write-Host 'downloads essentials contract: OK'

