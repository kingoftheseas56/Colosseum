import QtQuick

import QtTest
import "../../qml" as App

Item {
    id: window
    width: 1280; height: 900; visible: true
    App.Taskbar { id: bar; open: true; groups: [] }
    TestCase {
        name: "FeriaTaskbarContext"
        when: windowShown
        SignalSpy { id: activityClick; target: bar; signalName: "historyStatsClicked" }
        function find(item, name) {
            if (item.objectName === name) return item
            const children = item.children || []
            for (let i = 0; i < children.length; ++i) {
                const result = find(children[i], name)
                if (result) return result
            }
            return null
        }
        function test_same_taskbar_action_identifies_current_activity_domain() {
            verify(find(bar, "taskbarFeria") === null)
            compare(bar.closedWidth, 130)
            const action = find(bar, "taskbarHistoryStatsInput")
            verify(action !== null)
            compare(action.accessibleName, "Your Colosseum")
            const position = action.mapToItem(window, 0, 0)
            bar.feriaContext = true
            compare(action.accessibleName, "Your Feria")
            compare(action.mapToItem(window, 0, 0), position)
            action.forceActiveFocus()
            tryCompare(action, "activeFocus", true)
            keyClick(Qt.Key_Return)
            compare(activityClick.count, 1)
            bar.feriaContext = false
            compare(action.accessibleName, "Your Colosseum")
        }
    }
}
