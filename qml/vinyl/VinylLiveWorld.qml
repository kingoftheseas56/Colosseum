import QtQuick

// Legacy entry retained for component consumers; Main routes to VinylAppWorld.
VinylRuntime {
    id: runtime
    objectName: "vinylLiveWorld"
    browseShell: legacyShell

    VinylLegacyShell {
        id: legacyShell
        anchors.fill: parent
        worldData: runtime.worldData
        backdrop: runtime.chromeBackdrop
    }
}
