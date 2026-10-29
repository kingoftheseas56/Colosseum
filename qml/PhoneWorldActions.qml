pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: root
    property string medium: ""
    property string automationPrefix: "phoneWorldActions"
    property bool trackersActive: false
    signal searchClicked()
    signal stremioClicked()
    signal trackersClicked()
    signal accountClicked(real anchorRight, real anchorBottom)
    signal wallpaperClicked()
    readonly property var actions: {
        const rows = [{key: "search", label: "Search", icon: "search.svg"}]
        if (medium === "Theatre") rows.push({key: "stremio", label: "Stremio", icon: "stremio-official.svg"})
        rows.push({key: "trackers", label: "Trackers", icon: "trackers.svg"})
        rows.push({key: "account", label: "Device and account", icon: "your-colosseum-laurel.svg"})
        rows.push({key: "wallpaper", label: "Wallpaper", icon: "settings.svg"})
        return rows
    }
    implicitWidth: actions.length * 48
    implicitHeight: 48
    Theme { id: theme }

    function activate(key, button) {
        if (key === "search") searchClicked()
        else if (key === "stremio") stremioClicked()
        else if (key === "trackers") trackersClicked()
        else if (key === "wallpaper") wallpaperClicked()
        else {
            const point = button.mapToItem(null, button.width, button.height)
            accountClicked(point.x, point.y)
        }
    }
    function focusAction(key) {
        for (let i = 0; i < buttons.count; ++i) {
            const button = buttons.itemAt(i)
            const data = button ? button["modelData"] : null
            const control = button ? button["control"] : null
            if (data && control && data.key === key) {
                control.forceActiveFocus(Qt.OtherFocusReason)
                return true
            }
        }
        return false
    }
    Row {
        Repeater {
            id: buttons
            model: root.actions
            delegate: Rectangle {
                id: button
                required property var modelData
                property alias control: action
                objectName: root.automationPrefix + "_" + modelData.key
                width: 48; height: 48; radius: 12
                color: action.interactionActive ? Qt.rgba(240 / 255, 196 / 255, 74 / 255, 0.16) : "transparent"
                Image {
                    anchors.centerIn: parent
                    width: 22; height: 22
                    source: "../assets/icons/" + button.modelData.icon
                    sourceSize.width: 44; sourceSize.height: 44
                    fillMode: Image.PreserveAspectFit
                    Accessible.ignored: true
                }
                KeyboardAction {
                    id: action
                    anchors.fill: parent
                    accessibleName: button.modelData.label
                    focusRadius: 12
                    onTriggered: root.activate(button.modelData.key, button)
                }
            }
        }
    }
}
