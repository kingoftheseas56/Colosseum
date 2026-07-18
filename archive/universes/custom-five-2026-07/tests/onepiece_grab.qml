// Visual grab of the One Piece Grand Line page via Qt's grabToImage readback (offscreen).
// The voyage (course line + saga waypoints) is drawn, so it renders here; remote poster art
// rides IPv4-pinned hosts in the real app but may be blank on this bare stack.
import QtQuick
import QtQuick.Window
import "../qml" as UI

Window {
    id: win
    width: 1360; height: 940; visible: true
    color: "#04070a"

    UI.OnePieceUniversePage {
        id: page
        anchors.fill: parent
        universeName: "One Piece"
    }

    Timer {
        interval: 7000; running: true; repeat: false
        onTriggered: {
            var ok = page.grabToImage(function (res) {
                var saved = res.saveToFile("tests/onepiece-grab.png");
                console.log("GRAB " + (saved ? "OK" : "FAIL"));
                // second shot: scroll to THE CHART (the signature) + bounty board
                var flick = null
                function findFlick(item) {
                    for (var i = 0; i < item.children.length; i++) {
                        var c = item.children[i]
                        if (c.contentY !== undefined && c.contentHeight > c.height) return c
                        var f = findFlick(c); if (f) return f
                    }
                    return null
                }
                flick = findFlick(page)
                if (!flick) { console.log("GRAB2 no flickable"); Qt.exit(saved ? 0 : 1); return }
                flick.contentY = 760
                grab2.start()
            });
            if (!ok) { console.log("GRAB request rejected"); Qt.exit(2); }
        }
    }
    Timer {
        id: grab2
        interval: 900; running: false; repeat: false
        onTriggered: {
            page.grabToImage(function (res) {
                var saved = res.saveToFile("tests/onepiece-grab-chart.png");
                console.log("GRAB2 " + (saved ? "OK" : "FAIL"));
                // third: chart's far end (the treasure X) — scroll the HORIZONTAL flick
                function findHFlick(item) {
                    for (var i = 0; i < item.children.length; i++) {
                        var c = item.children[i]
                        if (c.contentX !== undefined && c.contentWidth > c.width + 50
                            && c.flickableDirection === Flickable.HorizontalFlick) return c
                        var f = findHFlick(c); if (f) return f
                    }
                    return null
                }
                var h = findHFlick(page)
                if (h) h.contentX = h.contentWidth - h.width
                grab3.start()
            });
        }
    }
    Timer {
        id: grab3
        interval: 900; running: false; repeat: false
        onTriggered: {
            page.grabToImage(function (res) {
                var saved = res.saveToFile("tests/onepiece-grab-x.png");
                console.log("GRAB3 " + (saved ? "OK" : "FAIL"));
                // fourth: the bounty board (bottom of the page)
                function findVFlick(item) {
                    for (var i = 0; i < item.children.length; i++) {
                        var c = item.children[i]
                        if (c.contentY !== undefined && c.contentHeight > c.height + 50) return c
                        var f = findVFlick(c); if (f) return f
                    }
                    return null
                }
                var v = findVFlick(page)
                if (v) v.contentY = v.contentHeight - v.height
                grab4.start()
            });
        }
    }
    Timer {
        id: grab4
        interval: 900; running: false; repeat: false
        onTriggered: {
            page.grabToImage(function (res) {
                var saved = res.saveToFile("tests/onepiece-grab-bounty.png");
                console.log("GRAB4 " + (saved ? "OK" : "FAIL"));
                Qt.exit(saved ? 0 : 1);
            });
        }
    }
}
