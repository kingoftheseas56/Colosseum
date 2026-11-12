import QtQuick

Item {
    id: root
    required property Item backdrop
    property real track: 0
    property bool compact: false
    // Support is held back until 1.4.0, once Vinyl and Feria have landed (Hemanth 2026-11-12).
    // The page, route and link policy stay in place; turning this on brings the button back.
    property bool supportEnabled: false
    readonly property var actionModel: [
        { kind: "bug", label: qsTr("Report a Bug"), icon: "feedback-bug.svg", objectName: "homeReportBug" },
        { kind: "feature", label: qsTr("Feature Request"), icon: "feedback-feature.svg", objectName: "homeRequestFeature" },
        { kind: "support", label: qsTr("Support"), icon: "feedback-support.svg", objectName: "homeSupport" }
    ].filter(function(a) { return a.kind !== "support" || root.supportEnabled })
    signal requested(Item invoker, string kind)
    implicitHeight: actions.height

    Grid {
        id: actions
        anchors.right: parent.right
        columns: root.compact && root.width < 350 ? 2 : root.actionModel.length
        spacing: root.compact ? 8 : 10

        Repeater {
            model: root.actionModel
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
