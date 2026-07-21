$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$required = @(
    'native/player2/player2_harness_main.cpp',
    'native/player2/host/HarnessHostServices.h',
    'native/player2/host/HarnessHostServices.cpp',
    'qml/player2/Harness.qml',
    'qml/player2/Theme.qml',
    'qml/player2/qmldir',
    'player2.bat'
)
foreach ($relative in $required) {
    if (-not (Test-Path (Join-Path $repoRoot $relative))) {
        throw "contract failure: missing $relative"
    }
}

$sources = ($required + 'native/player2/CMakeLists.txt' | ForEach-Object {
    Get-Content -Raw (Join-Path $repoRoot $_)
}) -join "`n"
$main = Get-Content -Raw (Join-Path $repoRoot 'native/player2/player2_harness_main.cpp')
$hostSource = Get-Content -Raw (Join-Path $repoRoot 'native/player2/host/HarnessHostServices.cpp')
$qml = Get-Content -Raw (Join-Path $repoRoot 'qml/player2/Harness.qml')
$bat = Get-Content -Raw (Join-Path $repoRoot 'player2.bat')

function Require-Text([string]$Text, [string]$Pattern, [string]$Message) {
    if ($Text -notmatch $Pattern) { throw "contract failure: $Message" }
}
function Reject-Text([string]$Text, [string]$Pattern, [string]$Message) {
    if ($Text -match $Pattern) { throw "contract failure: $Message" }
}

Require-Text $main 'QQuickWindow::setGraphicsApi\(QSGRendererInterface::Direct3D11\)' 'D3D11 must be selected before window creation'
Require-Text $main 'setOrganizationName\(QStringLiteral\("Colosseum"\)\)' 'lab organization must be explicit'
Require-Text $main 'setApplicationName\(QStringLiteral\("Player2Lab"\)\)' 'lab application identity must be isolated'
Require-Text $main '--scenario' 'synthetic scenario parsing is required'
Require-Text $main '--report' 'deterministic report output is required'
Require-Text $hostSource 'events\.jsonl' 'host events must use an isolated JSONL ledger'
Require-Text $hostSource 'cpuTransfers' 'report must expose the no-CPU-transfer counter'
Require-Text $hostSource 'deviceErrors' 'report must expose D3D device errors'
Require-Text $qml 'Player2VideoItem' 'harness must paint the extracted Player 2 video item'
Require-Text $qml 'generated|Generated' 'harness must show generated frames'
Require-Text $qml 'presented|Presented' 'harness must show presented frames'
Require-Text $qml 'dropped|Dropped' 'harness must show dropped frames'
Require-Text $qml 'late|Late' 'harness must show late frames'
Require-Text $qml 'CPU transfers' 'harness must show CPU transfer count'
Require-Text $bat 'player2_harness\.exe' 'launcher must start only the standalone harness'

Reject-Text $sources 'MpvQt|libmpv|mpv\.lib|PlayerPage\.qml' 'lab must not import the production mpv player'
Reject-Text $sources 'Cinemeta|Torrentio' 'lab must not import production streaming services'
Reject-Text $sources 'HistoryStore|ProgressStore|LibraryStore|PlaybackHistory' 'lab must not import production stores'
Reject-Text $sources 'Colosseum/Colosseum' 'lab must not use the production settings root'

Write-Output 'player2_harness_contract: PASS'
