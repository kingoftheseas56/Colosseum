// UniverseHallPage — the universe collection's "see all": THE HALL OF WORLDS. Not a grid,
// not tiles (Hemanth's one constraint, 2026-07-12): the viewport is a great shelf and every
// universe is a SPINE — banner art washed dark, name in tall rotated Fraunces, an index
// numeral at the crown. Hover breathes a spine open (it widens, neighbors lean away) to
// reveal the kicker, blurb, media ledger and "Enter →"; click enters the world (hover is an
// enhancement — a straight click works untouched). Data: Universes.universes, verbatim.
import QtQuick
import "Universes.js" as Universes

Item {
    id: root
    anchors.fill: parent

    // shell contract (mirrors the genre-page layers)
    property Item backdrop: null
    signal backRequested()
    signal minimizeRequested()
    signal closeRequested()
    signal exploreRequested(string name)   // a spine → win.openUniverse

    Theme { id: theme }

    property int hovered: -1              // which spine breathes (-1 = the shelf at rest)

    // ---- the wall the hall stands in ----
    Item {
        anchors.fill: parent
        ShaderEffectSource {
            anchors.fill: parent
            sourceItem: root.backdrop
            live: true
            hideSource: false
            visible: root.backdrop !== null
        }
        Image { anchors.fill: parent; visible: root.backdrop === null
                source: "../assets/wallpaper/captured-motion.jpg"
                fillMode: Image.PreserveAspectCrop; cache: true }
        Rectangle { anchors.fill: parent; color: Qt.rgba(0.02, 0.025, 0.045, 0.9) }
    }

    // ---- header strip ----
    Column {
        id: head
        anchors.left: parent.left; anchors.leftMargin: theme.margin
        anchors.top: parent.top; anchors.topMargin: 30
        spacing: 6
        z: 20
        Text { text: "THE COLLECTION"; color: theme.gold
               font.family: theme.ui; font.pixelSize: 11; font.letterSpacing: 3 }
        Row {
            spacing: 14
            Text { text: "Hall of Worlds"; color: theme.ink
                   font.family: theme.display; font.pixelSize: 34 }
            Text { text: Universes.universes.length + " universes"
                   color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 13
                   anchors.baseline: parent.children[0].baseline }
        }
    }
    BackAction {
        x: theme.margin; y: 96; z: 20
        onTriggered: root.backRequested()
    }
    Row {
        z: 20
        anchors.right: parent.right; anchors.rightMargin: theme.margin; y: 34
        spacing: 20
        Item {
            width: 22; height: 22
            Image { anchors.fill: parent; source: "../assets/icons/minimize.svg"
                sourceSize.width: 22; sourceSize.height: 22; fillMode: Image.PreserveAspectFit
                opacity: minMa.containsMouse ? 1.0 : 0.72 }
            MouseArea { id: minMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                onClicked: root.minimizeRequested() }
        }
        Item {
            width: 22; height: 22
            Image { anchors.fill: parent; source: "../assets/icons/power.svg"
                sourceSize.width: 22; sourceSize.height: 22; fillMode: Image.PreserveAspectFit
                opacity: clMa.containsMouse ? 1.0 : 0.72 }
            MouseArea { id: clMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                onClicked: root.closeRequested() }
        }
    }

    // ---- THE SHELF — spines shoulder to shoulder, one per world ----
    Row {
        id: shelf
        anchors.left: parent.left; anchors.right: parent.right
        anchors.top: parent.top; anchors.topMargin: 140
        anchors.bottom: parent.bottom; anchors.bottomMargin: 34
        anchors.leftMargin: theme.margin; anchors.rightMargin: theme.margin
        spacing: 10

        // geometry: the breathing spine takes `open` width, the rest share what remains
        readonly property int count: Universes.universes.length
        readonly property real openW: Math.min(500, width * 0.34)
        readonly property real restW: root.hovered < 0
            ? (width - spacing * (count - 1)) / count
            : (width - spacing * (count - 1) - openW) / (count - 1)

        Repeater {
            model: Universes.universes
            delegate: Item {
                id: spine
                required property var modelData
                required property int index
                readonly property bool open: root.hovered === spine.index
                width: open ? shelf.openW : shelf.restW
                height: shelf.height
                Behavior on width { NumberAnimation { duration: 340; easing.type: Easing.OutCubic } }

                Rectangle {
                    anchors.fill: parent
                    radius: 14; clip: true
                    color: spine.modelData.c1 || "#14161d"
                    border.width: 1
                    border.color: spine.open ? Qt.rgba(0.94, 0.77, 0.29, 0.65)
                                             : Qt.rgba(0.97, 0.97, 0.96, 0.10)
                    Behavior on border.color { ColorAnimation { duration: 240 } }

                    // the world's art, washed dark so the type carries the spine
                    Image {
                        anchors.fill: parent
                        source: spine.modelData.banner
                        asynchronous: true; cache: true
                        fillMode: Image.PreserveAspectCrop
                        opacity: status === Image.Ready ? (spine.open ? 0.6 : 0.34) : 0
                        Behavior on opacity { NumberAnimation { duration: 300 } }
                    }
                    Rectangle {
                        anchors.fill: parent
                        gradient: Gradient {
                            GradientStop { position: 0.0; color: Qt.rgba(0, 0, 0, 0.30) }
                            GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, spine.open ? 0.78 : 0.55) }
                        }
                    }

                    // ---- CLOSED face: index numeral at the crown, name running up the spine ----
                    Item {
                        anchors.fill: parent
                        opacity: spine.open ? 0 : 1
                        visible: opacity > 0.01
                        Behavior on opacity { NumberAnimation { duration: 200 } }
                        Text {   // the crown numeral — library plate
                            anchors.top: parent.top; anchors.topMargin: 18
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: (spine.index + 1 < 10 ? "0" : "") + (spine.index + 1)
                            color: Qt.rgba(0.94, 0.77, 0.29, 0.8)
                            font.family: theme.display; font.pixelSize: 15; font.italic: true
                        }
                        Text {   // the title climbs the spine, bottom to top
                            anchors.centerIn: parent
                            rotation: -90
                            width: spine.height - 130
                            text: spine.modelData.name
                            color: theme.ink
                            font.family: theme.display; font.pixelSize: 26
                            elide: Text.ElideRight
                            horizontalAlignment: Text.AlignHCenter
                        }
                    }

                    // ---- OPEN face: the world introduces itself ----
                    Item {
                        anchors.fill: parent
                        anchors.margins: 28
                        opacity: spine.open ? 1 : 0
                        visible: opacity > 0.01
                        Behavior on opacity { NumberAnimation { duration: 260 } }
                        Column {
                            anchors.left: parent.left; anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            spacing: 10
                            Text { text: "UNIVERSE  ·  " + ((spine.index + 1 < 10 ? "0" : "") + (spine.index + 1))
                                   color: theme.gold; font.family: theme.ui
                                   font.pixelSize: 11; font.letterSpacing: 3 }
                            Text {
                                width: parent.width
                                text: spine.modelData.name
                                color: theme.ink; font.family: theme.display; font.pixelSize: 38
                                wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight
                            }
                            Text {
                                width: parent.width
                                text: spine.modelData.blurb
                                color: theme.inkDim; font.family: theme.ui; font.pixelSize: 13
                                lineHeight: 1.4
                                wrapMode: Text.WordWrap; maximumLineCount: 3; elide: Text.ElideRight
                            }
                            // the ledger line: bright count · dim medium (the house rule)
                            Text {
                                width: parent.width
                                textFormat: Text.StyledText
                                font.family: theme.ui; font.pixelSize: 13
                                text: (spine.modelData.chips || []).map(function(c) {
                                    var s = String(c.t), i = s.indexOf(" ")
                                    var first = i < 0 ? s : s.substring(0, i)
                                    if (!/^\d/.test(first)) return "<font color='#c9c8d0'>" + s + "</font>"
                                    return "<b><font color='#f7f7f5'>" + first + "</font></b> <font color='#c9c8d0'>"
                                           + s.substring(i + 1) + "</font>"
                                }).join("<font color='#8b8a94'>   ·   </font>")
                            }
                            Item { width: 1; height: 4 }
                            Row {
                                spacing: 8
                                Text { text: "Enter the universe"; color: theme.ink
                                       font.family: theme.ui; font.pixelSize: 14; font.weight: Font.DemiBold }
                                Text { text: "→"; color: theme.gold; font.pixelSize: 15 }
                            }
                        }
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onEntered: root.hovered = spine.index
                    onExited: if (root.hovered === spine.index) root.hovered = -1
                    onClicked: root.exploreRequested(spine.modelData.name)
                }
            }
        }
    }
}
