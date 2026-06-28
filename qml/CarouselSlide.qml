// CarouselSlide — renders ONE featured slide as a rounded card: real art (manga AniList banner =
// full-bleed wide; comic iTunes poster = native rounded gradient backdrop + crisp poster on right),
// a left scrim, copy, and the ghost medium marker. Used as a SwipeView page by FeaturedCarousel.
import QtQuick
import QtQuick.Effects

Item {
    id: slideRoot
    property var slide: ({})                 // { title, blurb, ghost, c1, c2, art?, artKind? }
    property string kicker: "Featured"
    property string primaryLabel: "Read"
    property string secondaryLabel: "Details"
    signal primaryClicked()
    signal secondaryClicked()

    readonly property bool isPoster: slide.artKind !== undefined && slide.artKind === "poster"
    readonly property bool compactCopy: slideRoot.height < 360
    Theme { id: theme }

    Rectangle {
        anchors.fill: parent
        radius: 20; clip: true
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0; color: slideRoot.slide.c1 !== undefined ? slideRoot.slide.c1 : "#241433" }
            GradientStop { position: 1; color: slideRoot.slide.c2 !== undefined ? slideRoot.slide.c2 : "#120b1a" }
        }
        border.width: 1; border.color: theme.edge

        Image {
            id: artRaw
            anchors.fill: parent
            source: slideRoot.slide.art !== undefined ? slideRoot.slide.art : ""
            asynchronous: true; cache: true
            fillMode: Image.PreserveAspectCrop
            verticalAlignment: Image.AlignTop
            visible: false
        }
        Item {
            id: artMask
            anchors.fill: parent; visible: false; layer.enabled: true
            Rectangle { anchors.fill: parent; radius: 20; color: "white" }
        }
        // manga wide banner: full-bleed, rounded by the mask
        MultiEffect {
            anchors.fill: parent
            source: artRaw
            maskEnabled: true; maskSource: artMask
            visible: !slideRoot.isPoster
            opacity: (!slideRoot.isPoster && artRaw.status === Image.Ready) ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: 300 } }
        }
        // comic poster: native rounded gradient vignette (NO blur → clean corners)…
        Rectangle {
            anchors.fill: parent; radius: 20
            visible: slideRoot.isPoster
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0;   color: Qt.rgba(0, 0, 0, 0.45) }
                GradientStop { position: 0.6; color: Qt.rgba(0, 0, 0, 0.12) }
                GradientStop { position: 1;   color: Qt.rgba(0, 0, 0, 0.45) }
            }
        }
        // …with the crisp poster stood on the right
        Image {
            visible: slideRoot.isPoster
            source: slideRoot.isPoster && slideRoot.slide.art !== undefined ? slideRoot.slide.art : ""
            asynchronous: true; cache: true
            anchors.right: parent.right; anchors.rightMargin: 56
            anchors.verticalCenter: parent.verticalCenter
            height: parent.height - 44
            width: height * 0.66
            fillMode: Image.PreserveAspectFit
            sourceSize.width: 320; sourceSize.height: 480
            opacity: status === Image.Ready ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: 300 } }
        }

        // left darkening wash so copy reads over ANY art
        Rectangle {
            anchors.fill: parent; radius: parent.radius
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0;    color: Qt.rgba(0, 0, 0, 0.78) }
                GradientStop { position: 0.55; color: Qt.rgba(0, 0, 0, 0.30) }
                GradientStop { position: 1;    color: Qt.rgba(0, 0, 0, 0.0) }
            }
        }
        // ghost medium marker
        Text {
            text: slideRoot.slide.ghost !== undefined ? slideRoot.slide.ghost : ""
            color: Qt.rgba(1, 1, 1, 0.06)
            font.family: theme.display; font.bold: true; font.pixelSize: 150
            anchors.right: parent.right; anchors.rightMargin: 40
            anchors.top: parent.top; anchors.topMargin: -30
        }
        // copy
        Column {
            anchors.left: parent.left
            anchors.bottom: parent.bottom
            anchors.leftMargin: 42
            anchors.bottomMargin: 42
            width: Math.min(560, parent.width - 84)
            spacing: slideRoot.compactCopy ? 8 : 10
            Text { text: slideRoot.kicker.toUpperCase(); color: theme.gold
                font.family: theme.ui; font.pixelSize: 11; font.letterSpacing: 3 }
            Text { text: slideRoot.slide.title !== undefined ? slideRoot.slide.title : ""
                color: theme.ink; font.family: theme.display; font.pixelSize: slideRoot.compactCopy ? 42 : 50
                lineHeight: 0.96; maximumLineCount: 2; elide: Text.ElideRight
                width: parent.width; wrapMode: Text.WordWrap }
            Text { text: slideRoot.slide.blurb !== undefined ? slideRoot.slide.blurb : ""
                color: theme.inkDim; font.family: theme.ui; font.pixelSize: slideRoot.compactCopy ? 13 : 14
                maximumLineCount: slideRoot.compactCopy ? 2 : 3; elide: Text.ElideRight
                width: parent.width; wrapMode: Text.WordWrap; lineHeight: 1.25 }
            Row {
                spacing: 10; topPadding: 6
                Rectangle {
                    radius: 11; height: 42; width: pl.implicitWidth + 36; color: theme.gold
                    Text { id: pl; anchors.centerIn: parent; text: slideRoot.primaryLabel; color: "#1a1408"
                        font.family: theme.ui; font.pixelSize: 14; font.weight: Font.DemiBold }
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                        onClicked: slideRoot.primaryClicked() }
                }
                Rectangle {
                    radius: 11; height: 42; width: sl.implicitWidth + 36
                    color: Qt.rgba(1, 1, 1, 0.10); border.width: 1; border.color: Qt.rgba(1, 1, 1, 0.18)
                    Text { id: sl; anchors.centerIn: parent; text: slideRoot.secondaryLabel; color: theme.ink
                        font.family: theme.ui; font.pixelSize: 14; font.weight: Font.Medium }
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                        onClicked: slideRoot.secondaryClicked() }
                }
            }
        }
    }
}
