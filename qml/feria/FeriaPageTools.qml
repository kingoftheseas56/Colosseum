// Copyright (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: BSD-3-Clause
// Native Find/zoom wiring adapted from Qt Quick Nano Browser BrowserWindow.qml,
// qt/qtwebengine c8e817ac60146921c9d73de2e420e1445c9cf513.
// See NanoBrowser-LICENSE.txt. Feria adds selected-page/lifecycle fencing.
import QtQuick
import QtWebEngine

Item {
    id: tools
    required property var browser
    readonly property bool active: browser && browser.visible && !browser.closingSession
    property bool findOpen: false
    property string query: ""
    property int numberOfMatches: 0
    property int activeMatch: 0
    property bool pending: false
    property bool hasResults: false
    property int generation: 0
    property int requestGeneration: -1
    property bool issuing: false
    property bool restoreAfterDismissal: false
    property bool clearAfterDismissal: false
    readonly property real minimumZoom: 0.25
    readonly property real maximumZoom: 5.0
    readonly property var zoomSteps: [0.25, 0.33, 0.5, 0.67, 0.75, 0.8, 0.9, 1,
                                     1.1, 1.25, 1.5, 1.75, 2, 2.5, 3, 4, 5]
    readonly property int zoomPercent: browser ? Math.round(browser.zoomFactor * 100) : 100
    readonly property bool canZoomIn: active && browser.zoomFactor < maximumZoom - 0.001
    readonly property bool canZoomOut: active && browser.zoomFactor > minimumZoom + 0.001
    signal focusFindRequested()

    function openFind() {
        if (!active) return
        restoreAfterDismissal = false
        findOpen = true
        focusFindRequested()
    }
    function restorePageFocus(clearSelection) {
        var focusGeneration = generation
        Qt.callLater(function() {
            if (tools.active && !tools.findOpen && tools.generation === focusGeneration) {
                tools.browser.forceActiveFocus()
                if (clearSelection) {
                    tools.browser.triggerWebAction(WebEngineView.Unselect)
                    // Focus reaches Chromium asynchronously. Fence the final
                    // native Unselect behind that renderer round trip; do not
                    // let a late dismissal affect a newer Find/route/session.
                    tools.browser.evaluateJavaScript("document.hasFocus()", function(focused) {
                        if (focused && tools.active && !tools.findOpen
                                && tools.generation === focusGeneration && tools.browser.activeFocus)
                            tools.browser.triggerWebAction(WebEngineView.Unselect)
                    })
                }
            }
        })
    }
    function finishDismissal() {
        if (!restoreAfterDismissal) return
        restoreAfterDismissal = false
        restorePageFocus(clearAfterDismissal)
    }
    function closeFind(restoreFocus, clearQuery) {
        var hadSearch = findOpen && query.length > 0
        restoreAfterDismissal = Boolean(restoreFocus)
        clearAfterDismissal = hadSearch
        ++generation
        requestGeneration = -1
        findOpen = false
        pending = false
        hasResults = false
        numberOfMatches = 0
        activeMatch = 0
        if (clearQuery) query = ""
        if (browser) {
            // Qt's empty search cancels the current native request. Its helper
            // rejects older request IDs, so late replies cannot re-open Find.
            issuing = true
            browser.findText("")
            issuing = false
        }
        // Popup.closed owns restoring focus after its exit transition.
    }
    function search(text, backward) {
        if (!active || !findOpen) return
        query = String(text)
        requestGeneration = generation
        hasResults = false
        numberOfMatches = 0
        activeMatch = 0
        pending = query.length > 0 && browser.documentReady
        issuing = true
        browser.findText(pending ? query : "", backward ? WebEngineView.FindBackward : 0)
        issuing = false
    }
    function findNext() {
        if (!findOpen) openFind()
        search(query, false)
    }
    function findPrevious() {
        if (!findOpen) openFind()
        search(query, true)
    }
    function zoomIn() {
        if (!canZoomIn) return
        for (var factor of zoomSteps) {
            if (factor > browser.zoomFactor + 0.001) { browser.zoomFactor = factor; return }
        }
    }
    function zoomOut() {
        if (!canZoomOut) return
        for (var i = zoomSteps.length - 1; i >= 0; --i) {
            if (zoomSteps[i] < browser.zoomFactor - 0.001) { browser.zoomFactor = zoomSteps[i]; return }
        }
    }
    function resetZoom() { if (active) browser.zoomFactor = 1 }
    onActiveChanged: if (!active) closeFind(false, true)
    Connections {
        target: tools.browser
        function onUrlChanged() { tools.closeFind(false, true) }
        function onRenderProcessTerminated() { tools.closeFind(false, true) }
        function onNavigationGenerationChanged() { tools.closeFind(false, true) }
        function onClosingSessionChanged() { if (tools.browser.closingSession) tools.closeFind(false, true) }
        function onFindTextFinished(result) {
            // Qt may synchronously announce cancellation of the superseded
            // request while findText() starts the new one. Ignore that result.
            if (tools.issuing || !tools.active || !tools.findOpen
                    || !tools.query.length || tools.requestGeneration !== tools.generation) return
            tools.pending = false
            tools.hasResults = true
            tools.numberOfMatches = result.numberOfMatches
            tools.activeMatch = result.activeMatch
        }
    }
    Shortcut { sequence: "Ctrl+F"; context: Qt.WindowShortcut; enabled: tools.active; onActivated: tools.openFind() }
    Shortcut { sequence: "F3"; context: Qt.WindowShortcut; enabled: tools.active; onActivated: tools.findNext() }
    Shortcut { sequence: "Shift+F3"; context: Qt.WindowShortcut; enabled: tools.active; onActivated: tools.findPrevious() }
    Shortcut { sequences: ["Ctrl++", "Ctrl+="]; context: Qt.WindowShortcut; enabled: tools.active; onActivated: tools.zoomIn() }
    Shortcut { sequence: "Ctrl+-"; context: Qt.WindowShortcut; enabled: tools.active; onActivated: tools.zoomOut() }
    Shortcut { sequence: "Ctrl+0"; context: Qt.WindowShortcut; enabled: tools.active; onActivated: tools.resetZoom() }
}
