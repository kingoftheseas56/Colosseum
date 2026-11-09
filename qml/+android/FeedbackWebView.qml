import QtQuick
import "../FeedbackRoute.js" as Route

Item {
    id: root
    property url pageUrl: ""
    property var backend: typeof AndroidFeedbackBrowser !== "undefined" ? AndroidFeedbackBrowser : null
    readonly property bool loading: !!backend && backend.loading
    readonly property string errorText: backend ? backend.errorText : qsTr("Feedback browser is unavailable.")
    readonly property bool supportPage: Route.isSupportDocumentUrl(pageUrl)
    property bool usingSupportFallback: false
    signal closeRequested()
    signal externalLinkRequested(url destination)
    function reload() {
        if (!root.backend || !root.pageUrl.toString().length) return
        root.usingSupportFallback = false
        root.backend.open(root.pageUrl)
    }
    onPageUrlChanged: root.reload()
    Component.onDestruction: if (backend) backend.close()
    WindowContainer {
        anchors.fill: parent
        visible: root.errorText.length === 0
        window: root.backend ? root.backend.foreignWindow : null
    }
    Connections {
        target: root.backend
        function onCloseRequested() { root.closeRequested() }
        function onExternalLinkRequested(destination) { root.externalLinkRequested(destination) }
        function onStateChanged() {
            if (root.supportPage && !root.usingSupportFallback && root.backend.errorText.length) {
                root.usingSupportFallback = true
                Qt.callLater(function() {
                    if (root.usingSupportFallback && root.backend)
                        root.backend.open(root.pageUrl, true)
                })
            }
        }
    }
}
