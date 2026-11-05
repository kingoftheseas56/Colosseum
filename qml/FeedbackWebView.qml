import QtQuick
import QtWebEngine
import "FeedbackRoute.js" as Route

Item {
    id: root
    property url pageUrl: ""
    property bool universeDialog: false
    property bool transparentDialog: false
    property var availableUniverses: []
    property bool loading: false
    property string errorText: ""
    signal closeRequested()
    function takeKeyboardFocus() { web.forceActiveFocus() }
    function configureProfile() {
        const profile = profilePrototype.instance()
        if (!profile) { root.errorText = qsTr("The feedback browser could not initialize. Close it and try again."); return false }
        root.feedbackProfile = profile
        web.profile = profile
        return true
    }
    function reload() {
        if (!root.feedbackProfile && !configureProfile()) return
        root.errorText = ""
        web.url = root.pageUrl
        web.reload()
    }
    onPageUrlChanged: if (root.feedbackProfile) web.url = root.pageUrl
    Component.onCompleted: if (configureProfile()) web.url = root.pageUrl
    WebEngineProfilePrototype {
        id: profilePrototype
        storageName: "colosseum-feedback"
    }
    property var feedbackProfile: null
    Connections {
        target: root.feedbackProfile
        function onDownloadRequested(download) { download.cancel() }
    }
    WebEngineView {
        id: web
        objectName: "feedbackWebDocument"
        anchors.fill: parent
        visible: root.errorText.length === 0
        backgroundColor: root.transparentDialog ? "transparent" : "#08090d"
        settings.javascriptCanOpenWindows: false
        settings.localContentCanAccessFileUrls: false
        settings.localContentCanAccessRemoteUrls: false
        onNavigationRequested: function(request) {
            if (!request.isMainFrame || Route.isDocumentUrl(request.url)) return
            request.reject()
            if (root.universeDialog && /^https:\/\/en\.wikipedia\.org\/wiki\//.test(String(request.url)))
                Qt.openUrlExternally(request.url)
        }
        onUrlChanged: {
            const closeHash = root.universeDialog ? "#close-universe" : "#close-feedback"
            if (root.transparentDialog && String(url) === String(root.pageUrl) + closeHash)
                root.closeRequested()
        }
        onLoadingChanged: function(info) {
            root.loading = info.status === WebEngineView.LoadStartedStatus
            if (info.status === WebEngineView.LoadFailedStatus)
                root.errorText = qsTr("The feedback page could not load.")
            if (root.universeDialog && info.status === WebEngineView.LoadSucceededStatus
                    && String(web.url) === String(root.pageUrl)) {
                const names = JSON.stringify(root.availableUniverses)
                web.runJavaScript("window.dispatchEvent(new CustomEvent('colosseum-universe-catalogue', { detail: " + names + " }))")
            }
        }
        onRenderProcessTerminated: root.errorText = qsTr("The feedback page stopped. Reload to try again.")
    }
    Timer {
        interval: 30000
        running: root.loading
        onTriggered: { web.stop(); root.loading = false; root.errorText = qsTr("The feedback page did not finish loading.") }
    }
}
