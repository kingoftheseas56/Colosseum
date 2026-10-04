// UniverseBanner — one universe in the Universes world: a large full-width banner, its art
// shown along its whole length, its name level in Fraunces, what it holds beneath the name.
// The world's list is finite and curated, so each entry earns a banner, not a catalogue tile
// (Hemanth 2026-10-04: "Universes as large banners instead of the traditional tiles"). It
// descends from the Hall's ledger bar and keeps its manners: art under a left-weighted wash,
// a gold edge on hover or keyboard selection, a straight click enters. It does not change
// height on hover, so a moving pointer never shoves the page.
import QtQuick
import QtQuick.Effects

Item {
    id: banner

    property var entry: ({})               // { name, banner, chips: [{ t }], … }
    property bool keyboardSelected: false
    property bool hot: false               // pointer is here (the owning pile decides)
    signal hoverEntered()
    signal hoverExited()
    signal activated()

    readonly property bool lit: banner.hot || banner.keyboardSelected
    readonly property string holds: (banner.entry.chips || []).map(function(c) { return c.t }).join("  ·  ")

    width: parent ? parent.width : 0
    height: 132

    Theme { id: theme }

    Item {
        anchors.fill: parent

        Rectangle { anchors.fill: parent; radius: 14; color: "#14161d" }

        // the world's art, along the whole banner — masked so the corners are truly round
        // (CarouselSlide's pattern: a clipped Rectangle would leave the image's corners square)
        Image {
            id: artRaw
            anchors.fill: parent
            source: banner.entry.banner || ""
            asynchronous: true; cache: true
            sourceSize.width: 1600
            fillMode: Image.PreserveAspectCrop
            visible: false
        }
        Item {
            id: artMask
            anchors.fill: parent; visible: false; layer.enabled: true
            Rectangle { anchors.fill: parent; radius: 14; color: "white" }
        }
        MultiEffect {
            anchors.fill: parent
            source: artRaw
            maskEnabled: true; maskSource: artMask
            opacity: artRaw.status === Image.Ready ? (banner.lit ? 0.95 : 0.72) : 0
            Behavior on opacity { NumberAnimation { duration: 300 } }
        }
        // left-weighted wash: the name reads over any art, the art stays visible to the right
        Rectangle {
            anchors.fill: parent; radius: 14
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0;  color: Qt.rgba(0, 0, 0, 0.84) }
                GradientStop { position: 0.45; color: Qt.rgba(0, 0, 0, 0.46) }
                GradientStop { position: 1.0;  color: Qt.rgba(0, 0, 0, 0.12) }
            }
        }
        // the edge, drawn last so nothing paints over it
        Rectangle {
            anchors.fill: parent; radius: 14
            color: "transparent"
            border.width: 1
            border.color: banner.lit ? Qt.rgba(0.94, 0.77, 0.29, 0.65) : Qt.rgba(0.97, 0.97, 0.96, 0.10)
            Behavior on border.color { ColorAnimation { duration: 240 } }
        }

        Column {
            anchors.left: parent.left; anchors.leftMargin: 30
            anchors.right: enterRow.left; anchors.rightMargin: 24
            anchors.verticalCenter: parent.verticalCenter
            spacing: 8
            Text {
                width: parent.width
                text: banner.entry.name || ""
                color: theme.ink
                font.family: theme.display; font.pixelSize: 30
                elide: Text.ElideRight
            }
            Text {
                width: parent.width
                visible: text.length > 0
                text: banner.holds
                color: theme.inkDim
                font.family: theme.ui; font.pixelSize: 13
                elide: Text.ElideRight
            }
        }

        Row {
            id: enterRow
            anchors.right: parent.right; anchors.rightMargin: 28
            anchors.verticalCenter: parent.verticalCenter
            spacing: 8
            opacity: banner.lit ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: 250 } }
            Text { text: "Enter the universe"; color: theme.ink
                   font.family: theme.ui; font.pixelSize: 13; font.weight: Font.DemiBold }
            Text { text: "→"; color: theme.gold; font.pixelSize: 14 }
        }
    }

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onEntered: banner.hoverEntered()
        onExited: banner.hoverExited()
        onClicked: banner.activated()
    }

    FocusRing {
        anchors.fill: parent
        radius: 14
        shown: banner.keyboardSelected
    }
}
