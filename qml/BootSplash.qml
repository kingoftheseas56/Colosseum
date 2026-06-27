// BootSplash — OS-style loader. Prefetches every catalog cover/banner into Qt's pixmap cache
// (hidden Images with cache:true) behind a progress bar, then emits finished() so the shell reveals
// with art already warm. A hard timeout guarantees boot always completes even if a CDN stalls.
import QtQuick
import "Catalog.js" as Catalog
import "TheatreApi.js" as TheatreApi

Rectangle {
    id: splash
    signal finished()
    color: "#08080d"

    property var urls: []
    property int done: 0
    property bool prefetchStarted: false
    property bool completed: false
    readonly property real progress: urls.length > 0 ? done / urls.length : 1

    function complete() {
        if (splash.completed)
            return
        splash.completed = true
        splash.finished()
    }

    function pushUnique(target, url) {
        url = TheatreApi.normalizeArtUrl(url)
        if (url && target.indexOf(url) === -1)
            target.push(url)
    }

    function startPrefetch(extraUrls) {
        if (splash.prefetchStarted)
            return
        var all = []
        var base = Catalog.allImageUrls()
        for (var i = 0; i < base.length; i++)
            pushUnique(all, base[i])
        for (var j = 0; j < extraUrls.length; j++)
            pushUnique(all, extraUrls[j])
        splash.urls = all
        splash.done = 0
        splash.prefetchStarted = true
        if (splash.urls.length === 0)
            complete()
        timeout.start()
    }

    function bump() {
        if (!splash.prefetchStarted || splash.completed)
            return
        if (++splash.done >= splash.urls.length)
            complete()
    }

    Component.onCompleted: TheatreApi.loadTheatre(function(rows) {
        apiTimeout.stop()
        startPrefetch(TheatreApi.imageUrlsFromRows(rows))
    })

    // hidden prefetchers — same sourceSize the tiles use, so the cached decode is reused 1:1
    Repeater {
        model: splash.prefetchStarted ? splash.urls : []
        delegate: Image {
            required property string modelData
            source: modelData
            asynchronous: true; cache: true; visible: false
            sourceSize.width: 264; sourceSize.height: 392
            onStatusChanged: if (status === Image.Ready || status === Image.Error) splash.bump()
        }
    }

    // If the live catalog stalls, still warm the static catalog and reveal the shell.
    Timer { id: apiTimeout; interval: 4500; running: true; repeat: false; onTriggered: splash.startPrefetch([]) }

    // hard timeout so boot always completes after the API/catalog prefetch starts
    Timer { id: timeout; interval: 9000; running: false; repeat: false; onTriggered: splash.complete() }

    Column {
        anchors.centerIn: parent
        spacing: 22
        Text {
            text: "COLOSSEUM"; color: "#f0c44a"
            font.family: "Georgia"; font.pixelSize: 40; font.letterSpacing: 8
            anchors.horizontalCenter: parent.horizontalCenter
        }
        Rectangle {   // progress track
            width: 320; height: 4; radius: 2; color: Qt.rgba(1, 1, 1, 0.12)
            anchors.horizontalCenter: parent.horizontalCenter
            Rectangle {
                width: parent.width * splash.progress; height: parent.height; radius: 2
                color: "#f0c44a"
                Behavior on width { NumberAnimation { duration: 250 } }
            }
        }
        Text {
            text: "Loading library… " + Math.round(splash.progress * 100) + "%"
            color: "#9a99a5"; font.family: "Segoe UI"; font.pixelSize: 12
            anchors.horizontalCenter: parent.horizontalCenter
        }
    }
}
