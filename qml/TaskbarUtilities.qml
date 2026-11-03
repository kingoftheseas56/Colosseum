import QtQuick
import QtQuick.Layouts

FocusScope {
    id: root
    property Item backdrop: null
    property string worldMode: ""
    property bool androidHost: Qt.platform.os === "android"
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
    implicitHeight: grid.implicitHeight + 24
    height: implicitHeight
    Theme { id: theme }
    Glass { anchors.fill: parent; backdrop: root.backdrop; scrim: 0.72; tint: 0.08; radius: 18 }
    MouseArea { anchors.fill: parent }
    function focusAction(action) {
        for (let i = 0; i < buttons.count; ++i) {
            const button = buttons.itemAt(i)
            if (button && button.modelData.key === action) { button.focusControl(); return }
        }
    }
    GridLayout {
        id: grid
        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 12 }
        columns: Math.max(1, Math.min(root.actions.length, Math.floor(width / 88)))
        columnSpacing: 6
        rowSpacing: 6
        Repeater {
            id: buttons
            model: root.actions
            delegate: Item {
                id: cell
                required property var modelData
                Layout.fillWidth: true
                Layout.preferredHeight: 68
                function focusControl() { input.forceActiveFocus(Qt.TabFocusReason) }
                Rectangle {
                    anchors.fill: parent
                    radius: 12
                    color: input.interactionActive || cell.modelData.active ? theme.glassHi : "transparent"
                    border.width: input.activeFocus ? 1 : 0
                    border.color: theme.gold
                }
                Image {
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: 10; width: 22; height: 22
                    source: "../assets/icons/" + cell.modelData.icon
                    sourceSize.width: 44; sourceSize.height: 44
                    opacity: input.interactionActive || cell.modelData.active ? 1 : 0.8
                }
                Text {
                    anchors { left: parent.left; right: parent.right; bottom: parent.bottom; bottomMargin: 10 }
                    horizontalAlignment: Text.AlignHCenter
                    text: cell.modelData.label
                    color: theme.inkDim
                    font.family: theme.ui
                    font.pixelSize: 11
                    elide: Text.ElideRight
                }
                Rectangle {
                    visible: !!cell.modelData.dot
                    x: cell.width / 2 + 8; y: 8
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
                    focusRadius: 12
                    onTriggered: {
                        const anchor = cell.mapToItem(null, cell.width, 0)
                        root.triggered(cell.modelData.key, anchor.x, anchor.y)
                    }
                }
            }
        }
    }
}
