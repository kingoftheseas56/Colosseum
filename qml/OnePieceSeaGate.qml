pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Shapes

Control {
    id: root

    signal activated()

    width: 132
    height: 154
    padding: 0
    hoverEnabled: true
    activeFocusOnTab: true
    focusPolicy: Qt.StrongFocus
    background: null

    readonly property bool hot: hover.hovered || activeFocus

    function trigger() {
        root.focus = false
        root.activated()
    }

    Keys.onReturnPressed: root.trigger()
    Keys.onEnterPressed: root.trigger()
    HoverHandler { id: hover }
    TapHandler { onTapped: root.trigger() }

    contentItem: Item {
        id: content
        anchors.fill: parent
        Rectangle {
            id: plate
            width: 104
            height: 104
            radius: 20
            anchors.horizontalCenter: parent.horizontalCenter
            y: 0
            color: Qt.rgba(0.035, 0.047, 0.063, 0.82)
            border.width: 1
            border.color: root.hot ? "#f0c44a" : Qt.rgba(0.97, 0.97, 0.96, 0.42)

            Shape {
                anchors.fill: parent

                ShapePath {
                    fillColor: Qt.rgba(0.78, 0.64, 0.48, 0.92)
                    strokeColor: Qt.rgba(0.95, 0.86, 0.72, 0.72)
                    strokeWidth: 1.2
                    startX: 12
                    startY: 88
                    PathLine { x: 52; y: 18 }
                    PathLine { x: 92; y: 88 }
                    PathLine { x: 12; y: 88 }
                }

                ShapePath {
                    fillColor: "transparent"
                    strokeColor: "#f0c44a"
                    strokeWidth: 2.2
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    startX: 15
                    startY: 88
                    PathLine { x: 52; y: 55 }
                    PathLine { x: 52; y: 17 }
                }

                ShapePath {
                    fillColor: "transparent"
                    strokeColor: "#f0c44a"
                    strokeWidth: 2.2
                    capStyle: ShapePath.RoundCap
                    startX: 89
                    startY: 88
                    PathLine { x: 52; y: 55 }
                }
            }

            Rectangle {
                width: 7
                height: 7
                radius: 3.5
                x: 48.5
                y: 51.5
                color: "#f0c44a"
            }

            Rectangle {
                anchors.top: parent.top
                anchors.right: parent.right
                anchors.topMargin: 8
                anchors.rightMargin: 8
                width: 42
                height: 18
                radius: 9
                color: Qt.rgba(0.02, 0.03, 0.04, 0.78)
                border.width: 1
                border.color: Qt.rgba(0.94, 0.77, 0.29, 0.34)
                Text {
                    anchors.centerIn: parent
                    text: "PAGE 02"
                    color: "#f0c44a"
                    font.family: "Segoe UI"
                    font.pixelSize: 8
                    font.bold: true
                }
            }
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            y: 111
            width: parent.width
            text: "REVERSE MOUNTAIN"
            horizontalAlignment: Text.AlignHCenter
            color: root.hot ? "#f0c44a" : "#f7f7f5"
            font.family: "Fraunces"
            font.pixelSize: 15
            style: Text.Outline
            styleColor: Qt.rgba(0, 0, 0, 0.82)
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            y: 137
            text: "ENTER GRAND LINE  >"
            color: "#f0c44a"
            font.family: "Segoe UI"
            font.pixelSize: 8
            font.bold: true
            font.letterSpacing: 1.2
        }

    }
}
