import QtQuick
import QtWebEngine
import QtWebChannel
import Colosseum.Player 1.0
import Colosseum.FeriaPlayer 1.0 as Native

Item {
    id: root
    objectName: "feriaNativePlayer"
    required property var candidate
    required property string documentToken
    property var headers: ({})
    property bool suspended: false
    property real initialPosition: candidate ? candidate.position || 0 : 0
    readonly property alias backend: playback
    readonly property var currentState: playback.currentState
    signal closed(real position)
    signal fullscreenRequested()
    signal unavailable(string reason)
    function takeFocus() { controls.forceActiveFocus() }
    function closePlayer() {
        var position = Number(currentState.position || 0)
        closed(position)
    }
    onSuspendedChanged: playback.setSuspended(suspended)
    Rectangle { anchors.fill: parent; color: "#08090d" }
    Native.FeriaNativePlayer {
        id: playback
        onCloseRequested: root.closePlayer()
        onFullscreenRequested: root.fullscreenRequested()
    }
    Loader {
        id: surface
        anchors.fill: parent
        active: playback.active
        sourceComponent: MpvItem { objectName: "feriaNativeMpvSurface" }
        onLoaded: playback.bindSurface(item)
    }
    WebChannel { id: channel }
    WebEngineView {
        id: controls
        focus: true
        objectName: "feriaNativeHtmlControls"
        anchors.fill: parent
        backgroundColor: "transparent"
        profile: WebEngineProfile { offTheRecord: true }
        webChannel: channel
        url: Qt.resolvedUrl("player/index.html")
        settings.localContentCanAccessRemoteUrls: false
        settings.localContentCanAccessFileUrls: true
        settings.javascriptCanOpenWindows: false
        onNavigationRequested: function(request) {
            if (String(request.url).split('#')[0] !== String(Qt.resolvedUrl("player/index.html"))) request.reject()
        }
        onNewWindowRequested: function(request) {}
        onRenderProcessTerminated: root.unavailable("Player controls stopped responding. Return to the page and try again.")
    }
    Component.onCompleted: {
        channel.registerObject("player", playback)
        playback.setSuspended(suspended)
        if (!playback.openSource(candidate.source, documentToken, initialPosition,
                candidate.paused === true, Number(candidate.volume), Number(candidate.rate), headers, candidate.audioSource || ""))
            unavailable("This source cannot be opened in Feria player.")
        Qt.callLater(root.takeFocus)
    }
    Component.onDestruction: playback.close()
}
