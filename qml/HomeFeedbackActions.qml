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
            delegate: FeedbackActionButton {
                id: button
                required property var modelData
                width: root.compact ? (root.width - actions.spacing) / 2 : implicitWidth
                backdrop: root.backdrop
                track: root.track
                compact: root.compact
                label: modelData.label
                iconSource: "../assets/icons/" + modelData.icon
                highlighted: input.interactionActive
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
