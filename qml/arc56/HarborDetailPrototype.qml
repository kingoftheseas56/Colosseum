import QtQuick
import "HarborDetailFixtures.js" as Fixtures

Rectangle {
    id: window
    width: 1440
    height: 900
    color: "#06070b"
    objectName: "arc56HarborDetailPrototype"

    property string mode: "series"
    property string capturePath: ""
    property real captureScrollY: 0
    property var fixture: Fixtures.series()

    function parseArguments() {
        var args = Qt.application.arguments || []
        for (var i = 0; i < args.length; ++i) {
            var arg = String(args[i])
            if (arg === "--movie") mode = "movie"
            else if (arg === "--series") mode = "series"
            else if (arg.indexOf("--capture=") === 0)
                capturePath = arg.slice("--capture=".length)
            else if (arg.indexOf("--scroll=") === 0)
                captureScrollY = Number(arg.slice("--scroll=".length)) || 0
        }
        fixture = mode === "movie" ? Fixtures.movie() : Fixtures.series()
    }

    function chooseMode(nextMode) {
        mode = nextMode
        fixture = mode === "movie" ? Fixtures.movie() : Fixtures.series()
        detail.inWatchlist = false
        detail.favorite = false
        detail.watched = false
        detail.forceActiveFocus()
    }

    Component.onCompleted: {
        parseArguments()
        console.log("[arc56] mode", mode, "capture", capturePath, "scroll", captureScrollY)
        if (captureScrollY > 0)
            detail.contentY = captureScrollY
        if (capturePath.length > 0)
            captureTimer.start()
    }

    FontLoader { source: "../../assets/fonts/Fraunces-Regular.ttf" }
    FontLoader { source: "../../assets/fonts/Switzer-Regular.otf" }
    FontLoader { source: "../../assets/fonts/Inter-Regular.otf" }

    HarborDetailView {
        id: detail
        anchors.fill: parent
        detailData: window.fixture
        focus: true

        onPlayRequested: item => console.log("[arc56] play", item.title || item.id)
        onDownloadRequested: item => console.log("[arc56] download", item.title || item.id)
        onEpisodeDetailsRequested: episode => console.log("[arc56] episode details", episode.title)
        onItemRequested: item => console.log("[arc56] related title", item.title)

        Keys.onPressed: event => {
            if (event.key === Qt.Key_1) {
                window.chooseMode("movie")
                event.accepted = true
            } else if (event.key === Qt.Key_2) {
                window.chooseMode("series")
                event.accepted = true
            }
        }
    }

    Rectangle {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 18
        width: hint.implicitWidth + 24
        height: 34
        radius: 17
        z: 100
        color: Qt.rgba(0.024, 0.027, 0.043, 0.78)
        border.width: 1
        border.color: Qt.rgba(1, 1, 1, 0.12)

        Text {
            id: hint
            anchors.centerIn: parent
            text: window.mode === "movie" ? "1 · Movie   2 · TV" : "1 · Movie   2 · TV"
            color: "#9a99a5"
            font.family: "Segoe UI"
            font.pixelSize: 11
        }
    }

    Timer {
        id: captureTimer
        interval: 7000
        repeat: false
        running: false
        onTriggered: {
            detail.grabToImage(function(result) {
                var ok = result.saveToFile(window.capturePath)
                console.log("[arc56] capture", ok ? "saved" : "failed", window.capturePath)
                Qt.callLater(Qt.quit)
            })
        }
    }
}
