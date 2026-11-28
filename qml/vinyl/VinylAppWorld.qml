import QtQuick

VinylRuntime {
    id: runtime
    objectName: "vinylRuntime"
    homeLanding: true
    fullWindowDetails: Qt.platform.os !== "android"
    page: "home"
    enabled: componentReady && hostReady && !showHostLoading
    opacity: showHostLoading ? 0 : 1
    browseShell: houseShell
    readonly property bool ready: houseShell.ready
    readonly property string activeTab: houseShell.activeTab
    property Image wallpaperImage: null
    property bool hostedHomeLoading: false
    readonly property bool homeLoadingFinishing: houseShell.homeLoadingFinishing
    readonly property bool homeLoadingIndeterminate: page === "home" && !searchOpen
        && houseShell.homeLoadingIndeterminate
    // Keep the world alive for Home work without revealing its chrome at Loader.Ready.
    readonly property bool showHostLoading: hostedHomeLoading && homeLoadingIndeterminate
    onShowHostLoadingChanged: if (!showHostLoading && hostReady && lifecycleActive && visible)
        Qt.callLater(parkFocus)

    VinylHouseShell {
        id: houseShell
        anchors.fill: parent
        worldData: runtime.worldData
        backdrop: runtime.chromeBackdrop
        wallpaperImage: runtime.wallpaperImage
        showPageBacking: !runtime.blackBrowseBackdrop
        hostedHomeLoading: runtime.hostedHomeLoading
        runtimeReady: runtime.componentReady
        hostReady: runtime.hostReady
    }
}
