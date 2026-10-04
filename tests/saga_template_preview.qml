import QtQuick
import QtQuick.Window
import "../qml" as App

Window {
    id: host
    visible: true
    width: Number(argument("--width", "1440"))
    height: Number(argument("--height", "720"))
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
        onUniverseRequested: (name) => saga.universeName = name
        onContinueResumeRequested: (entry) => console.log("[saga-preview] RESUME", entry.title)
        onContinueDetailRequested: (entry) => console.log("[saga-preview] DETAIL", entry.title)
    }
    // --state returning: seed a returning visitor once the works have resolved, so Continue,
    // progress marks and the resume labels can be judged without the app's Progress store.
    Timer {
        interval: 12000
        running: host.argument("--state", "") === "returning"
        onTriggered: {
            const b = saga.uni.books, f = saga.uni.films, s = saga.uni.shows, out = []
            function add(item, kind, progress, sub, watched) {
                if (item && item.resolved !== false && item.upcoming !== true)
                    out.push({ id: String(item.id), kind: kind, title: item.title, sub: sub,
                               cover: item.cover, progress: progress, watched: watched === true })
            }
            add(b[0], "book", 1, "", true)
            add(b[1], "book", 0.41, "Chapter 9 · 41%")
            if (f.length) { add(f[0], "video", 1, "", true); add(f[1], "video", 0.62, "1h 02m left") }
            if (s.length) add(s[0], "video", 0.33, "S3 E4 · 22m left")
            saga.previewProgress = out.reverse()
        }
    }
    // --scroll N: capture the page scrolled to N pixels
    Timer {
        interval: host.waitMs - 2000
        running: Number(host.argument("--scroll", "0")) > 0
        onTriggered: findItem(saga, "sagaScroll").contentY = Number(host.argument("--scroll", "0"))
    }
    // --wait N: seconds before the capture (default 20). Outside the app nothing pins the image
    // hosts to IPv4, so backdrops can take ~21s each on a machine with dead IPv6.
    readonly property int waitMs: Number(argument("--wait", "20")) * 1000
    Timer {
        interval: host.waitMs
        running: host.argument("--capture", "").length > 0
        onTriggered: {
            const section = findItem(saga, host.argument("--section", ""))
            if (section && section !== saga) saga.revealSection(section)
            saga.grabToImage(function(result) {
            const saved = result.saveToFile(host.argument("--capture", ""))
            console.log("[saga-preview] focus", host.activeFocusItem ? (host.activeFocusItem.objectName || host.activeFocusItem) : "none",
                        "scroll", findItem(saga, "sagaScroll").contentY)
            console.log("[saga-preview] capture", saved, "books", saga.uni.books.length,
                        "films", saga.uni.films.length, "shows", saga.uni.shows.length)
            Qt.exit(saved ? 0 : 1)
            })
        }
    }
}
