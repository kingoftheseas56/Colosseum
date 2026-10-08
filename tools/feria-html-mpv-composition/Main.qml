import QtQuick
import QtQuick.Window
import QtWebEngine
import QtWebChannel
import Experiment.Player 1.0

Window {
    id: root
    width: 1000; height: 780; visible: true
    color: "#141a20"
    title: "Feria HTML + unchanged MpvItem | isolated feasibility prototype"
    property int surfaceGeneration: 0
    Component { id: playerComponent; MpvItem { objectName: "nativePlayer" } }
    Loader {
        id: playerLoader
        active: false
        sourceComponent: playerComponent
        x: nativeHost.aperture.x; y: nativeHost.aperture.y
        width: Math.max(1, nativeHost.aperture.width)
        height: Math.max(1, nativeHost.aperture.height)
        visible: nativeHost.surfaceVisible
        onLoaded: nativeHost.bindSurface(item)
    }
    WebChannel {
        id: channel
    }
    WebEngineView {
        id: web
        anchors.fill: parent
        backgroundColor: "transparent"
        profile: localProfile
        webChannel: channel
        url: "about:blank"
        settings.localContentCanAccessFileUrls: true
        settings.localContentCanAccessRemoteUrls: false
        settings.playbackRequiresUserGesture: false
        settings.fullScreenSupportEnabled: false
        onLoadingChanged: function(info) {
            console.log("DOCUMENT LOAD", info.status, info.errorString)
            if (info.status === WebEngineView.LoadStartedStatus) nativeHost.navigate()
        }
        onNavigationRequested: function(request) {
            if (request.url.toString().split('#')[0] !== trustedPageUrl.toString()) request.action = WebEngineNavigationRequest.IgnoreRequest
        }
        onNewWindowRequested: function(request) { console.log("BLOCKED new window") }
        onJavaScriptConsoleMessage: function(level, message, line, source) { console.log("HTML", message, "line", line) }
        onRenderProcessTerminated: function(status, code) { if (testEnabled) testProbe.result("renderer terminated", false, {status: status, code: code}) }
    }
    Connections {
        target: nativeHost
        function onSurfaceRequested(enabled) {
            const generation = ++root.surfaceGeneration
            playerLoader.active = false
            Qt.callLater(function() { if (generation === root.surfaceGeneration) playerLoader.active = enabled })
        }
        function onFullscreenRequested(fullscreen) { if (fullscreen) root.showFullScreen(); else root.showNormal() }
    }
    Connections {
        target: testProbe
        function onZoomRequested(factor) { web.zoomFactor = factor }
        function onWebOpacityRequested(value) { web.opacity = value }
    }
    Component.onCompleted: {
        channel.registerObject("player", playerFacade)
        if (testEnabled) channel.registerObject("probe", testProbe)
        web.url = trustedPageUrl
    }
}
