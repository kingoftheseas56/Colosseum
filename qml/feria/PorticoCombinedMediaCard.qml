import QtQuick
import ".." as Colosseum
import "../CatalogueVisualMetrics.js" as Metrics

Item {
    id: root
    property real unit: 16
    property string title: ""
    property string sub: ""
    property string artSource: ""
    property string shape: "poster"
    property string rankText: ""
    property bool selected: false
    property color ink: "#f7f7f5"
    property color mist: "#c9c8d0"
    property color slate: "#9a99a5"
    property color gold: "#f0c44a"
    property color toneA: "#3f5a78"
    property color toneB: "#16222e"
    property string displayFont: "Fraunces"
    signal triggered()
    signal entered()

    Colosseum.Theme { id: theme }

    readonly property real artHeight: shape === "poster" ? width * 1.5 : width
    height: Metrics.gallery.hoverLift + artHeight + 10 + Metrics.gallery.titleMinHeight + 24

    Loader {
        anchors.fill: parent
        anchors.topMargin: Metrics.gallery.hoverLift
        active: root.shape === "poster"
        sourceComponent: Colosseum.CataloguePosterCard {
            item: ({title: root.title, cover: root.artSource, author: root.sub})
            visualProfile: "gallery"
            showAuthorAtRest: true
            keyboardFocused: root.selected
            onActivated: root.triggered()
        }
    }
    Rectangle {
        id: art
        y: Metrics.gallery.hoverLift
        visible: root.shape !== "poster"
        width: root.width
        height: root.artHeight
        radius: root.shape === "circle" ? width / 2 : (root.shape === "square" ? 0.55 * root.unit : 0.75 * root.unit)
        clip: true
        color: Qt.rgba(1,1,1,0.05)
        border.width: 1
        border.color: Qt.rgba(1,1,1,0.08)
        transform: Translate {
            y: root.selected ? -Metrics.gallery.hoverLift : 0
            Behavior on y { NumberAnimation { duration: Metrics.gallery.hoverDuration; easing.type: Easing.OutCubic } }
        }

        Rectangle {
            anchors.fill: parent
            radius: parent.radius
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0; color: root.toneA }
                GradientStop { position: 1; color: root.toneB }
            }
            Text {
                anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                anchors.margins: 0.8 * root.unit
                text: root.shape === "circle" ? root.title.slice(0, 2).toUpperCase() : root.title
                color: root.ink
                font.family: root.displayFont
                font.pixelSize: root.shape === "circle" ? 2.6 * root.unit : 1.05 * root.unit
                font.weight: Font.Medium
                wrapMode: Text.WordWrap
                maximumLineCount: root.shape === "circle" ? 1 : 4
                elide: Text.ElideRight
                horizontalAlignment: root.shape === "circle" ? Text.AlignHCenter : Text.AlignLeft
            }
        }
        Image {
            anchors.fill: parent
            source: root.artSource
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            cache: true
            smooth: true
            visible: status === Image.Ready
        }
        Text {
            visible: root.rankText.length > 0
            x: root.shape === "circle" ? (parent.width - implicitWidth) / 2 : 0.6 * root.unit
            y: root.shape === "circle" ? parent.height - 1.9 * root.unit : 0.45 * root.unit
            text: root.rankText
            color: root.ink
            font.family: root.displayFont
            font.pixelSize: 1.3 * root.unit
            font.weight: Font.Medium
            style: Text.Raised
            styleColor: Qt.rgba(0,0,0,0.8)
        }
        Rectangle {
            anchors.fill: parent
            anchors.margins: -0.1875 * root.unit
            radius: parent.radius + 0.1875 * root.unit
            color: "transparent"
            border.width: 0.1875 * root.unit
            border.color: root.selected ? root.gold : "transparent"
            visible: root.selected
        }
    }
    Text {
        id: titleText
        visible: root.shape !== "poster"
        anchors { left: parent.left; right: parent.right; top: art.bottom; topMargin: 0.7 * root.unit }
        text: root.title
        color: root.ink
        font.family: theme.ui
        font.pixelSize: Metrics.gallery.titlePixels
        font.weight: Font.DemiBold
        wrapMode: Text.WordWrap
        maximumLineCount: 2
        elide: Text.ElideRight
        horizontalAlignment: root.shape === "circle" ? Text.AlignHCenter : Text.AlignLeft
    }
    Text {
        visible: root.shape !== "poster"
        anchors { left: parent.left; right: parent.right; top: titleText.bottom; topMargin: 0.15 * root.unit }
        text: root.sub
        color: root.shape === "poster" ? root.mist : root.slate
        font.family: theme.ui
        font.pixelSize: 11
        elide: Text.ElideRight
        horizontalAlignment: root.shape === "circle" ? Text.AlignHCenter : Text.AlignLeft
    }
    Text {
        visible: root.shape === "poster" && root.rankText.length > 0
        x: 10; y: 8 + (root.selected ? 0 : Metrics.gallery.hoverLift)
        text: root.rankText
        color: root.ink
        font.family: root.displayFont
        font.pixelSize: 22
        style: Text.Raised
        styleColor: "#99000000"
    }
    Colosseum.KeyboardAction {
        id: input
        anchors.fill: parent
        focusEnabled: false
        showFocusFrame: false
        accessibleName: root.title
        onTriggered: root.triggered()
    }
    Connections {
        target: input
        function onHoveredChanged() { if (input.hovered) root.entered() }
    }
}
