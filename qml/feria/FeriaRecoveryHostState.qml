// Transient presentation state only. The injected browser is the existing wrapper.
import QtQuick

Item {
    id: recoveryState
    objectName: "feriaRecoveryHostState"
    property var browser: null
    property bool active: false
    property string engine: ""
    property bool qtAvailable: false
    property bool webAvailable: false
    property string visitKey: ""
    property int epoch: 0
    property int slowEpoch: -1
    property bool starting: false
    property bool slow: false
    property bool succeeded: false
    property bool stopped: false
    property bool fallbackUsed: false
    property bool actionPending: false
    property string error: ""
    property string resumeMessage: ""
    readonly property bool busy: active && (starting || (browser && browser.loadActive === true))
    readonly property bool engineAvailable: engine === "webview2" ? webAvailable : qtAvailable
    readonly property bool alternativeAvailable: engine === "webview2" ? qtAvailable : webAvailable
    readonly property bool canStop: active && browser && browser.loadActive === true && typeof browser.stopLoad === "function"
    readonly property bool canRetry: active && !busy && engineAvailable && !actionPending
    readonly property real progress: browser && typeof browser.loadProgress === "number" ? browser.loadProgress : -1
    signal playbackSample(var sample)
    signal browserExitRequested()
    signal browserOptionsRequested()
    signal restartRequested()
    signal switchRequested(string next, bool remember)
    signal locationStarted(string location)
    signal locationCompleted(string location, bool success)

    function cancel() {
        epoch++
        if (slowTimer) slowTimer.stop()
        slow = false
        starting = false
        actionPending = false
    }
    function resetVisit() {
        cancel()
        succeeded = false
        stopped = false
        error = ""
        resumeMessage = ""
        fallbackUsed = false
    }
    function beginStartup() {
        cancel()
        if (!active) return
        error = ""
        stopped = false
        succeeded = false
        starting = true
        armSlow()
    }
    function armSlow() {
        slowEpoch = epoch
        if (slowTimer) slowTimer.restart()
    }
    function markSlow() {
        if (active && busy && slowEpoch === epoch) slow = true
    }
    function started(location) {
        if (!active) return
        cancel()
        stopped = false
        succeeded = false
        error = ""
        armSlow()
        locationStarted(location)
    }
    function completed(location, success) {
        if (!active) return
        starting = false
        succeeded = success === true
        error = success ? "" : "This page could not be loaded. Retry it or return to Feria."
        // WebView2 also emits completed on DOM readiness, before NavigationCompleted.
        if (!browser || browser.loadActive !== true) { if (slowTimer) slowTimer.stop(); slow = false }
        locationCompleted(location, success)
    }
    function failed() {
        if (!active) return
        cancel()
        succeeded = false
        error = engineAvailable ? "The browser could not start. Retry startup or choose an available compatibility mode."
                                : "This browser engine is unavailable. Return to Feria or choose an available compatibility mode."
        if (engine === "webview2" && qtAvailable && !fallbackUsed) {
            fallbackUsed = true
            var expected = epoch
            Qt.callLater(function() {
                if (recoveryState.active && recoveryState.epoch === expected && recoveryState.engine === "webview2")
                    recoveryState.switchRequested("qtwebengine", false)
            })
        }
    }
    function resumeUnavailable() {
        if (!active) return
        resumeMessage = "The saved page opened, but its saved position was not restored. Use the provider’s controls to find your place."
    }
    function retry() {
        if (!canRetry) return false
        actionPending = true
        error = ""
        stopped = false
        succeeded = false
        starting = true
        epoch++
        armSlow()
        if (browser && browser.ready && typeof browser.reload === "function") browser.reload()
        else restartRequested()
        return true
    }
    function stop() {
        if (!canStop) return false
        epoch++
        if (slowTimer) slowTimer.stop()
        slow = false
        starting = false
        stopped = true
        browser.stopLoad()
        return true
    }
    function switchEngine() {
        if (!active || !alternativeAvailable || actionPending) return false
        cancel()
        actionPending = true
        switchRequested(engine === "webview2" ? "qtwebengine" : "webview2", true)
        // Synchronous engine replacement resets startup state; keep its busy attempt guarded.
        if (busy) actionPending = true
        return true
    }
    function navigate(backwards) {
        if (!active || !browser) return false
        var permitted = backwards ? browser.navCanGoBack : browser.navCanGoForward
        var action = backwards ? browser.pageBack : browser.pageForward
        if (!permitted || typeof action !== "function") return false
        if (backwards) browser.pageBack()
        else browser.pageForward()
        return true
    }
    onActiveChanged: if (!active) cancel()
    onVisitKeyChanged: resetVisit()
    onEngineChanged: {
        cancel()
        resumeMessage = ""
        succeeded = false
        error = ""
    }
    onBusyChanged: if (!busy) { if (slowTimer) slowTimer.stop(); slow = false; actionPending = false }
    Timer { id: slowTimer; interval: 30000; onTriggered: recoveryState.markSlow() }
    function bindBrowserSignals() {
        if (!connectionOwner) return
        // Destroy the old receiver too: already queued signals cannot target a new visit.
        connectionOwner.active = false
        if (browser) connectionOwner.active = true
    }
    onBrowserChanged: bindBrowserSignals()
    Component.onCompleted: bindBrowserSignals()
    Loader {
        id: connectionOwner
        active: false
        onLoaded: {
            item.boundBrowser = recoveryState.browser
            item.boundVisit = recoveryState.visitKey
            item.boundEngine = recoveryState.engine
        }
        sourceComponent: Item {
            id: owner
            property var boundBrowser: null
            property string boundVisit: ""
            property string boundEngine: ""
            function current() {
                return recoveryState.active && boundBrowser && boundBrowser === recoveryState.browser
                    && boundVisit === recoveryState.visitKey && boundEngine === recoveryState.engine
            }
            Connections {
                target: owner.boundBrowser
                function onStarted(location) { if (owner.current()) recoveryState.started(location) }
                function onCompleted(location, success) { if (owner.current()) recoveryState.completed(location, success) }
                function onFailed(reason) { if (owner.current()) recoveryState.failed() }
                function onResumeUnavailable() { if (owner.current()) recoveryState.resumeUnavailable() }
                function onPlaybackObserved(sample) { if (owner.current()) recoveryState.playbackSample(sample) }
                function onExitRequested() { if (owner.current()) recoveryState.browserExitRequested() }
                function onOptionsRequested() { if (owner.current()) recoveryState.browserOptionsRequested() }
                function onLoadActiveChanged() {
                    if (!owner.current()) return
                    if (!owner.boundBrowser.loadActive) {
                        recoveryState.starting = false
                        recoveryState.slow = false
                        recoveryState.actionPending = false
                        if (slowTimer) slowTimer.stop()
                    }
                }
            }
        }
    }
}
