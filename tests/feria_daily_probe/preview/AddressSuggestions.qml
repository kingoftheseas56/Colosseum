import QtQuick
import QtQuick.Controls
import QtQuick.Window
import "../../../qml/feria" as Feria

// Review-only fixture. Three saved pages are supplied by a stand-in store.
Window {
    id: preview
    width: 760
    height: 620
    visible: true
    color: "#111926"
    property bool previewReady: false
    property var fakeStore: ({
        searchBrowserBookmarks: function(query) {
            return [{url: "http://127.0.0.1:4242/apple", title: "Apple orchard"}]
        },
        searchBrowserHistory: function(query) {
            return [
                {url: "http://127.0.0.1:4242/orchard-two", title: "Orchard planning board"},
                {url: "http://127.0.0.1:4242/orchard-one", title: "Orchard field journal"}
            ]
        }
    })
    function findItem(item, name) {
        if (!item) return null
        if (item.objectName === name) return item
        var children = item.children || []
        for (var i = 0; i < children.length; ++i) {
            var result = findItem(children[i], name)
            if (result) return result
        }
        return null
    }
    Rectangle {
        x: 48; y: 40; width: 670; height: 100; radius: 16
        color: "#1a2737"
        border.color: "#364455"
        Text {
            x: 20; y: 13
            text: "Feria"
            color: "#f7f7f5"
            font.pixelSize: 19
        }
        Feria.FeriaBrowserToolbar {
            id: toolbar
            x: 190
            y: 46
            accountStore: preview.fakeStore
            currentUrl: "http://127.0.0.1:4242/apple"
            browserReady: true
        }
    }
    Component.onCompleted: Qt.callLater(function() {
        toolbar.focusAddress()
        Qt.callLater(function() {
            var field = preview.findItem(toolbar.overlay, "feriaBrowserAddress")
            if (!field) return
            field.forceActiveFocus()
            field.text = "orchard"
            Qt.callLater(function() {
                var list = preview.findItem(toolbar.overlay, "feriaAddressSuggestions")
                if (!list || list.count !== 3) return
                list.currentIndex = 1
                preview.previewReady = true
            })
        })
    })
}
