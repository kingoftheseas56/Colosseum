import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "FeedbackRoute.js" as Route
import "SystemFocusContainment.js" as FocusContainment

FocusScope {
    id: root
    property url pageUrl: Route.pageUrl(Qt.application.version, Qt.platform.os)
    property url browserSource: Qt.resolvedUrl("FeedbackWebView.qml")
    readonly property var browserItem: browser.item
    signal closeRequested()
    signal minimizeRequested()
    signal fullscreenRequested()
    signal quitRequested()
    Theme { id: theme }
    Rectangle { anchors.fill: parent; color: "#08090d" }
    RowLayout {
        id: header
        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 16 }
        height: 48
        spacing: 12
        BackAction {
            id: backButton
            objectName: "feedbackBack"
            variant: "immersive"
            tip: qsTr("Back")
            Layout.preferredWidth: 48
            Layout.preferredHeight: 48
            onTriggered: root.closeRequested()
        }
        Label { text: qsTr("Feedback"); color: theme.ink; font.family: theme.ui; font.pixelSize: 15; Layout.fillWidth: true }
        Item {
            Layout.preferredWidth: 48
            Layout.preferredHeight: 48
            Rectangle {
                anchors.fill: parent
                radius: 12
                color: refreshInput.interactionActive ? theme.glassHi : "transparent"
            }
            Image {
                anchors.centerIn: parent
                width: 22; height: 22
                source: "../assets/icons/lucide/rotate-cw.svg"
                sourceSize.width: 44; sourceSize.height: 44
                opacity: refreshInput.interactionActive ? 1 : 0.8
            }
            KeyboardAction {
                id: refreshInput
                objectName: "feedbackRefresh"
                anchors.fill: parent
                accessibleName: qsTr("Refresh feedback")
                focusRadius: 12
                onTriggered: {
                    if (root.browserItem && root.browserItem.reload) root.browserItem.reload()
                    else { browser.active = false; Qt.callLater(function() { browser.active = true }) }
                }
            }
            ToolTip.visible: refreshInput.interactionActive
            ToolTip.delay: 600
            ToolTip.text: qsTr("Refresh")
        }
        HouseWindowControls {
            objectPrefix: "feedbackWindow"
            Layout.alignment: Qt.AlignVCenter
            onMinimizeRequested: root.minimizeRequested()
            onFullscreenRequested: root.fullscreenRequested()
            onQuitRequested: root.quitRequested()
        }
    }
    Loader {
        id: browser
        objectName: "feedbackBrowserLoader"
        anchors { left: parent.left; right: parent.right; top: header.bottom; bottom: parent.bottom; topMargin: 12 }
        source: root.browserSource
        onLoaded: item.pageUrl = root.pageUrl
    }
    Connections {
        target: browser.item
        ignoreUnknownSignals: true
        function onCloseRequested() { root.closeRequested() }
    }
    Column {
        anchors.centerIn: browser
        width: Math.min(root.width - 48, 440)
        spacing: 18
        visible: browser.status === Loader.Error || (root.browserItem && root.browserItem.errorText.length > 0)
        Text {
            width: parent.width
            text: qsTr("Feedback could not load. Check your connection and try again.")
            color: "#f7f7f5"; wrapMode: Text.WordWrap; font.pixelSize: 16
        }
        Button { text: qsTr("Open in browser"); height: 48; onClicked: Qt.openUrlExternally(root.pageUrl) }
    }
    BusyIndicator {
        anchors.centerIn: browser
        running: browser.status === Loader.Loading || (!!root.browserItem && root.browserItem.loading)
        visible: running
    }
    Keys.onEscapePressed: root.closeRequested()
    Keys.onBackPressed: root.closeRequested()
    Keys.priority: Keys.AfterItem
    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Tab && FocusContainment.move(root.Window.window, root, !(event.modifiers & Qt.ShiftModifier)))
            event.accepted = true
    }
    Component.onCompleted: backButton.focusControl()
}
