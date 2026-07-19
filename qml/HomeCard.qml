// HomeCard — one glass card, three shapes. A frosted, edge-lit frame that hover-lifts,
// carrying art (or a typographic book jacket), an optional world-tag pill, a gold progress
// bar, and a title/sub caption beneath. Used by every HomeRail.
//
//   landscape  296x167 art   — video / Theatre / Continue
//   portrait   146x216 art   — Tankoban volumes
//   jacket     146x216 panel — Biblio (no cover CDN in our lanes; a colored spine + title/author)
//
// Gold discipline: gold shows ONLY on the progress fill. Per-world tag is a faint identity cue.

import QtQuick
import QtQuick.Effects

Item {
    id: card

    // ── public API ──
    property string shape: "landscape"      // "landscape" | "portrait" | "jacket"
    property url art: ""
    property string worldTag: ""
    property string titleText: ""
    property string subText: ""
    property real progress: 0
    property string jacketTitle: ""
    property string jacketAuthor: ""
    property color jacketColor: "#2b2350"
    signal activated()

    Theme { id: theme }

    readonly property bool land: shape === "landscape"
    readonly property bool jacket: shape === "jacket"
    readonly property int thumbW: land ? 296 : 146
    readonly property int thumbH: land ? 167 : 216

    width: thumbW
    height: col.implicitHeight

    // hover-lift
    property real lift: cardMa.containsMouse ? -5 : 0
    Behavior on lift { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
    transform: Translate { y: card.lift }

    Column {
        id: col
        width: card.thumbW
        spacing: 10

        // ── the frame: art thumb OR typographic jacket ──
        Item {
            width: card.thumbW; height: card.thumbH

            // art thumb (landscape / portrait)
            Rectangle {
                anchors.fill: parent
                visible: !card.jacket
                radius: 13; clip: true
                color: theme.glassTint
                border.width: 1
                border.color: cardMa.containsMouse ? theme.edge : theme.edgeSoft

                Image {
                    anchors.fill: parent
                    source: card.art
                    asynchronous: true; cache: true
                    fillMode: Image.PreserveAspectCrop
                    sourceSize.width: card.thumbW * 2
                    sourceSize.height: card.thumbH * 2
                    visible: status === Image.Ready
                }
            }

            // typographic book jacket (Biblio)
            Rectangle {
                anchors.fill: parent
                visible: card.jacket
                radius: 13; clip: true
                border.width: 1
                border.color: cardMa.containsMouse ? theme.edge : theme.edgeSoft
                gradient: Gradient {
                    orientation: Gradient.Vertical
                    GradientStop { position: 0; color: card.jacketColor }
                    GradientStop { position: 1; color: Qt.darker(card.jacketColor, 2.4) }
                }
                // small spine mark, top-left
                Rectangle { x: 14; y: 14; width: 22; height: 2; color: Qt.rgba(1, 1, 1, 0.6) }
                Column {
                    anchors.left: parent.left; anchors.right: parent.right
                    anchors.bottom: parent.bottom; anchors.margins: 14
                    spacing: 6
                    Text {
                        width: parent.width
                        text: card.jacketTitle; color: theme.ink
                        font.family: theme.displaySans; font.pixelSize: 17
                        font.weight: Font.ExtraBold; lineHeight: 1.05
                        wrapMode: Text.WordWrap; maximumLineCount: 3; elide: Text.ElideRight
                    }
                    Text {
                        width: parent.width
                        text: card.jacketAuthor.toUpperCase(); color: Qt.rgba(1, 1, 1, 0.6)
                        font.family: theme.displaySans; font.pixelSize: 10; font.letterSpacing: 1.1
                        elide: Text.ElideRight; maximumLineCount: 1
                    }
                }
            }

            // world-tag pill (top-left)
            Rectangle {
                visible: card.worldTag.length > 0
                anchors.top: parent.top; anchors.left: parent.left; anchors.margins: 8
                height: 20; radius: 10
                width: wtagText.implicitWidth + 16
                color: Qt.rgba(6 / 255, 7 / 255, 12 / 255, 0.55)
                border.width: 1; border.color: theme.edgeSoft
                Text {
                    id: wtagText
                    anchors.centerIn: parent
                    text: card.worldTag.toUpperCase(); color: theme.ink
                    font.family: theme.displaySans; font.pixelSize: 9
                    font.weight: Font.Bold; font.letterSpacing: 1.0
                }
            }

            // gold progress bar (bottom) — the only gold on the card
            Rectangle {
                visible: card.progress > 0 && !card.jacket
                anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                height: 3
                color: Qt.rgba(0, 0, 0, 0.55)
                Rectangle {
                    width: parent.width * Math.max(0, Math.min(1, card.progress))
                    height: parent.height; color: theme.gold
                }
            }
        }

        // ── caption ──
        Column {
            width: card.thumbW
            spacing: 2
            Text {
                width: parent.width
                visible: card.titleText.length > 0
                text: card.titleText; color: theme.ink
                font.family: theme.displaySans; font.pixelSize: 14; font.weight: Font.DemiBold
                elide: Text.ElideRight; maximumLineCount: 1
            }
            Text {
                width: parent.width
                visible: card.subText.length > 0
                text: card.subText; color: theme.inkDimmer
                font.family: theme.ui; font.pixelSize: 12
                elide: Text.ElideRight; maximumLineCount: 1
            }
        }
    }

    MouseArea {
        id: cardMa
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: card.activated()
    }

    Accessible.role: Accessible.Button
    Accessible.name: card.titleText + (card.subText.length ? " — " + card.subText : "")
}
