// UniverseBanner — one universe in the Universes world: a large full-width PORTAL. Its art
// runs along its whole length under a left-weighted wash; on the left an identity rail holds
// the universe's own mark (the Straw Hat, the DC bullet, 007); then its name in Fraunces, two
// lines of its About copy (the same blurb its page prints, never written for the banner) and
// what it holds. The world's list is finite and curated, so each entry earns a banner, not a
// catalogue tile (Hemanth 2026-10-04: "Universes as large banners instead of the traditional
// tiles"). Portal treatment 2026-10-05 from Preflight's Universes world prototype
// (Preflight-Architect/arcs/58-universes-and-tributes/prototypes/universes-world-html-mock).
// It keeps the Hall bar's manners: a gold edge on hover or keyboard selection, a straight
// click enters, and it never changes height on hover, so a moving pointer cannot shove the page.
import QtQuick
import QtQuick.Effects

Item {
    id: banner

    property var entry: ({})               // { name, banner, logo, logoShape, blurb, chips: [{ t }], … }
    property bool keyboardSelected: false
    property bool hot: false               // pointer is here (the owning pile decides)
    property bool artActive: true          // owner prioritizes visible/nearby banners
    property bool artRequested: false
    onArtActiveChanged: if (artActive) artRequested = true
    Component.onCompleted: if (artActive) artRequested = true
    signal hoverEntered()
    signal hoverExited()
    signal activated()

    readonly property bool lit: banner.hot || banner.keyboardSelected
    readonly property string holds: (banner.entry.chips || []).map(function(c) { return c.t }).join("  ·  ")
    // the mark's box inside the 80x74 rail slot: wide wordmarks, tall emblems, square badges
    readonly property string shape: banner.entry.logoShape || "square"
    readonly property real markW: banner.shape === "tall" ? 48 : (banner.shape === "wide" ? 82 : 66)
    readonly property real markH: banner.shape === "tall" ? 70 : (banner.shape === "wide" ? 56 : 66)
    readonly property bool hasMark: String(banner.entry.logo || "").length > 0

    width: parent ? parent.width : 0
    height: 178

    Theme { id: theme }

    Rectangle { anchors.fill: parent; radius: 14; color: "#14161d" }

    // the world's art, along the whole banner — masked so the corners are truly round
    // (CarouselSlide's pattern: a clipped Rectangle would leave the image's corners square)
    Image {
        id: artRaw
        objectName: "universeBannerArt"
        anchors.fill: parent
        property string readySource: ""
        onStatusChanged: if (status === Image.Ready) readySource = String(source)
        source: banner.artRequested && (banner.visible || readySource === String(banner.entry.banner || "")) ? (banner.entry.banner || "") : ""
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
        Behavior on opacity { NumberAnimation { duration: 260 } }
    }
    // left-weighted wash: mark, name and copy read over any art; the art stays open to the right
    Rectangle {
        anchors.fill: parent; radius: 14
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0;  color: Qt.rgba(0, 0, 0, 0.90) }
            GradientStop { position: 0.30; color: Qt.rgba(0, 0, 0, 0.76) }
            GradientStop { position: 0.55; color: Qt.rgba(0, 0, 0, 0.43) }
            GradientStop { position: 1.0;  color: Qt.rgba(0, 0, 0, 0.14) }
        }
    }
    // the edge, drawn last so nothing paints over it
    Rectangle {
        anchors.fill: parent; radius: 14
        color: "transparent"
        border.width: 1
        border.color: banner.lit ? Qt.rgba(0.94, 0.77, 0.29, 0.65) : Qt.rgba(0.97, 0.97, 0.96, 0.11)
        Behavior on border.color { ColorAnimation { duration: 180 } }
    }

    // ---- the identity rail: the universe's own mark, then a hairline ----
    Item {
        id: rail
        objectName: "universeBannerRail"
        x: 30
        width: banner.hasMark ? 98 : 0
        height: parent.height
        visible: banner.hasMark
        Image {
            objectName: "universeBannerMark"
            anchors.centerIn: parent
            anchors.horizontalCenterOffset: -9          // centred in the 80px slot left of the hairline
            width: banner.markW; height: banner.markH
            property string readySource: ""
            onStatusChanged: if (status === Image.Ready) readySource = String(source)
            source: banner.artRequested && (banner.visible || readySource === String(banner.entry.logo || "")) ? (banner.entry.logo || "") : ""
            asynchronous: true; cache: true
            sourceSize.width: 164; sourceSize.height: 148
            fillMode: Image.PreserveAspectFit
            opacity: banner.lit ? 1 : 0.92
        }
        Rectangle {
            anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
            width: 1; height: 92
            color: Qt.rgba(1, 1, 1, 0.12)
        }
    }

    // ---- name, two lines of About copy, what it holds ----
    Column {
        id: copy
        objectName: "universeBannerCopy"
        x: 30 + (banner.hasMark ? 98 + 26 : 0)
        width: Math.max(120, Math.min(670, banner.width - x - enter.width - 28 - 26))
        anchors.verticalCenter: parent.verticalCenter
        spacing: 7
        Text {
            width: parent.width
            text: banner.entry.name || ""
            color: theme.ink
            font.family: theme.display; font.pixelSize: theme.fontSize(30)
            elide: Text.ElideRight
        }
        Text {
            objectName: "universeBannerBlurb"
            width: parent.width
            visible: text.length > 0
            text: banner.entry.blurb || ""
            color: theme.inkDim
            font.family: theme.ui; font.pixelSize: theme.fontSize(13)
            lineHeight: 1.35
            wrapMode: Text.WordWrap
            maximumLineCount: 2
            elide: Text.ElideRight
        }
        Text {
            width: parent.width
            visible: text.length > 0
            text: banner.holds
            color: theme.inkDim
            font.family: theme.ui; font.pixelSize: theme.fontSize(13)
            elide: Text.ElideRight
        }
    }

    // ---- the Explore affordance: appears with the gold edge, eases in from the right ----
    Rectangle {
        id: enter
        objectName: "universeBannerExplore"
        anchors.verticalCenter: parent.verticalCenter
        x: banner.width - 28 - width + (banner.lit ? 0 : 8)
        width: enterRow.implicitWidth + 28; height: 38; radius: 11
        color: Qt.rgba(0, 0, 0, 0.34)
        border.width: 1; border.color: theme.edge
        opacity: banner.lit ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 180 } }
        Behavior on x { NumberAnimation { duration: 180 } }
        Row {
            id: enterRow
            anchors.centerIn: parent
            spacing: 7
            Text { text: "Explore"; color: theme.ink
                   font.family: theme.ui; font.pixelSize: theme.fontSize(13); font.weight: Font.DemiBold }
            Image {
                width: 12; height: 12
                anchors.verticalCenter: parent.verticalCenter
                source: "../assets/icons/lucide/chevron-right.svg"
                sourceSize.width: 12; sourceSize.height: 12
                fillMode: Image.PreserveAspectFit
                opacity: 0.92
            }
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
