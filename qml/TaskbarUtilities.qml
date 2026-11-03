import QtQuick
import QtQuick.Controls.Basic

Row {
    id: root
    objectName: "taskbarUtilities"
    property bool trackersActive: false
    property bool updateAvailable: false
    property bool updateUnseen: false
    property bool reducedMotion: false
    readonly property var dlnaState: typeof dlnaSharing !== "undefined" ? dlnaSharing : null
    readonly property bool sharing: !!dlnaState && dlnaState.running === true
    readonly property string trackersStatus: {
        if (typeof TrackerSyncCenter === "undefined" || !TrackerSyncCenter) return ""
        const state = TrackerSyncCenter.aggregateState || ({})
        if (state.status === "Attention" || state.status === "Owner unavailable") return "error"
        if (state.status === "Empty" && !Number(state.connectedCount || 0)) return ""
        return state.status === "Healthy" ? "ok" : "pending"
    }
    readonly property var actions: [
        { key: "trackers", label: qsTr("Trackers"), icon: "trackers.svg", show: true, active: trackersActive, dot: trackersStatus },
        { key: "updates", label: qsTr("Updates"), icon: "update.svg", show: true, dot: updateAvailable ? "update" : "" },
        { key: "wallpaper", label: qsTr("Wallpaper"), icon: "settings.svg", show: true },
        { key: "sharing", label: qsTr("Wi-Fi sharing"), icon: sharing ? "access-point-gold.svg" : "access-point.svg", show: !!dlnaState, active: sharing }
    ].filter(action => action.show)
    signal triggered(string action, real anchorRight, real anchorBottom)
    spacing: 14
    height: 46
    Theme { id: theme }
    function focusAction(action) {
        for (let i = 0; i < buttons.count; ++i) {
            const button = buttons.itemAt(i)
            if (button && button.modelData.key === action) { button.focusControl(); return }
        }
    }
    Repeater {
        id: buttons
        model: root.actions
        delegate: Item {
            id: cell
            required property var modelData
            width: 46; height: 46
            function focusControl() { input.forceActiveFocus(Qt.TabFocusReason) }
            Rectangle {
                anchors.fill: parent
                radius: 13
                color: input.interactionActive || cell.modelData.active
                       ? Qt.rgba(1, 1, 1, 0.15) : Qt.rgba(1, 1, 1, 0.055)
                border.width: input.activeFocus ? 1 : 0
                border.color: theme.gold
            }
            Image {
                anchors.centerIn: parent
                width: 21; height: 21
                source: "../assets/icons/" + cell.modelData.icon
                sourceSize.width: 42; sourceSize.height: 42
                fillMode: Image.PreserveAspectFit
                opacity: cell.modelData.active ? 1 : 0.75
            }
            Rectangle {
                visible: !!cell.modelData.active
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom; anchors.bottomMargin: 4
                width: 20; height: 3; radius: 2
                color: Qt.rgba(0.94, 0.77, 0.29, 0.95)
            }
            Rectangle {
                visible: !!cell.modelData.dot
                anchors.top: parent.top; anchors.right: parent.right
                anchors.topMargin: 3; anchors.rightMargin: 3
                width: 7; height: 7; radius: 3.5
                color: cell.modelData.dot === "ok" ? "#5fd18b" : cell.modelData.dot === "error" ? "#ef6a5a"
                       : cell.modelData.dot === "update" ? theme.ink : theme.gold
                SequentialAnimation on opacity {
                    running: root.visible && cell.modelData.key === "updates" && root.updateUnseen && root.updateAvailable && !root.reducedMotion
                    loops: Animation.Infinite
                    NumberAnimation { to: 0.4; duration: 700 }
                    NumberAnimation { to: 1; duration: 700 }
                }
            }
            KeyboardAction {
                id: input
                objectName: "taskbarUtility_" + cell.modelData.key
                anchors.fill: parent
                accessibleName: cell.modelData.label + (cell.modelData.dot === "error" ? qsTr(", attention required") : "")
                focusRadius: 13
                onTriggered: {
                    const anchor = cell.mapToItem(null, cell.width, 0)
                    root.triggered(cell.modelData.key, anchor.x, anchor.y)
                }
            }
            ToolTip.visible: input.interactionActive
            ToolTip.delay: 600
            ToolTip.text: cell.modelData.label
        }
    }
}
