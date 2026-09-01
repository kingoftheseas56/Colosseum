import QtQuick
import QtQuick.Controls

Control {
    id: root

    required property var arc
    required property url imageSource
    property bool selected: false
    property bool reducedMotion: false
    signal activated(string arcId)

    width: 142
    height: 148
    padding: 0
    hoverEnabled: true
    activeFocusOnTab: true
    focusPolicy: Qt.StrongFocus
    background: null

    readonly property bool hot: hover.hovered || activeFocus

    Keys.onReturnPressed: root.activated(root.arc.id)
    Keys.onEnterPressed: root.activated(root.arc.id)

    HoverHandler { id: hover }
    TapHandler { onTapped: root.activated(root.arc.id) }

    contentItem: Item {
        id: content
        anchors.fill: parent
        Item {
            id: photoHolder
            width: root.selected ? 104 : 96
            height: width
            anchors.horizontalCenter: parent.horizontalCenter
            y: 11 + (104 - height) / 2
            scale: root.hot ? 1.025 : 1.0
            Behavior on width {
                enabled: !root.reducedMotion
                NumberAnimation { duration: 180; easing.type: Easing.OutCubic }
            }
            Behavior on scale {
                enabled: !root.reducedMotion
                NumberAnimation { duration: 180; easing.type: Easing.OutCubic }
            }

            Image {
                anchors.fill: parent
                source: root.imageSource
                fillMode: Image.PreserveAspectCrop
                smooth: true
                mipmap: true
                asynchronous: true
                cache: true
            }

            Rectangle {
                anchors.fill: parent
                radius: width / 2
                color: "transparent"
                border.width: 2
                border.color: root.selected || root.hot
                              ? "#f0c44a" : Qt.rgba(0.97, 0.97, 0.96, 0.78)
            }
        }

        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            y: 0
            width: 30
            height: 18
            radius: 9
            color: Qt.rgba(0.035, 0.047, 0.063, 0.82)
            border.width: 1
            border.color: root.selected ? Qt.rgba(0.94, 0.77, 0.29, 0.35)
                                        : Qt.rgba(1, 1, 1, 0.13)
            Text {
                anchors.centerIn: parent
                text: String(root.arc.order).padStart(2, "0")
                color: root.selected ? "#f0c44a" : Qt.rgba(0.97, 0.97, 0.96, 0.62)
                font.family: "Segoe UI"
                font.pixelSize: 9
                font.bold: true
            }
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            y: 121
            text: root.arc.title
            color: root.selected || root.hot ? "#f0c44a" : "#f7f7f5"
            font.family: "Fraunces"
            font.pixelSize: root.selected ? 18 : 17
            elide: Text.ElideRight
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            style: Text.Outline
            styleColor: Qt.rgba(0, 0, 0, 0.82)
        }

    }
}
