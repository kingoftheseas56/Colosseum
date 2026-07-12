// UniverseAtlas — the home page's universe hero, Atlas-folio form (spec: haven docs/
// superpowers/specs/2026-07-12-colosseum-universe-atlas-hero-design.md · mock rev 2 ratified
// 2026-07-12). Editorial folio + full-bleed banner per slide (SwipeView keeps drag), and a
// SPINE RAIL of universe thumbs below that replaces the dots: the active spine carries a gold
// progress line driven by the SAME timer as the auto-advance, so the page-turn is visible.
// NO poster card (banner IS the art statement — ratified). Ledger counts: bright count + dim
// medium; manga counts are DIFFERENT MANGA, never "volumes".
import QtQuick
import QtQuick.Controls
import "Universes.js" as Universes

Column {
    id: atlas

    property Item backdrop: null
    property real track: 0
    signal exploreRequested(string name)

    readonly property int interval: 6500
    spacing: 14

    Theme { id: theme }

    Glass {
        id: hero
        backdrop: atlas.backdrop
        track: atlas.track
        width: parent.width; height: 340; radius: 20
        tint: 0.06

        SwipeView {
            id: heroView
            anchors.fill: parent
            clip: true
            onCurrentIndexChanged: advance.restart()
            Repeater {
                model: Universes.universes
                delegate: Item {
                    id: slide
                    required property var modelData

                    // ---- banner: full-bleed; c1 stands in while art loads (house pipeline) ----
                    Rectangle {
                        anchors.fill: parent; radius: hero.radius; clip: true
                        color: slide.modelData.c1 ? slide.modelData.c1 : "#1a1410"
                        Image {
                            anchors.fill: parent
                            source: slide.modelData.banner
                            asynchronous: true; cache: true
                            fillMode: Image.PreserveAspectCrop
                            opacity: status === Image.Ready ? 1 : 0
                            Behavior on opacity { NumberAnimation { duration: 300 } }
                        }
                        // ghost monogram in the art region (beneath the seam scrim)
                        Text {
                            anchors.right: parent.right; anchors.rightMargin: 40
                            anchors.bottom: parent.bottom; anchors.bottomMargin: -60
                            text: slide.modelData.name.split(" ").map(function(w) { return w[0] }).join("")
                            font.family: theme.display; font.italic: true; font.pixelSize: 300
                            color: Qt.rgba(0.97, 0.97, 0.96, 0.05)
                        }
                        // seam: the editorial column bleeds over the art's left edge
                        Rectangle {
                            anchors.fill: parent
                            gradient: Gradient {
                                orientation: Gradient.Horizontal
                                GradientStop { position: 0.0;  color: Qt.rgba(0,0,0,0.88) }
                                GradientStop { position: 0.40; color: Qt.rgba(0,0,0,0.50) }
                                GradientStop { position: 1.0;  color: Qt.rgba(0,0,0,0.05) }
                            }
                        }
                    }

                    // ---- the folio column ----
                    Column {
                        anchors.left: parent.left; anchors.leftMargin: 44
                        anchors.top: parent.top; anchors.topMargin: 36
                        width: parent.width * 0.52
                        spacing: 10

                        Row {
                            spacing: 14
                            Text { text: "UNIVERSE"; color: theme.gold
                                   font.family: theme.ui; font.pixelSize: 11; font.letterSpacing: 3 }
                            Text {
                                text: String(heroView.currentIndex + 1).padStart(2, "0") + " / "
                                      + String(Universes.universes.length).padStart(2, "0")
                                color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 11
                                font.letterSpacing: 1.5
                            }
                        }
                        Text {
                            text: slide.modelData.name
                            color: theme.ink
                            font.family: theme.display; font.italic: true; font.pixelSize: 54
                        }
                        Text {
                            text: slide.modelData.blurb
                            color: theme.inkDim; font.family: theme.ui; font.pixelSize: 14
                            width: Math.min(parent.width, 400); wrapMode: Text.WordWrap
                        }

                        Item { width: 1; height: 2 }

                        // ---- the ledger: medium dim-left, count bold-right, hairline rules ----
                        Column {
                            width: Math.min(parent.width, 360)
                            Repeater {
                                model: Universes.ledger(slide.modelData.chips)
                                delegate: Item {
                                    id: lrow
                                    required property var modelData
                                    required property int index
                                    width: parent.width; height: 27
                                    Rectangle {
                                        visible: lrow.index > 0
                                        anchors.top: parent.top
                                        width: parent.width; height: 1
                                        color: Qt.rgba(0.97, 0.97, 0.96, 0.14)
                                    }
                                    Text {
                                        anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
                                        text: lrow.modelData.medium
                                        color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 13
                                        font.letterSpacing: 0.5
                                    }
                                    Text {
                                        anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                                        text: lrow.modelData.count
                                        color: theme.ink; font.family: theme.ui; font.pixelSize: 14
                                        font.weight: Font.Bold
                                    }
                                }
                            }
                        }

                        Item { width: 1; height: 4 }

                        // ---- explore (interaction unchanged from the old hero) ----
                        Rectangle {
                            radius: 12; height: 46; width: exploreRow.implicitWidth + 44
                            gradient: Gradient {
                                GradientStop { position: 0; color: exMa.containsMouse ? Qt.rgba(1,1,1,0.23) : Qt.rgba(1,1,1,0.14) }
                                GradientStop { position: 1; color: exMa.containsMouse ? Qt.rgba(1,1,1,0.10) : Qt.rgba(1,1,1,0.05) }
                            }
                            border.width: 1
                            border.color: exMa.containsMouse ? Qt.rgba(0.94,0.77,0.29,0.85) : Qt.rgba(1,1,1,0.26)
                            Behavior on border.color { ColorAnimation { duration: 160 } }
                            Row {
                                id: exploreRow; anchors.centerIn: parent; spacing: 10
                                Text { text: "Explore the universe"; color: theme.ink
                                    font.family: theme.ui; font.pixelSize: 14; font.weight: Font.DemiBold
                                    anchors.verticalCenter: parent.verticalCenter }
                                Text { text: "→"; color: theme.gold; font.pixelSize: 16
                                    anchors.verticalCenter: parent.verticalCenter
                                    transform: Translate { x: exMa.containsMouse ? 3 : 0 } }
                            }
                            MouseArea {
                                id: exMa; anchors.fill: parent
                                hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                onClicked: atlas.exploreRequested(Universes.universes[heroView.currentIndex].name)
                            }
                        }
                    }
                }
            }
        }

        // ONE timer drives both the page-turn and the active spine's progress line
        Timer {
            id: advance
            interval: atlas.interval; running: true; repeat: true
            onTriggered: heroView.currentIndex = (heroView.currentIndex + 1) % Universes.universes.length
        }
    }

    // ---- the spine rail: pagination IS the collection (dots are dead) ----
    Row {
        id: rail
        width: parent.width
        spacing: 14
        Repeater {
            model: Universes.universes
            delegate: Item {
                id: spine
                required property var modelData
                required property int index
                readonly property bool active: heroView.currentIndex === index
                width: (rail.width - rail.spacing * (Universes.universes.length - 1)) / Universes.universes.length
                height: 72

                Rectangle {
                    id: spineBody
                    anchors.fill: parent
                    anchors.topMargin: spine.active ? 0 : 3   // active lifts
                    radius: 10; clip: true
                    opacity: spine.active ? 1.0 : (spMa.containsMouse ? 0.85 : 0.58)
                    border.width: 1
                    border.color: spine.active ? Qt.rgba(0.94,0.77,0.29,0.5) : Qt.rgba(0.97,0.97,0.96,0.10)
                    gradient: Gradient {
                        GradientStop { position: 0; color: spine.modelData.c1 || "#1a1410" }
                        GradientStop { position: 1; color: "#0c0e13" }
                    }
                    Behavior on opacity { NumberAnimation { duration: 250 } }
                    Behavior on anchors.topMargin { NumberAnimation { duration: 250 } }

                    Image {
                        anchors.fill: parent
                        source: spine.modelData.banner
                        asynchronous: true; cache: true
                        fillMode: Image.PreserveAspectCrop
                        opacity: status === Image.Ready ? 0.55 : 0
                        Behavior on opacity { NumberAnimation { duration: 300 } }
                    }
                    Rectangle {   // bottom scrim under the name
                        anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                        height: 34
                        gradient: Gradient {
                            GradientStop { position: 0; color: "transparent" }
                            GradientStop { position: 1; color: Qt.rgba(0,0,0,0.65) }
                        }
                    }
                    Text {
                        anchors.left: parent.left; anchors.leftMargin: 12
                        anchors.bottom: parent.bottom; anchors.bottomMargin: 7
                        width: parent.width - 24
                        text: spine.modelData.name
                        color: theme.ink; font.family: theme.display; font.pixelSize: 14
                        elide: Text.ElideRight
                    }

                    // the auto-advance made visible: fills over exactly one interval
                    Rectangle {
                        id: prog
                        anchors.left: parent.left; anchors.bottom: parent.bottom
                        height: 2.5; width: 0
                        color: theme.gold
                        visible: spine.active
                        NumberAnimation {
                            id: progAnim
                            target: prog; property: "width"
                            from: 0; to: spine.width
                            duration: atlas.interval
                        }
                    }
                    Connections {
                        target: heroView
                        function onCurrentIndexChanged() {
                            progAnim.stop()
                            prog.width = 0
                            if (spine.active) progAnim.restart()
                        }
                    }
                    Component.onCompleted: if (spine.active) progAnim.restart()
                }
                MouseArea {
                    id: spMa; anchors.fill: parent
                    hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                    onClicked: heroView.currentIndex = spine.index
                }
            }
        }
    }
}
