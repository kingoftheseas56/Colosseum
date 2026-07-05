import QtQuick
import QtQuick.Window

// Headless load gate for the downloaded-video parity surfaces (spec 2026-07-06).
// TheatreSeries and the player menu components sit behind lazy creation paths, so a
// creation-time QML error ships invisibly (the DownloadsPage 12.5px lesson). This
// instantiates each one and requires READY. PlayerPage itself imports the native
// Colosseum.Player module and cannot load headless — it is covered by the contract
// test + the live app smoke.
Window {
    visible: true
    width: 300; height: 200

    property int readyCount: 0
    property int errorCount: 0
    function tally(name, status) {
        if (status === Loader.Error) { errorCount++; console.log("[diag] LOADER ERROR — " + name) }
        if (status === Loader.Ready) { readyCount++; console.log("[diag] LOADER READY — " + name) }
        if (readyCount + errorCount === 4)
            console.log(errorCount === 0 ? "[diag] ALL PARITY SURFACES READY" : "[diag] PARITY LOAD FAILURES: " + errorCount)
    }

    Loader { source: "../qml/TheatreSeries.qml"; onStatusChanged: tally("TheatreSeries", status) }
    Loader { source: "../qml/AudioMenu.qml";     onStatusChanged: tally("AudioMenu", status) }
    Loader { source: "../qml/SubtitleMenu.qml";  onStatusChanged: tally("SubtitleMenu", status) }
    Loader { source: "../qml/SubStyleBar.qml";   onStatusChanged: tally("SubStyleBar", status) }

    Timer { interval: 4000; running: true; onTriggered: Qt.quit() }
}
