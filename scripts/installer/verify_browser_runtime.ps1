param([Parameter(Mandatory = $true)][string]$RuntimeRoot)

$ErrorActionPreference = "Stop"
# Qt 6.11 Windows Quick deployment, including Feria's WebChannel import.
# https://doc.qt.io/qt-6.11/qtwebengine-deploying.html
$required = @(
    "QtWebEngineProcess.exe",
    "Qt6WebEngineCore.dll",
    "Qt6WebEngineQuick.dll",
    "Qt6WebChannel.dll",
    "resources\icudtl.dat",
    "resources\qtwebengine_resources.pak",
    "resources\qtwebengine_resources_100p.pak",
    "resources\qtwebengine_resources_200p.pak",
    "resources\qtwebengine_devtools_resources.pak",
    "resources\v8_context_snapshot.bin",
    "translations\qtwebengine_locales\en-US.pak",
    "qml\QtWebEngine\qmldir",
    "qml\QtWebEngine\qtwebenginequickplugin.dll",
    "qml\QtWebChannel\qmldir",
    "qml\QtWebChannel\webchannelquickplugin.dll"
)
foreach ($relative in $required) {
    $file = Join-Path $RuntimeRoot $relative
    if (!(Test-Path -LiteralPath $file -PathType Leaf)) {
        throw "BROWSER_RUNTIME_FAIL: missing runtime file: $relative"
    }
    if ((Get-Item -LiteralPath $file).Length -eq 0) {
        throw "BROWSER_RUNTIME_FAIL: empty runtime file: $relative"
    }
}
Write-Host "BROWSER_RUNTIME_OK files=$($required.Count)"
