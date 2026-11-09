import QtQuick

Item {
    id: root
    required property Item backdrop
    property real track: 0
    property bool compact: false
    signal requested(Item invoker, string kind)
    implicitHeight: actions.height

    Grid {
        id: actions
        anchors.right: parent.right
        columns: root.compact && root.width < 350 ? 2 : 3
        spacing: root.compact ? 8 : 10

        Repeater {
            model: [
                { kind: "bug", label: qsTr("Report a Bug"), icon: "feedback-bug.svg", objectName: "homeReportBug" },
                { kind: "feature", label: qsTr("Feature Request"), icon: "feedback-feature.svg", objectName: "homeRequestFeature" },
                { kind: "support", label: qsTr("Support"), icon: "feedback-support.svg", objectName: "homeSupport" }
            ]
            delegate: FeedbackActionButton {
                id: button
                required property var modelData
                width: root.compact
                    ? Math.max(0, (root.width - (actions.columns - 1) * actions.spacing) / actions.columns)
                    : implicitWidth
                backdrop: root.backdrop
                track: root.track
                compact: root.compact
                label: modelData.label
                iconSource: "../assets/icons/" + modelData.icon
                highlighted: input.interactionActive
                KeyboardAction {
                    id: input
                    objectName: button.modelData.objectName
                    anchors.fill: parent
                    accessibleName: button.modelData.label
                    focusRadius: 10
                    onTriggered: root.requested(input, button.modelData.kind)
                }
            }
        }
    }
}
