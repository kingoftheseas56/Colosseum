import QtQuick

VinylRuntime {
    id: runtime
    objectName: "vinylRuntime"
    homeLanding: true
    page: "home"
    enabled: componentReady && hostReady
    browseShell: houseShell
    readonly property bool ready: houseShell.ready
    readonly property string activeTab: houseShell.activeTab

    VinylHouseShell {
        id: houseShell
        anchors.fill: parent
        worldData: runtime.worldData
        backdrop: runtime.chromeBackdrop
        runtimeReady: runtime.componentReady
        hostReady: runtime.hostReady
    }
}
