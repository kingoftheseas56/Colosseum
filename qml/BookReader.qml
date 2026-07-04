// BookReader.qml — Tankoban 2's foliate EPUB reader, brought home into Colosseum
// via a QML WebEngineView (the whole reason for the MSVC migration). Loads
// resources/book_reader/ebook_reader.html, wires the native `BookBridge` over
// QWebChannel (qwebchannel.js + qt_bridge_shim.js injected at DocumentCreation,
// so the file:// page can reach the bridge), then opens a downloaded .epub via
// window.__ebookOpenBook(path). The book file comes from the download-fed
// `Books` engine — never a stream.
import QtQuick
import QtWebEngine
import QtWebChannel
import "BookReaderLaunch.js" as Launch

Item {
    id: reader
    property string bookPath: ""
    property var bookMeta: ({})     // {id, title, cover, c1, c2, book} for the Continue card
    property bool ready: false

    signal closed()              // BACK to the library (Esc / reader close)
    signal minimizeRequested()   // foliate chrome minimize → Colosseum window

    // Register the native bridge under the name "bridge" (what qt_bridge_shim.js
    // expects as channel.objects.bridge). registerObject by name works for a C++
    // context object; registeredObjects:[...] needs a QML attached id it can't carry.
    Component.onCompleted: bridgeChannel.registerObject("bridge", BookBridge)

    // Open a local book file. First call loads the reader HTML, then fires
    // __ebookOpenBook on load-success; later calls (page already up) fire it directly.
    function open(path, book) {
        reader.bookPath = path
        reader.bookMeta = book || ({})
        reader.ready = false
        watchdog.restart()
        if (web.loadProgress >= 100 && web.url != "") reader.openInPage()
        else web.url = Qt.resolvedUrl("../resources/book_reader/ebook_reader.html")
    }
    function openInPage() {
        if (reader.bookPath === "") return
        web.runJavaScript(Launch.buildOpenScript(reader.bookPath, reader.bookMeta || ({})))
    }

    Rectangle { anchors.fill: parent; color: "#000000" }

    WebEngineView {
        id: web
        anchors.fill: parent
        backgroundColor: "#000000"
        settings.localContentCanAccessFileUrls: true
        settings.localContentCanAccessRemoteUrls: true
        settings.javascriptCanAccessClipboard: true

        // The bridge scripts (qwebchannel.js + qt_bridge_shim.js) are declared in
        // ebook_reader.html's <head>; this webChannel just provides the transport.
        // The "bridge" object is registered by name in Component.onCompleted above.
        webChannel: WebChannel { id: bridgeChannel }

        onLoadingChanged: function (info) {
            if (info.status === WebEngineView.LoadSucceededStatus) reader.openInPage()
        }
        onJavaScriptConsoleMessage: function (level, message, line, src) {
            console.log("[BookReader JS] " + message)
        }
    }

    // Black overlay until foliate's `stabilized` fires (BookBridge.readerReady).
    Rectangle {
        anchors.fill: parent; color: "#000000"; visible: !reader.ready
        Text {
            anchors.centerIn: parent; text: "Loading…"
            color: Qt.rgba(1, 1, 1, 0.55); font.pixelSize: 14
        }
    }

    // Watchdog: if `stabilized` never arrives, reveal the reader anyway after 6s.
    Timer { id: watchdog; interval: 6000; onTriggered: reader.ready = true }

    Connections {
        target: BookBridge
        function onReaderReady() { reader.ready = true }
        function onCloseRequested() { reader.closed() }
        function onWindowCloseRequested() { reader.closed() }
        function onWindowMinimizeRequested() { reader.minimizeRequested() }
        function onFullscreenRequested(on) { /* Colosseum is always fullscreen */ }
        // Feed the unified Continue/resume row (download-fed reading, like manga).
        function onProgressSaved(bookId, fraction) {
            if (typeof Progress === "undefined" || reader.bookPath === "") return
            var m = reader.bookMeta || ({})
            var idStr = (m.id !== undefined && ("" + m.id).length) ? ("" + m.id) : reader.bookPath
            Progress.record({
                "id": idStr,
                "kind": "book",
                "caption": m.title || "",
                "title": m.title || "",
                "sub": (fraction > 0 ? Math.round(fraction * 100) + "%" : "Reading"),
                "cover": m.cover || "",
                "c1": m.c1 !== undefined ? m.c1 : "#2a2440",
                "c2": m.c2 !== undefined ? m.c2 : "#15111f",
                "progress": Math.min(1, Math.max(0, fraction)),
                "resume": { "path": reader.bookPath, "book": m }
            })
        }
    }
}
