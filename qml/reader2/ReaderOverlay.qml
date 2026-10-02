import QtQuick

// Keep the existing QML chrome, but give each interactive region its own native
// child window above Android's foreign WebView. The middle of the book stays
// uncovered so selection and scrolling still reach the WebView.
Item {
    id: host
    default property alias surfaceData: surfaceItem.data
    property bool overlayActive: true
    readonly property bool nativeOverlay: Qt.platform.os === "android"

    property Item surface: Item {
        id: surfaceItem
        parent: host.nativeOverlay ? overlay.contentItem : host
        anchors.fill: parent
        opacity: host.nativeOverlay ? host.opacity : 1
        enabled: host.enabled
    }
    property Window overlay: Window {
        color: "transparent"
        Component.onCompleted: {
            if (host.nativeOverlay && typeof AndroidEbookRenderer !== "undefined")
                AndroidEbookRenderer.configureOverlay(this)
        }
    }
    property WindowContainer container: WindowContainer {
        parent: host
        anchors.fill: parent
        visible: host.nativeOverlay && host.overlayActive && host.opacity > 0.01
        window: host.nativeOverlay ? host.overlay : null
    }
}
