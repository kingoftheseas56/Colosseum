import QtQuick

Item {
    id: root
    property url pageUrl: ""
    property var backend: typeof AndroidFeedbackBrowser !== "undefined" ? AndroidFeedbackBrowser : null
    readonly property bool loading: !!backend && backend.loading
    readonly property string errorText: backend ? backend.errorText : qsTr("Feedback browser is unavailable.")
    signal closeRequested()
    function reload() { if (backend) backend.open(pageUrl) }
    onPageUrlChanged: if (backend && pageUrl.toString().length) backend.open(pageUrl)
    Component.onDestruction: if (backend) backend.close()
    WindowContainer {
        anchors.fill: parent
        visible: root.errorText.length === 0
        window: root.backend ? root.backend.foreignWindow : null
    }
    Connections {
        target: root.backend
        function onCloseRequested() { root.closeRequested() }
    }
}
