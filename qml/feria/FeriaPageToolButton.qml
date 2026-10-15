import QtQuick
import QtQuick.Controls
import "../account" as AccountUi

AccountUi.AccountButton {
    id: control
    required property url iconSource
    required property string helpText
    property real iconRotation: 0
    property bool crossBar: false
    variant: "link"
    implicitWidth: 32
    implicitHeight: 32
    Accessible.name: helpText
    ToolTip.text: helpText
    ToolTip.visible: hovered
    ToolTip.delay: 650
    contentItem: Item {
        opacity: control.enabled ? 1 : 0.35
        Image {
            anchors.centerIn: parent; width: 18; height: 18
            source: control.iconSource; sourceSize: Qt.size(18, 18)
            rotation: control.iconRotation
        }
        // Reuse the house minus SVG for the plus's perpendicular stroke.
        Image {
            anchors.centerIn: parent; width: 18; height: 18
            visible: control.crossBar
            source: control.iconSource; sourceSize: Qt.size(18, 18)
            rotation: 90
        }
    }
}
