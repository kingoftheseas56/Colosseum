import QtQuick
import QtQuick.Layouts

Item {
    id: root
    required property var controller
    readonly property real u: controller.unit
    readonly property real m: controller.marginX
    readonly property bool statsContentActive: statsView.contentFocusIndex >= 0
    readonly property bool appsContentFocused: appsView.contentFocusActive
    readonly property bool contentFocused: controller.accountHistoryFocusIndex >= 0
                                            || controller.accountContentFocusActive
                                            || statsContentActive || appsContentFocused

    function resetContentFocus() {
        controller.accountContentFocusActive = false
        highlights.focusKey = ""
        statsView.clearContentFocus()
        appsView.resetContentFocus()
    }
    function revealHistoryFocus(index) { historyView.revealFocus(index) }
    function enterHighlightsContent() {
        return controller.accountTab === "highlights" && highlights.enterFromShell()
    }
    function handleHighlightsKey(key) {
        return controller.accountTab === "highlights" && highlights.handleKey(key)
    }
    function enterStatsContent() { statsView.enterContent() }
    function handleStatsKey(key) { return statsView.handleKey(key) }
    function enterAppsContent() { return appsView.enterFromShell() }
    function handleAppsKey(key) { return appsView.handleKey(key) }

    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0; color: controller.dusk }
            GradientStop { position: 1; color: controller.night }
        }
    }

    Item {
        id: head
        anchors { left: parent.left; right: parent.right; top: parent.top }
        height: 6.2 * u
        z: 4

        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                GradientStop { position: 0; color: controller.dusk }
                GradientStop { position: 0.72; color: controller.dusk }
                GradientStop { position: 1; color: "transparent" }
            }
        }

        Row {
            anchors { left: parent.left; leftMargin: m; verticalCenter: parent.verticalCenter }
            spacing: 1.2 * u
            Rectangle {
                objectName: "account-back"
                width: 6.4 * u
                height: 2.5 * u
                radius: height / 2
                color: Qt.rgba(0,0,0,0.42)
                border.width: 1
                border.color: Qt.rgba(1,1,1,0.16)
                Text {
                    anchors.centerIn: parent
                    text: "‹   Feria"
                    color: controller.mist
                    font.family: controller.uiFont
                    font.pixelSize: 0.9 * u
                }
                Rectangle {
                    objectName: "account-back-focus"
                    anchors.fill: parent
                    anchors.margins: -0.1875 * u
                    radius: parent.radius + 0.1875 * u
                    color: "transparent"
                    border.width: 0.1875 * u
                    border.color: controller.gold
                    visible: !root.contentFocused && controller.accountShellFocusIndex === 0
                }
                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    onEntered: controller.setAccountShellFocus(0)
                    onClicked: {
                        controller.setAccountShellFocus(0)
                        controller.back()
                    }
                }
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: "Your Feria"
                color: controller.ink
                font.family: controller.displayFont
                font.pixelSize: 2.35 * u
                font.weight: Font.Medium
            }
        }

        Rectangle {
            anchors.centerIn: parent
            height: 3.25 * u
            width: tabs.implicitWidth + 0.625 * u
            radius: height / 2
            color: Qt.rgba(1,1,1,0.10)
            border.width: 1
            border.color: Qt.rgba(1,1,1,0.18)
            Row {
                id: tabs
                anchors.centerIn: parent
                spacing: 0.375 * u
                Repeater {
                    model: [{k:"history",n:"History"},{k:"highlights",n:"Highlights"},{k:"stats",n:"Stats"},{k:"apps",n:"Apps"}]
                    delegate: PorticoCombinedPill {
                        required property int index
                        required property var modelData
                        objectName: "account-tab-" + modelData.k
                        unit: u
                        label: modelData.n
                        selected: controller.accountTab === modelData.k
                        focusedState: !root.contentFocused && controller.accountShellFocusIndex === index + 1
                        onTriggered: {
                            controller.setAccountShellFocus(index + 1)
                            controller.accountTab = modelData.k
                        }
                        onEntered: controller.setAccountShellFocus(index + 1)
                    }
                }
            }
        }
    }

    PorticoCombinedParityAccountHistory {
        id: historyView
        anchors { left: parent.left; right: parent.right; top: head.bottom; bottom: parent.bottom }
        controller: root.controller
        visible: controller.accountTab === "history"
    }

    PorticoCombinedAccountHighlights {
        id: highlights
        anchors { left: parent.left; right: parent.right; top: head.bottom; bottom: parent.bottom }
        controller: root.controller
        visible: controller.accountTab === "highlights"
    }
    PorticoCombinedAccountStats {
        id: statsView
        anchors { left: parent.left; right: parent.right; top: head.bottom; bottom: parent.bottom }
        controller: root.controller
        visible: controller.accountTab === "stats"
    }
    PorticoCombinedAccountApps {
        id: appsView
        anchors { left: parent.left; right: parent.right; top: head.bottom; bottom: parent.bottom }
        controller: root.controller
        visible: controller.accountTab === "apps"
    }

    Connections {
        target: controller
        function onAccountTabChanged() {
            root.resetContentFocus()
        }
        function onViewStateChanged() {
            if (controller.viewState !== "account") root.resetContentFocus()
        }
        function onAccountShellFocusIndexChanged() {
            if (controller.accountShellFocusIndex >= 0 && statsView.contentFocusIndex >= 0)
                statsView.clearContentFocus()
        }
    }
}
