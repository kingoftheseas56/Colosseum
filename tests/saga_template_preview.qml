import QtQuick
import QtQuick.Window
import "../qml" as App

Window {
    id: host
    visible: true
    width: Number(argument("--width", "1440"))
    height: 1000
    title: "Saga template — " + saga.universeName
    function argument(key, fallback) {
        const i = Qt.application.arguments.indexOf(key)
        return i >= 0 ? Qt.application.arguments[i + 1] : fallback
    }
    function findItem(item, name) {
        if (item.objectName === name) return item
        for (let i = 0; i < item.children.length; ++i) {
            const result = findItem(item.children[i], name)
            if (result) return result
        }
        return null
    }
    App.SagaUniversePage {
        id: saga
        universeName: host.argument("--universe", "Harry Potter")
        onBackRequested: host.close()
        onCloseRequested: host.close()
        onMinimizeRequested: host.showMinimized()
        onFullscreenRequested: host.visibility === Window.FullScreen ? host.showNormal() : host.showFullScreen()
        onBookRequested: (book) => console.log("[saga-preview] BOOK", book.title)
        onWatchRequested: (item) => console.log("[saga-preview] WATCH", item.title)
        onComicsArchiveRequested: (box) => console.log("[saga-preview] COMICS", box.name)
    }
    Timer {
        interval: 20000
        running: host.argument("--capture", "").length > 0
        onTriggered: {
            const section = findItem(saga, host.argument("--section", ""))
            if (section && section !== saga) saga.revealSection(section)
            saga.grabToImage(function(result) {
            const saved = result.saveToFile(host.argument("--capture", ""))
            console.log("[saga-preview] capture", saved, "books", saga.uni.books.length,
                        "films", saga.uni.films.length, "shows", saga.uni.shows.length)
            Qt.exit(saved ? 0 : 1)
            })
        }
    }
}
