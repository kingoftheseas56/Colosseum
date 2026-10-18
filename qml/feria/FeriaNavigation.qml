// Native per-view actions/history: behavioral adaptation of Qt Nano Browser
// and Lifecycle Browser (BSD-3-Clause, NanoBrowser-LICENSE.txt). No donor shell.
import QtQuick
import QtQml.Models
import QtWebEngine

Item {
    id: navigation
    required property var browser
    readonly property bool usable: browser !== null && !browser.closingSession
        && !browser.adoptingWindowRequest && !browser.clearingSession
    readonly property bool canGoBack: usable && browser.canGoBack
    readonly property bool canGoForward: usable && browser.canGoForward
    readonly property bool loading: usable && browser.loading
    readonly property int progress: usable ? browser.loadProgress : 0
    readonly property var historyItems: browser ? browser.history.items : null
    readonly property int backCount: backRows.count
    readonly property int forwardCount: forwardRows.count
    property int historyRevision: 0
    visible: false

    // Qt's history models are QAbstractItemModels, not JavaScript arrays.
    // Count their rows without retaining a second URL/history store.
    Instantiator {
        id: backRows
        model: navigation.browser ? navigation.browser.history.backItems : null
        delegate: QtObject {}
    }
    Instantiator {
        id: forwardRows
        model: navigation.browser ? navigation.browser.history.forwardItems : null
        delegate: QtObject {}
    }
    Connections {
        target: navigation.browser
        function onUrlChanged() { ++navigation.historyRevision }
        function onLoadingChanged() { ++navigation.historyRevision }
    }
    Connections {
        target: navigation.historyItems
        function onModelReset() { ++navigation.historyRevision }
    }
    // Native commands run through the shared operation contract: each
    // dispatch admits a typed operation that settles exactly once on the
    // engine's terminal load state.
    function back() {
        if (!canGoBack) return false
        browser.admitNavigation("back")
        browser.wakePage()
        browser.goBack()
        return true
    }
    function forward() {
        if (!canGoForward) return false
        browser.admitNavigation("forward")
        browser.wakePage()
        browser.goForward()
        return true
    }
    function reload() {
        if (!usable) return false
        browser.admitNavigation("reload")
        browser.wakePage()
        browser.reload()
        return true
    }
    function stop() {
        if (!loading) return false
        browser.stopLoad()
        return true
    }
    function goToOffset(offset, revision) {
        if (!usable || revision !== historyRevision || !Number.isInteger(offset)
            || offset === 0 || offset < -backCount || offset > forwardCount) return false
        browser.wakePage()
        browser.admitNavigation(offset < 0 ? "back" : "forward")
        // Explicit chooser offsets intentionally differ from Back's engine
        // skip-on-back/forward policy. Never replace this with URL assignment.
        browser.goBackOrForward(offset)
        return true
    }
}
