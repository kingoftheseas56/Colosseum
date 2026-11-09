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
    readonly property bool supportPage: Route.isSupportDocumentUrl(pageUrl)
    readonly property url bundledSupportUrl: Qt.resolvedUrl("../assets/support/index.html")
    property bool usingSupportFallback: false
    signal closeRequested()
    signal externalLinkRequested(url destination)
    function loadFailed(message) {
        root.loading = false
        if (root.supportPage && !root.usingSupportFallback) {
            root.usingSupportFallback = true
            Qt.callLater(function() {
                if (root.usingSupportFallback) web.url = root.bundledSupportUrl
            })
        } else {
            root.errorText = message
        }
    }
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
        root.usingSupportFallback = false
        if (String(web.url) === String(root.pageUrl)) web.reload()
        else web.url = root.pageUrl
    }
    onPageUrlChanged: if (root.feedbackProfile) root.reload()
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
        settings.javascriptEnabled: !root.supportPage
        settings.javascriptCanOpenWindows: false
        settings.localContentCanAccessFileUrls: root.supportPage && root.usingSupportFallback
        settings.localContentCanAccessRemoteUrls: false
        onNavigationRequested: function(request) {
            if (root.supportPage) {
                if (request.isMainFrame && (root.usingSupportFallback
                    ? String(request.url) === String(root.bundledSupportUrl)
                    : Route.isSupportDocumentUrl(request.url))) return
                request.reject()
                if (!request.isMainFrame) return
                if (request.navigationType === WebEngineNavigationRequest.LinkClickedNavigation)
                    root.externalLinkRequested(request.url)
                else if (!root.usingSupportFallback && root.loading)
                    root.loadFailed(qsTr("The support page could not load."))
                return
            }
            if (!request.isMainFrame || Route.isDocumentUrl(request.url)) return
            request.reject()
            if (root.universeDialog && /^https:\/\/en\.wikipedia\.org\/wiki\//.test(String(request.url)))
                Qt.openUrlExternally(request.url)
        }
        onNewWindowRequested: function(request) {
            if (root.supportPage && request.userInitiated)
                root.externalLinkRequested(request.requestedUrl)
        }
        onUrlChanged: {
            const closeHash = root.universeDialog ? "#close-universe" : "#close-feedback"
            if (root.transparentDialog && String(url) === String(root.pageUrl) + closeHash)
                root.closeRequested()
        }
        onLoadingChanged: function(info) {
            if (root.supportPage && (root.usingSupportFallback
                ? String(info.url) !== String(root.bundledSupportUrl)
                : !Route.isSupportDocumentUrl(info.url))) return
            root.loading = info.status === WebEngineView.LoadStartedStatus
            if (info.status === WebEngineView.LoadFailedStatus)
                root.loadFailed(qsTr("The feedback page could not load."))
            if (root.universeDialog && info.status === WebEngineView.LoadSucceededStatus
                    && String(web.url) === String(root.pageUrl)) {
                const names = JSON.stringify(root.availableUniverses)
                web.runJavaScript("window.dispatchEvent(new CustomEvent('colosseum-universe-catalogue', { detail: " + names + " }))")
            }
        }
        onRenderProcessTerminated: root.loadFailed(qsTr("The feedback page stopped. Reload to try again."))
    }
    Timer {
        interval: root.supportPage && !root.usingSupportFallback ? 10000 : 30000
        running: root.loading
        onTriggered: {
            web.stop()
            root.loadFailed(qsTr("The feedback page did not finish loading."))
        }
    }
}
