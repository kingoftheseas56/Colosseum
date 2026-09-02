$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$reading = Get-Content (Join-Path $root 'qml\MangaReadingRoom.qml') -Raw
$chapter = Get-Content (Join-Path $root 'qml\MangaChapterSeriesView.qml') -Raw
$sharedPath = Join-Path $root 'qml\MangaSeriesSharedHeader.qml'

function Require([bool]$ok, [string]$message) {
    if (-not $ok) { throw "FAIL: $message" }
}

Require (Test-Path $sharedPath) 'shared manga series header component is missing'
$shared = Get-Content $sharedPath -Raw
Require ($reading -match 'MangaSeriesSharedHeader\s*\{') 'Tankoban mode does not use shared header'
Require ($chapter -match 'MangaSeriesSharedHeader\s*\{') 'Chapter mode does not use shared header'
Require ($shared -match 'Tankoban Mode') 'shared header is missing Tankoban Mode'
Require ($shared -match 'Chapter Mode') 'shared header is missing Chapter Mode'
Require ($shared.IndexOf('Tankoban Mode') -lt $shared.IndexOf('Chapter Mode')) 'mode order must be Tankoban then Chapter'
Require ($shared -match 'objectName:\s*"mangaSeriesModeSwitch"') 'shared mode switch lacks stable objectName'
Require ($shared -match 'objectName:\s*"mangaSeriesLibraryButton"') 'Library lacks stable shared-header objectName'
Require ($reading -notmatch 'LibraryButton\s*\{') 'Tankoban mode still owns a separate LibraryButton'
Require ($chapter -notmatch 'LibraryButton\s*\{') 'Chapter mode still owns a separate LibraryButton'
Write-Output 'MANGA_SHARED_HEADER_CONTRACT_OK'