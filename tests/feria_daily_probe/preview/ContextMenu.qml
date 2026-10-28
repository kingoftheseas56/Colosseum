import QtQuick
import QtQuick.Controls
import QtQuick.Window
import QtWebEngine
import "../../../qml/feria" as Feria

// Headless review snapshot only: minimal browser stand-in, not a WebEngine page.
Window {
    id: preview
    width: 760
    height: 620
    visible: true
    color: "#1b2430"
    property bool previewReady: false

    Item {
        id: browserStandIn
        anchors.fill: parent
        property bool suppressed: false
        property bool closingSession: false
        property bool clearingSession: false
        property bool adoptingWindowRequest: false
        property bool rendererLost: false
        property int navigationGeneration: 1
        property bool navCanGoBack: true
        property bool navCanGoForward: false
        property var navigation: ({reload: function() {}})
        function triggerWebAction(action) {}
        function pageBack() {}
        function pageForward() {}
        function imageTabRequested(url) {}

        Feria.FeriaContextMenu {
            id: browserMenu
            browser: browserStandIn
        }
    }
    Component.onCompleted: Qt.callLater(function() {
        browserMenu.openForRequest({
            linkUrl: "https://example.com/example",
            mediaUrl: "",
            mediaType: -1,
            isContentEditable: true,
            selectedText: "",
            editFlags: ContextMenuRequest.CanSelectAll,
            position: Qt.point(122, 105)
        })
        Qt.callLater(function() { preview.previewReady = browserMenu.visible })
    })
}
