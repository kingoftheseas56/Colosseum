$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$main = Get-Content -LiteralPath (Join-Path $root 'native\main.cpp') -Raw
$qml = Get-Content -LiteralPath (Join-Path $root 'qml\Main.qml') -Raw
$cmake = Get-Content -LiteralPath (Join-Path $root 'native\CMakeLists.txt') -Raw

function Need([bool]$condition, [string]$message) {
    if (-not $condition) { throw $message }
}

Need ($main.Contains('#include "work/ForegroundPriorityGovernor.h"')) 'main must include the governor'
Need ($main.Contains('new work::ForegroundPriorityGovernor')) 'production must construct one governor'
Need ($main.Contains('installEventFilter(foregroundPriority)')) 'governor must observe global app input'
Need ($main.Contains('"ForegroundPriority"')) 'governor must be exposed to QML'
Need ($main.Contains('&work::ForegroundPriorityGovernor::pressureChanged')) 'pressure must fan out natively'
Need ($main.Contains('backgroundWork->setPressure')) 'shared background coordinator must receive pressure'
Need ($main.Contains('biblioCatalog->setForegroundPriorityActive')) 'Biblio must receive interaction pressure'
Need ($main.Contains('biblioCatalog->setBackgroundWorkSuspended')) 'Biblio must receive immersive suspension'
Need ($main.Contains('vaultLibrary->setImmersive')) 'Vault apply/commit path must receive pressure'
Need ($main.Contains('catalogVaultClient->setForegroundPressure')) 'CatalogVault must receive pressure'
Need ($qml.Contains('ForegroundPriority.setImmersiveSurfaceOpen(win.immersiveSurfaceOpen)')) 'QML immersive ownership must target governor'
Need (-not $qml.Contains('BiblioCatalog.setBackgroundWorkSuspended(win.immersiveSurfaceOpen)')) 'QML must not bypass the governor for Biblio'
Need ($cmake.Contains('work/ForegroundPriorityGovernor.cpp')) 'app build must compile governor source'
Write-Host 'FOREGROUND_PRIORITY_GOVERNOR_P0_OK'
