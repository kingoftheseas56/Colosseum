import QtQuick

// Keep the existing QML chrome, but give each interactive region its own native
// child window above Android's foreign WebView. The middle of the book stays
// uncovered so selection and scrolling still reach the WebView.
// Every other platform gets a plain pass-through Item: the Window/WindowContainer
// pair (and the JNI graphics workarounds it installs) is created only on Android,
// behind a Loader that stays inactive elsewhere.
Item {
    id: host
    default property alias surfaceData: surfaceItem.data
    property bool overlayActive: true
    readonly property bool nativeOverlay: Qt.platform.os === "android"

    property Item surface: Item {
        id: surfaceItem
        parent: host.nativeOverlay && nativeOverlayLoader.item
                ? nativeOverlayLoader.item.overlay.contentItem : host
        anchors.fill: parent
        opacity: host.nativeOverlay ? host.opacity : 1
        enabled: host.enabled
    }

    Loader {
        id: nativeOverlayLoader
        anchors.fill: parent
        active: host.nativeOverlay
        // The WindowContainer is the Loader's own item, so it is a real child in the
        // scene. Held only as a property value it has no visual parent, the native
        // window is never attached, and the chrome reparented into it is never shown.
        sourceComponent: Component {
            WindowContainer {
                id: nativeChrome
                property Window overlay: Window {
                    color: "transparent"
                    Component.onCompleted: {
                        if (typeof AndroidEbookRenderer !== "undefined")
                            AndroidEbookRenderer.configureOverlay(this)
                    }
                }
                visible: host.overlayActive && host.opacity > 0.01
                window: nativeChrome.overlay
            }
        }
    }
}
