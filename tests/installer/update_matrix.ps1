# Tiny NSIS matrix for the side-by-side updater.  It never stages the full app:
# every payload is a few sentinel files and a text file named colosseum.exe.
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$nsis = "C:\Program Files (x86)\NSIS\makensis.exe"
if (!(Test-Path -LiteralPath $nsis)) { throw "makensis not found: $nsis" }

$temp = Join-Path ([IO.Path]::GetTempPath()) ("colosseum-update-matrix-" + [guid]::NewGuid().ToString("N"))
$fakeLocal = Join-Path $temp "localappdata"
$stageN = Join-Path $temp "stage-n"
$stageN1 = Join-Path $temp "stage-n1"
$stageBroken = Join-Path $temp "stage-broken"
$outN = Join-Path $temp "n.exe"
$outN1 = Join-Path $temp "n1.exe"
$outBroken = Join-Path $temp "broken.exe"
$outRename = Join-Path $temp "rename.exe"
$tag = [guid]::NewGuid().ToString("N").Substring(0, 8)
$regKey = "Software\ColosseumUpdateMatrix\$tag"
$shortcutPrefix = "Colosseum-UpdateMatrix-$tag"
$registryPath = "Registry::HKEY_CURRENT_USER\$regKey"

function Require([bool]$condition, [string]$message) {
    if (!$condition) { throw "UPDATE_MATRIX_FAIL: $message" }
}

function Write-Sentinel([string]$path, [string]$text) {
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $path) | Out-Null
    Set-Content -LiteralPath $path -Value $text -Encoding ascii -NoNewline
}

function Invoke-Nsis([string]$outFile, [string]$stage, [string]$version, [switch]$TestMode,
                     [switch]$RenameFailure) {
    $args = @(
        "/DSTAGE=$stage", "/DVERSION=$version", "/DOUTFILE=$outFile",
        "/DCOLOSSEUM_UPDATE_TEST_ROOT=$fakeLocal",
        "/DCOLOSSEUM_UPDATE_TEST_REGKEY=$regKey",
        "/DCOLOSSEUM_UPDATE_TEST_SHORTCUT_PREFIX=$shortcutPrefix"
    )
    if ($TestMode) { $args += "/DCOLOSSEUM_UPDATE_TEST_MODE=1" }
    if ($RenameFailure) { $args += "/DCOLOSSEUM_UPDATE_TEST_RENAME_FAIL=1" }
    $args += (Join-Path $root "scripts\installer\colosseum.nsi")
    & $nsis @args | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "makensis failed for $version ($LASTEXITCODE)" }
    Require (Test-Path -LiteralPath $outFile) "compiled installer exists: $outFile"
}

function Invoke-Installer([string]$path, [string[]]$arguments, [switch]$AllowFailure) {
    & $path @arguments | Out-Host
    if ($LASTEXITCODE -ne 0 -and !$AllowFailure) { throw "installer failed: $path ($LASTEXITCODE)" }
    return $LASTEXITCODE
}

try {
    New-Item -ItemType Directory -Force -Path $stageN, $stageN1, $stageBroken | Out-Null
    $exeRel = "native\build-msvc\colosseum.exe"
    Write-Sentinel (Join-Path $stageN $exeRel) "COL SSEUM N"
    Write-Sentinel (Join-Path $stageN "payload.txt") "payload-N"
    Write-Sentinel (Join-Path $stageN1 $exeRel) "COL SSEUM N+1"
    Write-Sentinel (Join-Path $stageN1 "payload.txt") "payload-N+1"
    Write-Sentinel (Join-Path $stageBroken "payload.txt") "broken-without-executable"

    # Normal install: payload, shortcuts, uninstall marker, and registry metadata.
    Invoke-Nsis $outN $stageN "1.0.0"
    Invoke-Installer $outN @("/S")
    $installRoot = Join-Path $fakeLocal "Programs\Colosseum"
    Require ((Get-Content -LiteralPath (Join-Path $installRoot $exeRel) -Raw) -eq "COL SSEUM N") "normal install payload"
    Require (Test-Path -LiteralPath (Join-Path $installRoot "uninstall.exe")) "normal install writes uninstaller"
    $reg = Get-ItemProperty -LiteralPath $registryPath
    Require ($reg.DisplayVersion -eq "1.0.0") "normal install registry version"
    $desktopLink = Join-Path ([Environment]::GetFolderPath("Desktop")) "$shortcutPrefix.lnk"
    $startLink = Join-Path $env:APPDATA "Microsoft\Windows\Start Menu\Programs\$shortcutPrefix.lnk"
    Require (Test-Path -LiteralPath $desktopLink) "normal install desktop shortcut"
    Require (Test-Path -LiteralPath $startLink) "normal install start shortcut"

    # Successful N -> N+1 update.  A short-lived process gives the NSIS handle
    # wait a real observable contract without holding the test shell open.
    Invoke-Nsis $outN1 $stageN1 "1.1.0" -TestMode
    $waiter = Start-Process -FilePath "$env:ComSpec" -ArgumentList @("/c", "ping 127.0.0.1 -n 3 >nul") -PassThru
    $updateLog = Join-Path $temp "success.log"
    Invoke-Installer $outN1 @(
        "/UPDATE=1", "/WAITPID=$($waiter.Id)", "/TARGETVERSION=1.1.0", "/RESTART=1",
        "/LOG=$updateLog"
    )
    $waiter.WaitForExit(10000) | Out-Null
    Require ($waiter.HasExited) "update waits for exact PID"
    Require ((Get-Content -LiteralPath (Join-Path $installRoot $exeRel) -Raw) -eq "COL SSEUM N+1") "N+1 payload swapped"
    $oldRoot = Join-Path $fakeLocal "Programs\Colosseum.__update-old"
    $newRoot = Join-Path $fakeLocal "Programs\Colosseum.__update-new"
    Require (Test-Path -LiteralPath $oldRoot) "old backup retained until healthy boot"
    Require (!(Test-Path -LiteralPath $newRoot)) "new sibling renamed into place"
    Require ((Get-Content -LiteralPath (Join-Path $oldRoot $exeRel) -Raw) -eq "COL SSEUM N") "old backup contains N"
    $reg = Get-ItemProperty -LiteralPath $registryPath
    Require ($reg.DisplayVersion -eq "1.1.0") "update refreshes registry version"
    Require (Test-Path -LiteralPath (Join-Path $installRoot "uninstall.exe")) "update preserves uninstaller"
    Require ((Get-Content -LiteralPath "$updateLog.launch" -Raw) -match "--update-result=success") "N+1 relaunch result"
    Require ((Get-Content -LiteralPath "$updateLog.launch" -Raw) -match [regex]::Escape("--update-backup=$oldRoot")) "exact old backup relaunch argument"

    # A stale backup is never overwritten by a second update attempt.
    $stale = Join-Path $fakeLocal "Programs\Colosseum.__update-old"
    Invoke-Installer $outN1 @("/UPDATE=1", "/WAITPID=0", "/TARGETVERSION=1.1.0", "/RESTART=0", "/LOG=$(Join-Path $temp 'stale.log')") -AllowFailure | Out-Null
    Require ((Get-Content -LiteralPath (Join-Path $installRoot $exeRel) -Raw) -eq "COL SSEUM N+1") "stale backup leaves current install"

    # Extraction validation fails before N is renamed.
    Remove-Item -LiteralPath $oldRoot -Recurse -Force
    Invoke-Nsis $outBroken $stageBroken "1.2.0" -TestMode
    $brokenLog = Join-Path $temp "broken.log"
    Invoke-Installer $outBroken @("/UPDATE=1", "/WAITPID=0", "/TARGETVERSION=1.2.0", "/RESTART=0", "/LOG=$brokenLog") -AllowFailure | Out-Null
    Require ((Get-Content -LiteralPath (Join-Path $installRoot $exeRel) -Raw) -eq "COL SSEUM N+1") "bad extraction leaves N untouched"
    Require (!(Test-Path -LiteralPath $oldRoot)) "bad extraction does not create old backup"
    Require ((Get-Content -LiteralPath $brokenLog -Raw) -match "payload_validation_failed") "bad extraction is logged"

    # A forced rename failure exercises restoration and the rollback relaunch.
    Invoke-Nsis $outRename $stageN1 "1.3.0" -TestMode -RenameFailure
    $renameLog = Join-Path $temp "rename.log"
    Invoke-Installer $outRename @("/UPDATE=1", "/WAITPID=0", "/TARGETVERSION=1.3.0", "/RESTART=1", "/LOG=$renameLog") -AllowFailure | Out-Null
    Require ((Get-Content -LiteralPath (Join-Path $installRoot $exeRel) -Raw) -eq "COL SSEUM N+1") "rename failure restores N"
    Require (!(Test-Path -LiteralPath $oldRoot)) "rollback removes temporary old sibling"
    Require (!(Test-Path -LiteralPath $newRoot)) "rollback removes temporary new sibling"
    Require ((Get-Content -LiteralPath "$renameLog.launch" -Raw) -match "--update-result=rollback") "rollback relaunch result"
    Require ((Get-Content -LiteralPath "$renameLog.launch" -Raw) -match [regex]::Escape("--update-target=$newRoot")) "rollback target is exact new sibling"

    Write-Host "UPDATE_MATRIX_OK"
}
finally {
    Remove-Item -LiteralPath $registryPath -Recurse -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $desktopLink, $startLink -Force -ErrorAction SilentlyContinue
    if (Test-Path -LiteralPath $temp) {
        Remove-Item -LiteralPath $temp -Recurse -Force -ErrorAction SilentlyContinue
    }
}
