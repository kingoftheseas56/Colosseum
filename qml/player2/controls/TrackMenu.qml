import QtQuick

// A glass track-selection popover (audio or subtitles), with a sync/delay row. It renders typed track
// rows from the session and reports a typed pick — the shell/session does the switching. Plain
// QtQuick only (no Controls dependency); its own click-swallower keeps taps off the video.
Item {
    id: menu

    property QtObject theme
    property bool open: false
    property string title: "Tracks"
    property var tracks: []
    property int selectedIndex: -1   // selected stream index; -1 = none / off
    property bool allowOff: false
    property real syncValue: 0.0
    property string syncLabel: "SYNC"

    signal picked(int streamIndex)   // -1 for Off
    signal syncChanged(real seconds)

    readonly property color panelColor: theme ? theme.panel : Qt.rgba(0.04, 0.05, 0.07, 0.94)
    readonly property color gold: theme ? theme.gold : "#f0c44a"
    readonly property color ink: theme ? theme.ink : "#f7f7f5"
    readonly property color inkDim: theme ? theme.inkDim : "#c9c8d0"
    readonly property color inkDimmer: theme ? theme.inkDimmer : "#9a99a5"

    implicitWidth: 320
    implicitHeight: panel.implicitHeight
    visible: open
    opacity: open ? 1 : 0
    Behavior on opacity { NumberAnimation { duration: 120 } }

    function metaFor(track) {
        var bits = []
        if (track.language) bits.push(String(track.language).toUpperCase())
        if (track.codec) bits.push(String(track.codec))
        if (track.forced) bits.push("Forced")
        if (track.default) bits.push("Default")
        return bits.join("  ·  ")
    }

    Rectangle {
        id: panel
        anchors.fill: parent
        implicitHeight: layout.implicitHeight + 24
        color: menu.panelColor
        radius: 14
        border.width: 1
        border.color: Qt.rgba(1, 1, 1, 0.12)

        MouseArea { anchors.fill: parent; hoverEnabled: true } // swallow clicks off the video

        Column {
            id: layout
            anchors.fill: parent
            anchors.margins: 12
            spacing: 0

            Text {
                text: menu.title + "   " + menu.tracks.length
                color: menu.ink
                font.family: "Segoe UI"
                font.pixelSize: 13
                font.weight: Font.DemiBold
                bottomPadding: 8
            }
            Rectangle { width: parent.width; height: 1; color: Qt.rgba(1, 1, 1, 0.08) }

            // Off row (subtitles)
            Item {
                width: parent.width
                height: menu.allowOff ? 40 : 0
                visible: menu.allowOff
                Row {
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 12
                    Rectangle {
                        width: 12; height: 12; radius: 6
                        anchors.verticalCenter: parent.verticalCenter
                        color: menu.selectedIndex < 0 ? menu.gold : "transparent"
                        border.width: 1; border.color: menu.selectedIndex < 0 ? menu.gold : Qt.rgba(1, 1, 1, 0.3)
                    }
                    Text { text: "Off"; color: menu.ink; font.family: "Segoe UI"; font.pixelSize: 13
                           anchors.verticalCenter: parent.verticalCenter }
                }
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: menu.picked(-1) }
            }

            // Track rows
            Repeater {
                model: menu.tracks
                Item {
                    required property var modelData
                    width: layout.width
                    height: 46
                    readonly property bool sel: menu.selectedIndex === modelData.index
                    Rectangle {
                        anchors.fill: parent
                        anchors.topMargin: 2; anchors.bottomMargin: 2
                        radius: 8
                        color: rowArea.containsMouse || sel ? Qt.rgba(1, 1, 1, 0.10) : "transparent"
                    }
                    Rectangle {
                        id: dot
                        width: 12; height: 12; radius: 6
                        anchors.left: parent.left; anchors.leftMargin: 4
                        anchors.verticalCenter: parent.verticalCenter
                        color: sel ? menu.gold : "transparent"
                        border.width: 1; border.color: sel ? menu.gold : Qt.rgba(1, 1, 1, 0.3)
                    }
                    Column {
                        anchors.left: dot.right; anchors.leftMargin: 12
                        anchors.right: parent.right; anchors.rightMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 2
                        Text {
                            width: parent.width; elide: Text.ElideRight
                            text: modelData.title && modelData.title.length > 0 ? modelData.title
                                  : (menu.title + " " + (modelData.index))
                            color: menu.ink; font.family: "Segoe UI"; font.pixelSize: 13
                        }
                        Text {
                            width: parent.width; elide: Text.ElideRight
                            text: menu.metaFor(modelData)
                            visible: text.length > 0
                            color: menu.inkDimmer; font.family: "Segoe UI"; font.pixelSize: 11
                        }
                    }
                    MouseArea {
                        id: rowArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: menu.picked(modelData.index)
                    }
                }
            }

            Text {
                width: parent.width
                visible: menu.tracks.length === 0
                topPadding: 8; bottomPadding: 8
                text: "No " + menu.title.toLowerCase() + " tracks"
                color: menu.inkDimmer; font.family: "Segoe UI"; font.pixelSize: 12
            }

            // SYNC / delay row
            Rectangle { width: parent.width; height: 1; color: Qt.rgba(1, 1, 1, 0.08) }
            Item {
                width: parent.width
                height: 44
                Text {
                    anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
                    text: menu.syncLabel
                    color: menu.inkDimmer; font.family: "Segoe UI"; font.pixelSize: 11
                    font.letterSpacing: 1.5
                }
                Row {
                    anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                    spacing: 8
                    component DelayButton: Rectangle {
                        property string glyph: ""
                        signal tapped()
                        width: 30; height: 26; radius: 6
                        color: da.containsMouse ? Qt.rgba(1, 1, 1, 0.10) : Qt.rgba(1, 1, 1, 0.05)
                        Text { anchors.centerIn: parent; text: glyph; color: menu.ink
                               font.family: "Segoe UI"; font.pixelSize: 13; font.weight: Font.DemiBold }
                        MouseArea { id: da; anchors.fill: parent; hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor; onClicked: parent.tapped() }
                    }
                    DelayButton { glyph: "−"; onTapped: menu.syncChanged(menu.syncValue - 0.1) }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 64; horizontalAlignment: Text.AlignHCenter
                        text: (menu.syncValue >= 0 ? "+" : "") + menu.syncValue.toFixed(2) + "s"
                        color: menu.ink; font.family: "Segoe UI"; font.pixelSize: 12
                        font.features: ({ "tnum": 1 })
                    }
                    DelayButton { glyph: "+"; onTapped: menu.syncChanged(menu.syncValue + 0.1) }
                    DelayButton { glyph: "0"; onTapped: menu.syncChanged(0) }
                }
            }
        }
    }
}
