$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$main = Get-Content (Join-Path $root "native/main.cpp") -Raw
$loopback = Get-Content (Join-Path $root "native/net/LoopbackPinProxy.cpp") -Raw
$layout = Get-Content (Join-Path $root "native/bootstrap/StartupLayout.cpp") -Raw

function Need([bool]$condition, [string]$message) {
    if (-not $condition) { throw $message }
}

Need ($main -notmatch 'QHostInfo::fromName|QThread::msleep|git\s+pull|pull\s+--ff-only') `
    "main.cpp contains a blocking/side-effecting pre-QML bootstrap primitive."
Need ($loopback -notmatch 'QHostInfo::fromName') `
    "LoopbackPinProxy must never reintroduce synchronous DNS on first connection."
Need ($layout -like '*qml_build_mismatch:*') `
    "StartupLayout must fail closed on native/QML fingerprint mismatch."

$load = $main.IndexOf('engine.load(QUrl::fromLocalFile(qmlPath))')
$rootCheck = $main.IndexOf('engine.rootObjects().isEmpty()', $load)
$refresh = $main.IndexOf('pinStore->refresh(pinnedHosts)', $load)
$frame = $main.IndexOf('&QQuickWindow::frameSwapped', $load)
$ack = $main.IndexOf('acknowledgeHealthyBoot(launchArguments)', $load)
Need ($load -ge 0 -and $rootCheck -gt $load) "QML root validation must follow engine.load()."
Need ($refresh -gt $rootCheck) "Remote pin refresh must begin only after QML root validation."
Need ($frame -gt $rootCheck -and $ack -gt $frame) `
    "Updater success must be acknowledged from first-frame/fallback logic after root validation."

$tag = $main.IndexOf('const bool isolatedAppData')
$log = $main.IndexOf('AppLog::install()', $tag)
$migrate = $main.IndexOf('reconcileAppData(oldAppData, newAppData)', $log)
Need ($tag -ge 0 -and $log -gt $tag -and $migrate -gt $log) `
    "AppData isolation and durable logging must be established before real-root reconciliation."

Write-Host "Startup bootstrap contract checks passed."