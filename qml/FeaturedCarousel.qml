// FeaturedCarousel — the world page's draggable spotlight (its signature/hero moment). SwipeView
// gives mouse-drag + flick + snap natively (Electron hand-rolled the pointer math; QML has it built
// in). Each page is a CarouselSlide. Dots keep GOLD as the active accent (doctrine: gold = active)
// and read over ANY cover thanks to a dark backing strip — gold-on-gold art was invisible before.
import QtQuick
import QtQuick.Controls

Item {
    id: car
    property var slides: []                        // [{ title, blurb, ghost, c1, c2, art?, artKind? }]
    property string kicker: "Featured"
    property string primaryLabel: "Read"
    property string secondaryLabel: "Details"
    signal primaryClicked(int index)
    signal secondaryClicked(int index)

    property alias index: view.currentIndex
    implicitHeight: 330
    width: parent ? parent.width : 800
    Theme { id: theme }

    SwipeView {
        id: view
        anchors.fill: parent
        clip: true
        Repeater {
            model: car.slides
            CarouselSlide {
                required property var modelData
                required property int index
                slide: modelData
                kicker: car.kicker
                primaryLabel: car.primaryLabel
                secondaryLabel: car.secondaryLabel
                onPrimaryClicked: car.primaryClicked(index)
                onSecondaryClicked: car.secondaryClicked(index)
            }
        }
    }

    // color-agnostic dots: GOLD active on a dark backing strip → reads over any cover
    Rectangle {
        visible: car.slides.length > 1
        anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.margins: 26
        radius: 999; height: 22; width: dotsRow.implicitWidth + 22
        color: Qt.rgba(0, 0, 0, 0.42)
        Row {
            id: dotsRow
            anchors.centerIn: parent; spacing: 7
            Repeater {
                model: car.slides.length
                delegate: Rectangle {
                    required property int index
                    width: index === view.currentIndex ? 22 : 7
                    height: 7; radius: 4
                    color: index === view.currentIndex ? theme.gold : Qt.rgba(1, 1, 1, 0.5)
                    Behavior on width { NumberAnimation { duration: 150 } }
                    MouseArea {
                        anchors.fill: parent; anchors.margins: -4
                        cursorShape: Qt.PointingHandCursor
                        onClicked: view.currentIndex = index
                    }
                }
            }
        }
    }
}
