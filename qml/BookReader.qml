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
import "BiblioApi.js" as BiblioApi

Item {
    id: reader
    property string bookPath: ""
    property var bookMeta: ({})     // {id, title, cover, c1, c2, book} for the Continue card
    property bool ready: false

    // The audiobook pairing identity — derived EXACTLY like BiblioBook.qml does
    // (audiobook-opened books carry book.pairKey; ebook-opened books compute it
    // from title/author), so the reader and the detail page agree on pairing
    // without plumbing pairKey through the open-call chain.
    readonly property string pairKey: (reader.bookMeta && reader.bookMeta.pairKey)
                                      ? reader.bookMeta.pairKey
                                      : ((reader.bookMeta && reader.bookMeta.title)
                                         ? BiblioApi.pairKey(reader.bookMeta.title, reader.bookMeta.author || "")
                                         : "")

    signal closed()              // BACK to the library (Esc / reader close)
    signal minimizeRequested()   // foliate chrome minimize → Colosseum window

    // Register the native bridge under the name "bridge" (what qt_bridge_shim.js
    // expects as channel.objects.bridge). registerObject by name works for a C++
    // context object; registeredObjects:[...] needs a QML attached id it can't carry.
    // "readerAudio" is the QML-side hook the shim maps to window.__roomStopAudiobook
    // (TTS-open pauses the audiobook strip — one engine per ear, no native rebuild).
    Component.onCompleted: {
        bridgeChannel.registerObject("bridge", BookBridge)
        bridgeChannel.registerObject("readerAudio", audioRemote)
    }

    QtObject {
        id: audioRemote
        function pauseForTts() { listenStrip.pauseForTts() }
    }

    // Show/hide the HTML pill's listen-audio button per pairing state.
    function syncAudiobookButton() {
        var has = reader.pairKey !== ""
                  && (typeof Audiobooks !== 'undefined')
                  && Audiobooks.isDownloaded(reader.pairKey)
        web.runJavaScript("window.__roomSetHasAudiobook && window.__roomSetHasAudiobook(" + (has ? "true" : "false") + ")")
    }
    // A paired audiobook can finish downloading WHILE the book is open — light the
    // pill button live instead of waiting for the next reader open.
    Connections {
        target: (typeof Audiobooks !== 'undefined') ? Audiobooks : null
        function onFinished(key, path) { if (key === reader.pairKey) reader.syncAudiobookButton() }
    }

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
        var esc = reader.bookPath.replace(/\\/g, "\\\\").replace(/'/g, "\\'")
        web.runJavaScript("window.__ebookOpenBook('" + esc + "')")
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

    // Watchdog: if `stabilized` never arrives, reveal the reader anyway after 6s
    // (and still sync the pill's listen button — this path skips onReaderReady).
    Timer { id: watchdog; interval: 6000; onTriggered: { reader.ready = true; reader.syncAudiobookButton() } }

    // The reader's audiobook listen strip: the SECOND remote onto the shared
    // AudiobookSession (audioSession, window root in Main.qml — reachable here by
    // dynamic scope exactly like AudiobookPlayer.qml binds it). Docked to the
    // bottom, above the WebEngineView. Hiding it never stops the stream.
    AudiobookStrip {
        id: listenStrip
        session: audioSession
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
    }

    Connections {
        target: BookBridge
        function onReaderReady() { reader.ready = true; reader.syncAudiobookButton() }
        function onListenRequested() {
            // one engine per ear: the audiobook strip is opening, so the HTML TTS
            // strip (if speaking) stops first — the reverse direction rides
            // window.__roomStopAudiobook via the "readerAudio" channel object.
            web.runJavaScript("window.__roomStopTts && window.__roomStopTts()")
            listenStrip.openFor(reader.pairKey, reader.bookMeta)
        }
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
