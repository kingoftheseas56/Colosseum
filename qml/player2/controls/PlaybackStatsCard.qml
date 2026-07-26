import QtQuick

// THE SHIPPED PLAYER'S "Playback stats" card, carried over — not reinterpreted. Layout, palette,
// header, row order, "--" placeholders and the click-swallower are qml/PlayerPage.qml:3818-3882
// verbatim (Hemanth's directive, 2026-07-26: "use the same QML for playback stats, do not try to
// write anything from scratch when our old player already has it"). The ONLY translation is the
// data source: `stats` is fed by the shell from the Player 2 session instead of mpv properties.
// A value this engine does not measure yet reads "--", exactly as the old card renders an absent
// mpv property — absence stays visible instead of being faked.
Rectangle {
    id: statsCard
    property QtObject theme
    property bool open: false
    property var stats: ({})

    visible: open
    z: 18
    anchors.left: parent.left
    anchors.top: parent.top
    anchors.leftMargin: 28
    anchors.topMargin: 92
    width: 330
    height: statsColumn.implicitHeight + 28
    radius: 18
    color: Qt.rgba(0.04, 0.05, 0.07, 0.86)
    border.width: 1
    border.color: Qt.rgba(1, 1, 1, 0.14)

    function formatBitrate(v) {
        var n = Number(v || 0)
        if (!(n > 0)) return "--"
        return n >= 1e6 ? (n / 1e6).toFixed(1) + " Mbps" : Math.round(n / 1e3) + " kbps"
    }
    function statsValue(label) {
        var s = statsCard.stats || ({})
        if (label === "Resolution") {
            var w = Number(s.width || 0), h = Number(s.height || 0)
            return w > 0 && h > 0 ? (w + "x" + h) : "--"
        }
        if (label === "Frame rate") {
            var fps = Number(s.estimatedFps || s.containerFps || 0)
            return fps > 0 ? fps.toFixed(2) + " fps" : "--"
        }
        if (label === "Video codec") return s.videoCodec || "--"
        if (label === "Audio codec") return s.audioCodec || "--"
        if (label === "HW decode") return s.hwdec || "--"
        if (label === "Video bitrate") return formatBitrate(s.videoBitrate)
        if (label === "Audio bitrate") return formatBitrate(s.audioBitrate)
        if (label === "Dropped frames")
            return String(Number(s.frameDropDecoder || 0)) + " / " + String(Number(s.frameDropOutput || 0))
        if (label === "Cache buffering")
            return s.cacheBufferingState !== undefined && s.cacheBufferingState !== ""
                   ? Number(s.cacheBufferingState).toFixed(0) + "%" : "--"
        if (label === "Audio track") return s.audioTrack || "--"
        if (label === "Subtitle track") return s.subtitleTrack || "--"
        if (label === "Speed") return Number(s.speed || 1).toFixed(2) + "x"
        if (label === "Volume")
            return Math.round(Number(s.volume || 0)) + "%" + (s.muted ? " / muted" : "")
        return "--"
    }

    // Absorb background clicks so tapping the stats card never toggles pause (parity spec F2).
    MouseArea { anchors.fill: parent; hoverEnabled: true }

    Column {
        id: statsColumn
        x: 16
        y: 14
        width: parent.width - 32
        spacing: 5
        Text {
            width: parent.width
            text: "Playback stats"
            color: statsCard.theme ? statsCard.theme.gold : "#f0c44a"
            font.family: "Segoe UI"
            font.pixelSize: 11
            font.weight: Font.DemiBold
            font.capitalization: Font.AllUppercase
            font.letterSpacing: 1.8
        }
        Repeater {
            model: [
                "Resolution", "Frame rate", "Video codec", "Audio codec",
                "HW decode", "Video bitrate", "Audio bitrate", "Dropped frames",
                "Cache buffering", "Audio track", "Subtitle track", "Speed", "Volume"
            ]
            delegate: Row {
                required property string modelData
                width: statsColumn.width
                height: 18
                Text {
                    width: parent.width * 0.46
                    anchors.verticalCenter: parent.verticalCenter
                    text: modelData
                    color: statsCard.theme ? statsCard.theme.inkDimmer : "#9a99a5"
                    font.family: "Segoe UI"
                    font.pixelSize: 11
                    elide: Text.ElideRight
                }
                Text {
                    width: parent.width * 0.54
                    anchors.verticalCenter: parent.verticalCenter
                    text: statsCard.statsValue(modelData)
                    color: statsCard.theme ? statsCard.theme.ink : "#f7f7f5"
                    font.family: "Segoe UI"; font.features: ({ "tnum": 1 })
                    font.pixelSize: 11
                    horizontalAlignment: Text.AlignRight
                    elide: Text.ElideRight
                }
            }
        }
    }
}
