pragma ComponentBehavior: Bound

// BootSplash — OS-style loader. Prefetches every catalog cover/banner into Qt's pixmap cache
// (hidden Images with cache:true) behind the opening sequence, then emits finished() so the shell reveals
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
    property bool prefetchEnabled: true
    property bool includeVinyl: false
    property bool reducedMotion: false
    readonly property real progress: !prefetchEnabled || urls.length === 0 ? 1 : done / urls.length

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
        // Keep the API and reveal timeout in the diagnostic arm. Only hidden image work changes.
        if (!splash.prefetchEnabled) {
            timeout.start()
            return
        }
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
        model: splash.prefetchEnabled && splash.prefetchStarted ? splash.urls : []
        delegate: Image {
            required property string modelData
            // A timed-out boot no longer owns foreground image work. Keep finished
            // decodes attached, but cancel queued/in-flight warmers on reveal.
            property bool ready: false
            source: ready || (!splash.completed && splash.visible) ? modelData : ""
            asynchronous: true; cache: true; visible: false
            sourceSize.width: 264; sourceSize.height: 392
            onStatusChanged: {
                if (status === Image.Ready) ready = true
                if (status === Image.Ready || status === Image.Error) splash.bump()
            }
        }
    }

    // If the live catalog stalls, still warm the static catalog and reveal the shell.
    Timer { id: apiTimeout; interval: 4500; running: true; repeat: false; onTriggered: splash.startPrefetch([]) }

    // hard timeout so boot always completes after the API/catalog prefetch starts
    Timer { id: timeout; interval: 9000; running: false; repeat: false; onTriggered: splash.complete() }

    Loader {
        anchors.fill: parent
        active: splash.visible
        asynchronous: true
        sourceComponent: BootSplashPresentation {
            booting: !splash.completed
            includeVinyl: splash.includeVinyl
            reducedMotion: splash.reducedMotion
        }
    }
}
