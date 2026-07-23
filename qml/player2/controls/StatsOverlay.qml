import QtQuick

// The playback stats card, fed by the session's typed diagnostics() map (refreshed 1 Hz while open).
// Every value is a named typed field — no generic property string lookup, matching the current
// player's stats panel content.
Item {
    id: stats

    property var session
    property QtObject theme
    property bool open: false
    property var data: ({})

    readonly property color panelColor: theme ? theme.panel : Qt.rgba(0.04, 0.05, 0.07, 0.86)
    readonly property color gold: theme ? theme.gold : "#f0c44a"
    readonly property color ink: theme ? theme.ink : "#f7f7f5"
    readonly property color inkDimmer: theme ? theme.inkDimmer : "#9a99a5"

    visible: open
    implicitWidth: 330
    implicitHeight: card.implicitHeight

    function num(key, digits) {
        var v = data[key]
        return (v === undefined || v === null) ? "—" : Number(v).toFixed(digits === undefined ? 0 : digits)
    }
    function str(key) {
        var v = data[key]
        return (v === undefined || v === null || v === "") ? "—" : String(v)
    }

    readonly property var rows: [
        { label: "State", value: str("state") },
        { label: "Video codec", value: str("videoCodec") },
        { label: "HW decode", value: str("hardwareFormat") },
        { label: "Input format", value: str("inputFormat") },
        { label: "Colour", value: str("colorConversion") },
        { label: "Presented", value: num("presented") },
        { label: "Dropped", value: num("dropped") },
        { label: "Late drops", value: num("scheduledLateDrops") },
        { label: "A/V p95", value: num("avP95Ms", 1) + " ms" },
        { label: "CPU transfers", value: num("cpuTransfers") },
        { label: "Device errors", value: num("deviceErrors") },
        { label: "Audio format", value: str("audioFormat") },
        { label: "Audio queue", value: num("audioQueueMs", 1) + " ms" },
        { label: "Underruns", value: num("audioUnderruns") },
        { label: "Loudness lat.", value: num("normalizationLatencyMs", 1) + " ms" },
        { label: "Device state", value: str("deviceLostReason") }
    ]

    Timer {
        running: stats.open && stats.session
        interval: 1000
        repeat: true
        triggeredOnStart: true
        onTriggered: if (stats.session) stats.data = stats.session.diagnostics()
    }

    Rectangle {
        id: card
        anchors.fill: parent
        implicitHeight: col.implicitHeight + 28
        color: stats.panelColor
        radius: 18
        border.width: 1
        border.color: Qt.rgba(1, 1, 1, 0.10)

        Column {
            id: col
            anchors.fill: parent
            anchors.margins: 14
            spacing: 0

            Text {
                text: "PLAYBACK STATS"
                color: stats.gold
                font.family: "Segoe UI"
                font.pixelSize: 11
                font.letterSpacing: 1.8
                bottomPadding: 8
            }
            Repeater {
                model: stats.rows
                Item {
                    required property var modelData
                    width: col.width
                    height: 18
                    Text {
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        text: modelData.label
                        color: stats.inkDimmer
                        font.family: "Segoe UI"
                        font.pixelSize: 12
                    }
                    Text {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width * 0.56
                        horizontalAlignment: Text.AlignRight
                        elide: Text.ElideLeft
                        text: modelData.value
                        color: stats.ink
                        font.family: "Segoe UI"
                        font.pixelSize: 12
                        font.features: ({ "tnum": 1 })
                    }
                }
            }
        }
    }
}
