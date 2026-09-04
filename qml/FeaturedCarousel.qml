// FeaturedCarousel — the world page's draggable spotlight (its signature/hero moment). SwipeView
// gives mouse-drag + flick + snap natively (Electron hand-rolled the pointer math; QML has it built
// in). Each page is a CarouselSlide. Dots keep GOLD as the active accent (doctrine: gold = active)
// and read over ANY cover thanks to a dark backing strip — gold-on-gold art was invisible before.
import QtQuick
import QtQuick.Controls
import QtQuick.Window

Item {
    id: car
    property var slides: []
    readonly property bool televisionMode: {
        const w = car.Window.window
        return !!(w && w["televisionMode"] === true)
    }                        // [{ title, blurb, ghost, c1, c2, art?, artKind? }]
    property string kicker: "Featured"
    property string primaryLabel: "Read"
    property string secondaryLabel: "Details"
    signal primaryClicked(int index)
    signal secondaryClicked(int index)

    property alias index: view.currentIndex
    width: parent ? parent.width : 800
    AdaptiveLayout { id: adaptive; viewportWidth: car.width }
    implicitHeight: car.televisionMode ? 380 : adaptive.heroHeight
    activeFocusOnTab: car.televisionMode && car.slides.length > 0
    Accessible.role: Accessible.List
    Accessible.name: car.kicker
    Theme { id: theme }

    Keys.onPressed: (event) => {
        if (!car.televisionMode || car.slides.length === 0)
            return
        if (event.key === Qt.Key_Left) {
            view.currentIndex = Math.max(0, view.currentIndex - 1)
            event.accepted = true
        } else if (event.key === Qt.Key_Right) {
            view.currentIndex = Math.min(car.slides.length - 1, view.currentIndex + 1)
            event.accepted = true
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter
                   || event.key === Qt.Key_Select || event.key === Qt.Key_Space) {
            car.primaryClicked(view.currentIndex)
            event.accepted = true
        }
    }

    Rectangle {
        anchors.fill: parent
        anchors.margins: -5
        radius: 24
        visible: car.televisionMode && car.activeFocus
        color: "transparent"
        border.width: 4
        border.color: theme.gold
        z: 10000
    }

    SwipeView {
        id: view
        anchors.fill: parent
        clip: true
        focusPolicy: car.slides.length > 0 ? Qt.TabFocus : Qt.NoFocus
        Keys.onPressed: (event) => featuredKeys.handle(event)
        Repeater {
            model: car.slides
            CarouselSlide {
                required property var modelData
                required property int index
                slide: modelData
                kicker: car.kicker
                primaryLabel: modelData.cta !== undefined ? modelData.cta : car.primaryLabel
                secondaryLabel: modelData.secondaryLabel !== undefined ? modelData.secondaryLabel : car.secondaryLabel
                onPrimaryClicked: car.primaryClicked(index)
                onSecondaryClicked: car.secondaryClicked(index)
            }
        }
    }

    KeyboardCollectionController {
        id: featuredKeys
        view: view
        orientation: "horizontal"
        count: car.slides.length
        onActivated: (index) => car.primaryClicked(index)
    }

    // color-agnostic dots: GOLD active on a dark backing strip → reads over any cover
    Rectangle {
        visible: car.slides.length > 1
        anchors.right: parent.right; anchors.bottom: parent.bottom
        anchors.margins: adaptive.phone ? 16 : 26
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
