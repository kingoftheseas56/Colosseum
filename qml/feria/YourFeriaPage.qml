import QtQuick
import ".." as Colosseum

PorticoCombinedParityAccountView {
    id: root
    objectName: "yourFeriaPage"
    embedded: true
    activityOnly: true
    signal backRequested()
    signal settingsRequested()

    function takeKeyboardFocus() { focusContent() }
    function activate() {
        if (controller.accountTab === "apps") controller.accountTab = "history"
        controller.sharedAccountView = root
        controller.sharedAccountActive = true
    }
    Component.onCompleted: activate()
    Component.onDestruction: {
        if (controller && controller.sharedAccountView === root) {
            controller.sharedAccountView = null
            controller.sharedAccountActive = false
        }
    }
    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Escape) { backRequested(); event.accepted = true }
        else event.accepted = handleKey(event.key)
    }
    Colosseum.BackAction {
        objectName: "yourFeriaBack"
        z: 5
        x: root.m; y: 20
        label: "Feria"
        idleColor: controller.mist
        hoverColor: controller.ink
        labelSize: 14
        onTriggered: root.backRequested()
    }
    FeriaWindowControls {
        z: 5
        controller: root.controller
        namePrefix: "yourFeria"
        anchors { right: parent.right; rightMargin: root.m; top: parent.top; topMargin: 16 }
    }
    Connections {
        target: root.controller
        function onAccountTabChanged() {
            if (root.controller.sharedAccountView === root && root.controller.accountTab === "apps")
                root.settingsRequested()
        }
    }
    Connections {
        target: typeof FeriaAccount !== "undefined" ? FeriaAccount : null
        function onProfileChanged() { root.backRequested() }
    }
}
