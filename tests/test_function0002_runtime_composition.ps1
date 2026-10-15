param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
)
$ErrorActionPreference = "Stop"

function Require-Text([string]$Path, [string]$Needle, [string]$Message) {
    $text = Get-Content -Raw -LiteralPath $Path
    if (-not $text.Contains($Needle)) { throw $Message }
}
function Reject-Text([string]$Path, [string]$Needle, [string]$Message) {
    $text = Get-Content -Raw -LiteralPath $Path
    if ($text.Contains($Needle)) { throw $Message }
}

$main = Join-Path $RepoRoot "native/main.cpp"
$registry = Join-Path $RepoRoot "native/work/BackgroundActivityRegistry.cpp"
$download = Join-Path $RepoRoot "native/update/UpdateDownload.h"
$watchParty = Join-Path $RepoRoot "native/watchparty/WatchPartyUiController.h"
$testsCmake = Join-Path $RepoRoot "tests/CMakeLists.txt"

Require-Text $main "backgroundActivity->setCoordinator(backgroundWork);" "shared coordinator is not bound to BackgroundActivity"
Reject-Text $main "Q_UNUSED(backgroundWork);" "backgroundWork remains intentionally unused"
Require-Text $registry "BackgroundWorkCoordinator::pause" "registry does not route pause to coordinator"
Require-Text $registry "pauseStateChanged" "registry does not consume scheduler pause truth"
Require-Text $download "std::unique_ptr<UpdateCache> m_ownedCache" "UpdateDownload does not own production UpdateCache"
Require-Text $watchParty "setOwnedAccountBridge" "WatchPartyUi lacks owned bridge seam"
Require-Text $testsCmake "background_work_composition_harness" "composition harness is not registered with CTest"
Reject-Text (Join-Path $RepoRoot "native/account/AccountRuntime.cpp") "PRE-FLIGHT DRAFT STATUS" "AccountRuntime still claims draft status"
Reject-Text (Join-Path $RepoRoot "native/account/ProfileStoreRuntime.cpp") "PRE-FLIGHT DRAFT STATUS" "ProfileStoreRuntime still claims draft status"

Write-Host "FUNCTION0002_RUNTIME_COMPOSITION_CONTRACT_OK"
