// UniverseLedgerBar — one bar of the ratified LEDGER STACK (the Hall of Worlds' second
// form, Hemanth 2026-07-12): slim full-width bar, gold index numeral, name LEVEL in
// Fraunces, banner art washed dark behind. `open` breathes it taller to reveal the kicker
// and "Enter →"; a straight click enters the world untouched (hover stays an enhancement).
// Extracted 2026-10-04 from UniverseHallPage's pile, byte-for-byte, so the Universes world
// can house the same bar. The component is the BAR ONLY — no scroller, wallpaper or page
// chrome — and it never decides when it opens: the owning pile drives `open` (hover,
// keyboard selection, the walk-still rule); the bar just reports pointer hover and
// activation upward. Still never a tile grid (standing constraint).
import QtQuick

Item {
    id: bar

    property var entry: ({})               // the row's universe: { name, banner, ... }
    property int numeral: 0                // the plate/kicker number — the pile's own order
    property bool open: false              // the breathe (hover or keyboard selection)
    property int restHeight: 64            // the pile's geometry rides with the bar
    property int openHeight: 98
    property bool keyboardSelected: false  // house FocusRing (the Hall breathes instead)
    signal hoverEntered()
    signal hoverExited()
    signal activated()

    width: parent ? parent.width : 0
    height: bar.open ? bar.openHeight : bar.restHeight
    Behavior on height { NumberAnimation { duration: 340; easing.type: Easing.OutCubic } }

    Theme { id: theme }

    Rectangle {
        anchors.fill: parent
        radius: 14; clip: true
        color: "#14161d"   // the per-IP accent went with the baked list
        border.width: 1
        border.color: bar.open ? Qt.rgba(0.94, 0.77, 0.29, 0.65)
                               : Qt.rgba(0.97, 0.97, 0.96, 0.10)
        Behavior on border.color { ColorAnimation { duration: 240 } }

        // the world's art, washed dark so the type carries the bar
        Image {
            anchors.fill: parent
            source: bar.entry.banner || ""
            asynchronous: true; cache: true
            fillMode: Image.PreserveAspectCrop
            opacity: status === Image.Ready ? (bar.open ? 0.55 : 0.30) : 0
            Behavior on opacity { NumberAnimation { duration: 300 } }
        }
        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: Qt.rgba(0, 0, 0, 0.74) }
                GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, bar.open ? 0.45 : 0.28) }
            }
        }

        // ---- title row (always level, always present) ----
        Item {
            anchors.left: parent.left; anchors.right: parent.right
            anchors.top: parent.top
            height: bar.restHeight
            Text {   // the index numeral — library plate
                id: plate
                anchors.left: parent.left; anchors.leftMargin: 22
                anchors.verticalCenter: parent.verticalCenter
                text: (bar.numeral + 1 < 10 ? "0" : "") + (bar.numeral + 1)
                color: Qt.rgba(0.94, 0.77, 0.29, 0.8)
                font.family: theme.display; font.pixelSize: 14; font.italic: true
            }
            Text {   // the name reads level — no tilted head
                anchors.left: plate.right; anchors.leftMargin: 18
                anchors.right: enterRow.left; anchors.rightMargin: 18
                anchors.verticalCenter: parent.verticalCenter
                text: bar.entry.name || ""
                color: theme.ink
                font.family: theme.display; font.pixelSize: 21
                elide: Text.ElideRight
            }
            Row {
                id: enterRow
                anchors.right: parent.right; anchors.rightMargin: 22
                anchors.verticalCenter: parent.verticalCenter
                spacing: 8
                opacity: bar.open ? 1 : 0
                Behavior on opacity { NumberAnimation { duration: 250 } }
                Text { text: "Enter the tribute"; color: theme.ink
                       font.family: theme.ui; font.pixelSize: 13; font.weight: Font.DemiBold }
                Text { text: "→"; color: theme.gold; font.pixelSize: 14 }
            }
        }

        // ---- the breathe: the world introduces itself under its own title ----
        Column {
            anchors.left: parent.left; anchors.leftMargin: 68
            anchors.right: parent.right; anchors.rightMargin: 24
            anchors.top: parent.top; anchors.topMargin: bar.restHeight
            spacing: 8
            opacity: bar.open ? 1 : 0
            visible: opacity > 0.01
            Behavior on opacity { NumberAnimation { duration: 260 } }
            // the kicker is derived from the pile's own order, not from the row — the
            // blurb and the media ledger below it are GONE with the baked list, because
            // the roster supplies neither and faking either would put words in a
            // universe's mouth.
            Text { text: "TRIBUTE  ·  " + ((bar.numeral + 1 < 10 ? "0" : "") + (bar.numeral + 1))
                   color: theme.gold; font.family: theme.ui
                   font.pixelSize: 10; font.letterSpacing: 3 }
        }
    }

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onEntered: bar.hoverEntered()
        onExited: bar.hoverExited()
        onClicked: bar.activated()
    }

    // the house focus look (world pages; the Hall keeps its breathe-only selection)
    FocusRing {
        anchors.fill: parent
        radius: 14
        shown: bar.keyboardSelected
    }
}
