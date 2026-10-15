pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Shapes
import "OnePieceEastBlueData.js" as EastBlue

Item {
    id: root

    property bool reducedMotion: false
    property string selectedArcId: "arlong"
    signal paradiseRequested()

    Theme { id: theme }

    readonly property var selectedArc: EastBlue.arc(selectedArcId)
    property real focusOriginX: width * selectedArc.focusX
    property real focusOriginY: height * selectedArc.focusY
    property real focusScale: reducedMotion ? 1.0 : 1.035

    clip: true

    Behavior on focusOriginX {
        enabled: !root.reducedMotion
        NumberAnimation { duration: 720; easing.type: Easing.OutCubic }
    }
    Behavior on focusOriginY {
        enabled: !root.reducedMotion
        NumberAnimation { duration: 720; easing.type: Easing.OutCubic }
    }
    Behavior on focusScale {
        enabled: !root.reducedMotion
        NumberAnimation { duration: 720; easing.type: Easing.OutCubic }
    }
    Rectangle {
        anchors.fill: parent
        color: "#11171c"
    }

    Item {
        id: mapBackdrop
        anchors.fill: parent
        transform: Scale {
            origin.x: root.focusOriginX
            origin.y: root.focusOriginY
            xScale: root.focusScale
            yScale: root.focusScale
        }

        Image {
            anchors.fill: parent
            source: "../assets/universes/one-piece/east-blue-relief.png"
            fillMode: Image.Stretch
            smooth: true
            mipmap: true
            asynchronous: true
            cache: true
        }

        Rectangle {
            anchors.fill: parent
            color: Qt.rgba(0.02, 0.03, 0.04, 0.07)
        }
    }

    Rectangle {
        anchors.fill: parent
        color: "transparent"
        border.width: 1
        border.color: Qt.rgba(1, 1, 1, 0.13)
        radius: 22
    }
    Shape {
        id: voyageShape
        anchors.fill: parent
        z: 4
        layer.enabled: false

        ShapePath {
            id: voyagePath
            fillColor: "transparent"
            strokeColor: Qt.rgba(0.94, 0.88, 0.63, 0.88)
            strokeWidth: 2.4
            strokeStyle: ShapePath.DashLine
            dashPattern: [3.0, 4.5]
            dashOffset: 0
            capStyle: ShapePath.RoundCap
            startX: root.width * 0.104
            startY: root.height * 0.360
            PathCubic { control1X: root.width * 0.160; control1Y: root.height * 0.360; control2X: root.width * 0.235; control2Y: root.height * 0.350; x: root.width * 0.322; y: root.height * 0.349 }
            PathCubic { control1X: root.width * 0.394; control1Y: root.height * 0.332; control2X: root.width * 0.441; control2Y: root.height * 0.290; x: root.width * 0.505; y: root.height * 0.300 }
            PathCubic { control1X: root.width * 0.569; control1Y: root.height * 0.310; control2X: root.width * 0.611; control2Y: root.height * 0.423; x: root.width * 0.665; y: root.height * 0.464 }
            PathCubic { control1X: root.width * 0.690; control1Y: root.height * 0.440; control2X: root.width * 0.720; control2Y: root.height * 0.350; x: root.width * 0.740; y: root.height * 0.310 }
            PathCubic { control1X: root.width * 0.770; control1Y: root.height * 0.270; control2X: root.width * 0.810; control2Y: root.height * 0.215; x: root.width * 0.840; y: root.height * 0.200 }
            PathCubic { control1X: root.width * 0.890; control1Y: root.height * 0.190; control2X: root.width * 0.940; control2Y: root.height * 0.310; x: root.width * 0.936; y: root.height * 0.445 }

            NumberAnimation on dashOffset {
                from: 0
                to: -18
                duration: 4800
                loops: Animation.Infinite
                running: !root.reducedMotion
            }
        }
        ShapePath {
            id: sparklePath
            fillColor: "transparent"
            strokeColor: Qt.rgba(0.96, 0.86, 0.55, 0.72)
            strokeWidth: 3.2
            strokeStyle: ShapePath.DashLine
            dashPattern: [0.8, 7.5]
            dashOffset: 0
            capStyle: ShapePath.RoundCap
            startX: root.width * 0.104
            startY: root.height * 0.360
            PathCubic { control1X: root.width * 0.160; control1Y: root.height * 0.360; control2X: root.width * 0.235; control2Y: root.height * 0.350; x: root.width * 0.322; y: root.height * 0.349 }
            PathCubic { control1X: root.width * 0.394; control1Y: root.height * 0.332; control2X: root.width * 0.441; control2Y: root.height * 0.290; x: root.width * 0.505; y: root.height * 0.300 }
            PathCubic { control1X: root.width * 0.569; control1Y: root.height * 0.310; control2X: root.width * 0.611; control2Y: root.height * 0.423; x: root.width * 0.665; y: root.height * 0.464 }
            PathCubic { control1X: root.width * 0.690; control1Y: root.height * 0.440; control2X: root.width * 0.720; control2Y: root.height * 0.350; x: root.width * 0.740; y: root.height * 0.310 }
            PathCubic { control1X: root.width * 0.770; control1Y: root.height * 0.270; control2X: root.width * 0.810; control2Y: root.height * 0.215; x: root.width * 0.840; y: root.height * 0.200 }
            PathCubic { control1X: root.width * 0.890; control1Y: root.height * 0.190; control2X: root.width * 0.940; control2Y: root.height * 0.310; x: root.width * 0.936; y: root.height * 0.445 }

            NumberAnimation on dashOffset {
                from: 0
                to: -24
                duration: 2800
                loops: Animation.Infinite
                running: !root.reducedMotion
            }
        }
    }
    Text {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: 28
        z: 5
        text: "EAST BLUE"
        color: Qt.rgba(0.97, 0.97, 0.96, 0.50)
        font.family: theme.display
        font.pixelSize: Math.max(24, Math.min(35, root.width * 0.026))
        font.letterSpacing: 8
    }


    Repeater {
        model: EastBlue.arcs
        delegate: Rectangle {
            required property var modelData
            z: 5
            x: root.width * modelData.geoX - width / 2
            y: root.height * modelData.geoY - height / 2
            width: geoText.implicitWidth + 14
            height: 22
            radius: 11
            color: Qt.rgba(0.03, 0.045, 0.06,
                           root.selectedArcId === modelData.id ? 0.50 : 0.32)
            border.width: 1
            border.color: root.selectedArcId === modelData.id
                          ? Qt.rgba(0.94, 0.77, 0.29, 0.25)
                          : Qt.rgba(1, 1, 1, 0.07)
            opacity: root.selectedArcId === modelData.id ? 0.86 : 0.50

            Behavior on opacity {
                NumberAnimation { duration: 220 }
            }

            Text {
                id: geoText
                anchors.centerIn: parent
                text: parent.modelData.geoLabel.toUpperCase()
                color: Qt.rgba(0.97, 0.97, 0.96,
                               root.selectedArcId === parent.modelData.id ? 0.84 : 0.58)
                font.family: theme.ui
                font.pixelSize: 9
                font.letterSpacing: 1.65
            }
        }
    }

    Repeater {
        model: EastBlue.arcs
        delegate: OnePieceArcMarker {
            required property var modelData
            z: selected ? 9 : 7
            arc: modelData
            imageSource: Qt.resolvedUrl("../assets/universes/one-piece/east-blue-markers/" + modelData.id + ".png")
            selected: root.selectedArcId === modelData.id
            reducedMotion: root.reducedMotion
            x: root.width * modelData.markerX - width / 2
            y: root.height * modelData.markerY - height / 2
            onActivated: function(arcId) { root.selectedArcId = arcId }
        }
    }

    OnePieceSeaGate {
        id: reverseMountainGate
        z: 10
        anchors.right: parent.right
        anchors.rightMargin: 14
        // Reverse Mountain owns a transition gutter after Loguetown rather than sharing
        // the final arc's coordinate band. The lower placement also reads as a page turn.
        y: root.height * 0.445 - height / 2
        onActivated: root.paradiseRequested()
    }


}
