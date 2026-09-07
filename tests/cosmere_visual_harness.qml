// Manual eyes-on harness for the dedicated universe room. It deliberately stays open so
// the page can be inspected and its real Apple Books portals can be exercised.
import QtQuick
import QtQuick.Window

Window {
    id: harness
    visible: true
    width: 1280; height: 720
    title: "Cosmere Cognitive Atlas — visual harness"
    property var pickedBook: ({})
    Loader {
        id: atlasLoader
        anchors.fill: parent
        source: "../qml/CosmereUniversePage.qml"
        onLoaded: {
            item.universeName = "Cosmere"
            item.bookRequested.connect(function(book) {
                harness.pickedBook = book
                bookLayer.active = true
                if (bookLayer.item) bookLayer.item.book = book
            })
        }
    }
    Loader {
        id: bookLayer
        anchors.fill: parent
        z: 10
        active: false
        visible: active
        source: "../qml/BiblioBook.qml"
        onLoaded: {
            item.book = harness.pickedBook
            item.backRequested.connect(function() { bookLayer.active = false })
        }
    }
}
