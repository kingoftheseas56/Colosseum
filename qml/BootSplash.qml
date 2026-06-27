// BootSplash — OS-style loader. Prefetches every catalog cover/banner into Qt's pixmap cache
// (hidden Images with cache:true) behind a progress bar, then emits finished() so the shell reveals
// with art already warm. A hard timeout guarantees boot always completes even if a CDN stalls.
import QtQuick
import "Catalog.js" as Catalog

Rectangle {
    id: splash
    signal finished()
    color: "#08080d"

    readonly property var urls: Catalog.allImageUrls()
    property int done: 0
    readonly property real progress: urls.length > 0 ? done / urls.length : 1

    function bump() { if (++splash.done >= splash.urls.length) splash.finished() }

    // hidden prefetchers — same sourceSize the tiles use, so the cached decode is reused 1:1
    Repeater {
        model: splash.urls
        delegate: Image {
            required property string modelData
            source: modelData
            asynchronous: true; cache: true; visible: false
            sourceSize.width: 264; sourceSize.height: 392
            onStatusChanged: if (status === Image.Ready || status === Image.Error) splash.bump()
        }
    }

    // hard timeout so boot always completes (≤ 6s even if a CDN stalls)
    Timer { interval: 6000; running: true; repeat: false; onTriggered: splash.finished() }

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
