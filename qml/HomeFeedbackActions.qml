import QtQuick

Item {
    id: root
    required property Item backdrop
    property real track: 0
    property bool compact: false
    signal requested(Item invoker, string kind)
    implicitHeight: actions.height

    Row {
        id: actions
        anchors.right: parent.right
        spacing: root.compact ? 8 : 10

        Repeater {
            model: [
                { kind: "bug", label: qsTr("Report a Bug"), icon: "feedback-bug.svg" },
                { kind: "feature", label: qsTr("Feature Request"), icon: "feedback-feature.svg" }
            ]
            delegate: Glass {
                id: button
                required property var modelData
                width: root.compact ? (root.width - actions.spacing) / 2 : caption.implicitWidth + 30
                height: 39
                radius: 10
                backdrop: root.backdrop
                track: root.track
                blurMax: 14
                tint: 0
                scrim: 0
                edge: "transparent"

                Rectangle {
                    anchors.fill: parent
                    radius: button.radius
                    color: input.interactionActive ? "#9914191e" : "#5112171c"
                    border.color: input.interactionActive ? "#66f0c44a" : "#3bffffff"
                    Behavior on color { ColorAnimation { duration: 150 } }
                }
                Row {
                    id: caption
                    anchors.centerIn: parent
                    spacing: 9
                    Image {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 16
                        height: 16
                        source: "../assets/icons/" + button.modelData.icon
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: button.modelData.label
                        color: "#e5e3df"
                        font.family: "Segoe UI"
                        font.pixelSize: root.compact ? 11 : 12
                        font.weight: Font.Medium
                    }
                }
                KeyboardAction {
                    id: input
                    objectName: button.modelData.kind === "bug" ? "homeReportBug" : "homeRequestFeature"
                    anchors.fill: parent
                    accessibleName: button.modelData.label
                    focusRadius: 10
                    onTriggered: root.requested(input, button.modelData.kind)
                }
            }
        }
    }
}
