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

    // Native Save has no arbitrary URL downloader and never calls save(path).
    // Context is only a short-lived capability for the original native target.
    property int contextSerial: 0
    property var contextSnapshot: null
    property int saveOperationSerial: 0
    property string saveStatus: ""
    property var saveOutcome: ({})
    readonly property bool saveContextOpen: contextSnapshot !== null
    readonly property bool canSavePage: active && browser.documentReady && !browser.rendererLost
        && !browser.replacementLoadPending && browser.saveAdmissionOwner !== null
        && browser.saveAdmissionOwner.active && !browser.saveAdmissionOwner.retired
        && browser.saveAdmissionOwner.profile === browser.profile
        && browser.saveAdmissionOwner.pageSaveIngressExclusive === true
        && !browser.saveAdmissionOwner.pageSaveIntent
    readonly property bool canSaveLink: contextSnapshot !== null && contextSnapshot.linkAvailable
    readonly property bool canSaveImage: contextSnapshot !== null && contextSnapshot.imageAvailable
    readonly property bool canSaveMedia: contextSnapshot !== null && contextSnapshot.mediaAvailable

    function admittedSaveUrl(value) {
        var url = String(value || "")
        if (!/^https?:\/\//i.test(url) || /[\x00-\x20\\]/.test(url)) return false
        var authority = url.substring(url.indexOf("://") + 3).split(/[/?#]/)[0]
        return authority.length > 0 && authority.indexOf("@") < 0
    }
    function saveBindingNow() {
        return Object.assign({}, browser.saveBinding || ({}))
    }
    function mainFrameContextOnly() {
        // Public Qt does not expose the source frame of a context request.
        // Refuse documents with subframes rather than invent their provenance.
        var frame = browser.mainFrame
        return !!frame && frame.isValid !== false && frame.children !== undefined
            && frame.children.length === 0
    }
    function saveActionAvailable(kind, snapshot) {
        var action = kind === "page" ? WebEngineView.SavePage
            : kind === "link" ? WebEngineView.DownloadLinkToDisk
            : kind === "image" ? WebEngineView.DownloadImageToDisk
            : kind === "media" ? WebEngineView.DownloadMediaToDisk : -1
        var nativeAction = action < 0 ? null : browser.action(action)
        if (!nativeAction || !nativeAction.enabled) return false
        if (kind === "page") return admittedSaveUrl(browser.url)
        if (!snapshot || !snapshot.mainFrameOnly || !mainFrameContextOnly()) return false
        if (kind === "link") return admittedSaveUrl(snapshot.linkUrl)
        if (kind === "image") return snapshot.mediaType === ContextMenuRequest.MediaTypeImage
            && admittedSaveUrl(snapshot.mediaUrl)
        return (snapshot.mediaType === ContextMenuRequest.MediaTypeAudio
                || snapshot.mediaType === ContextMenuRequest.MediaTypeVideo)
            && (snapshot.mediaFlags & ContextMenuRequest.MediaCanSave) !== 0
            && admittedSaveUrl(snapshot.mediaUrl)
    }
    function captureSaveContext(request) {
        invalidateSaveContext("superseded")
        // Qt appends SavePage for background/link/media contexts, but not plain
        // editable or selected-text contexts. Replace only Save-bearing menus,
        // across every retained view, so page-save single-flight is exclusive.
        var saveBearing = String(request.linkUrl || "").length > 0
            || Number(request.mediaType) !== ContextMenuRequest.MediaTypeNone
            || (!request.isContentEditable && !String(request.selectedText || "").length)
        var replaceMenu = browser.saveAdmissionOwner !== null && saveBearing
        if (replaceMenu) request.accepted = true
        if (!saveBearing) return null
        if (!active || !browser.documentReady || browser.rendererLost || browser.replacementLoadPending)
            return null
        var snapshot = {
            contextId: "context-" + contextSerial, serial: contextSerial,
            navigationGeneration: browser.navigationGeneration,
            binding: Object.freeze(saveBindingNow()), profile: browser.profile,
            linkUrl: String(request.linkUrl || ""), mediaUrl: String(request.mediaUrl || ""),
            mediaType: Number(request.mediaType), mediaFlags: Number(request.mediaFlags || 0),
            mainFrameOnly: mainFrameContextOnly(), frameProvenance: "unavailable",
            nativeMenuReplaced: false
        }
        snapshot.linkAvailable = saveActionAvailable("link", snapshot)
        snapshot.imageAvailable = saveActionAvailable("image", snapshot)
        snapshot.mediaAvailable = saveActionAvailable("media", snapshot)
        snapshot.nativeMenuReplaced = replaceMenu
        if (replaceMenu && !browser.saveContextControlsAvailable)
            browser.message("Context Save controls are available in the app view. Ctrl+S saves the page.")
        contextSnapshot = Object.freeze(snapshot)
        return contextSnapshot
    }
    function invalidateSaveContext(reason) {
        ++contextSerial
        contextSnapshot = null
    }
    function currentSaveContext(contextId) {
        if (!active || !contextSnapshot || contextId !== contextSnapshot.contextId) return null
        var snapshot = contextSnapshot
        if (!browser.documentReady || browser.closingSession || browser.rendererLost
                || browser.replacementLoadPending || snapshot.profile !== browser.profile
                || snapshot.navigationGeneration !== browser.navigationGeneration) return null
        var fresh = saveBindingNow()
        var keys = ["accountEpoch", "profileId", "spaceId", "pageGeneration", "documentGeneration", "hostId", "hostGeneration"]
        for (var key of keys) if (snapshot.binding[key] !== fresh[key]) return null
        return snapshot
    }
    function contextNativeAction(contextId, kind) {
        var snapshot = currentSaveContext(contextId)
        if (!snapshot) return saveResult("invalidated", "This context is no longer current. Open it again.")
        var action = -1
        if (kind === "openLink" && admittedSaveUrl(snapshot.linkUrl)) action = WebEngineView.OpenLinkInNewTab
        else if (kind === "copyLink" && snapshot.linkUrl.length) action = WebEngineView.CopyLinkToClipboard
        else if (kind === "copyImage" && (snapshot.mediaType === ContextMenuRequest.MediaTypeImage
                || snapshot.mediaType === ContextMenuRequest.MediaTypeCanvas))
            action = WebEngineView.CopyImageToClipboard
        else if (kind === "copyMediaLink" && snapshot.mediaUrl.length)
            action = WebEngineView.CopyMediaUrlToClipboard
        var nativeAction = action < 0 ? null : browser.action(action)
        if (!nativeAction || !nativeAction.enabled)
            return saveResult("unsupported", "This context action is unavailable.")
        browser.wakePage()
        browser.triggerWebAction(action)
        invalidateSaveContext("native-action")
        return saveResult("dispatched", "")
    }
    function saveResult(status, reason, operationId) {
        saveStatus = reason || ""
        saveOutcome = {status: status, reason: reason || "", operationId: operationId || ""}
        if (reason && browser && typeof browser.message === "function") browser.message(reason)
        return saveOutcome
    }
    function dispatchNativeSave(snapshot, kind, format) {
        var owner = browser.saveAdmissionOwner
        if (!owner || !owner.active || owner.profile !== browser.profile
                || typeof owner.beginNativeSave !== "function")
            return saveResult("unsupported", "Saving is unavailable for this browser profile.")
        if (kind === "page" && typeof owner.pendingPageSaveForView === "function"
                && owner.pendingPageSaveForView(browser))
            return saveResult("blocked", "Finish the pending page save before starting another.")
        var intent = Object.assign({}, snapshot, {kind: kind, format: format,
            operationId: "save-operation-" + (++saveOperationSerial)})
        var token = owner.beginNativeSave(browser, intent)
        if (!token) return saveResult("blocked", owner.nativeSaveBlockedReason
            || "The download owner could not admit this Save request.")
        var action = kind === "page" ? WebEngineView.SavePage
            : kind === "link" ? WebEngineView.DownloadLinkToDisk
            : kind === "image" ? WebEngineView.DownloadImageToDisk : WebEngineView.DownloadMediaToDisk
        // Dispatch is not native admission or completion. The profile owns the
        // actual request, destination policy, state, and terminal result.
        browser.wakePage()
        browser.triggerWebAction(action)
        invalidateSaveContext("dispatched")
        return saveResult("dispatched", "", token)
    }
    function saveFromContext(contextId, kind) {
        var snapshot = currentSaveContext(contextId)
        if (!snapshot) return saveResult("invalidated", "This Save context is no longer current. Open it again.")
        if (["link", "image", "media"].indexOf(kind) < 0 || !saveActionAvailable(kind, snapshot))
            return saveResult("unsupported", "This native context cannot be saved. Frame-specific and protected media export are unavailable.")
        return dispatchNativeSave(snapshot, kind, -1)
    }
    function savePage(format) {
        // Fresh host commands read the current document. Only pending contexts
        // are fenced by their captured document; host presentation is not.
        if (!active || !browser.documentReady || browser.closingSession || browser.rendererLost
                || browser.replacementLoadPending)
            return saveResult("blocked", "Wait for the page to finish loading before saving.")
        if (!saveActionAvailable("page", null))
            return saveResult("unsupported", "This page does not support native Save.")
        var saveFormat = format === undefined ? WebEngineDownloadRequest.MimeHtmlSaveFormat : Number(format)
        if ([WebEngineDownloadRequest.SingleHtmlSaveFormat, WebEngineDownloadRequest.CompleteHtmlSaveFormat,
                WebEngineDownloadRequest.MimeHtmlSaveFormat].indexOf(saveFormat) < 0)
            return saveResult("unsupported", "This page-save format is unavailable.")
        return dispatchNativeSave({navigationGeneration: browser.navigationGeneration,
            binding: Object.freeze(saveBindingNow()), profile: browser.profile,
            sourceUrl: String(browser.url), frameProvenance: "main-document"}, "page", saveFormat)
    }

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
    onActiveChanged: if (!active) { closeFind(false, true); invalidateSaveContext("inactive") }
    Connections {
        target: tools.browser
        function onUrlChanged() { tools.closeFind(false, true); tools.invalidateSaveContext("url") }
        function onRenderProcessTerminated() { tools.closeFind(false, true); tools.invalidateSaveContext("renderer") }
        function onNavigationGenerationChanged() { tools.closeFind(false, true); tools.invalidateSaveContext("navigation") }
        function onClosingSessionChanged() { if (tools.browser.closingSession) { tools.closeFind(false, true); tools.invalidateSaveContext("closed") } }
        function onSaveBindingChanged() { tools.invalidateSaveContext("binding") }
        function onProfileChanged() { tools.invalidateSaveContext("profile") }
        function onContextMenuRequested(request) { tools.captureSaveContext(request) }
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
    Shortcut { sequence: "Ctrl+S"; context: Qt.WindowShortcut; enabled: tools.active; onActivated: tools.savePage() }
    Shortcut { sequence: "Ctrl+F"; context: Qt.WindowShortcut; enabled: tools.active; onActivated: tools.openFind() }
    Shortcut { sequence: "F3"; context: Qt.WindowShortcut; enabled: tools.active; onActivated: tools.findNext() }
    Shortcut { sequence: "Shift+F3"; context: Qt.WindowShortcut; enabled: tools.active; onActivated: tools.findPrevious() }
    Shortcut { sequences: ["Ctrl++", "Ctrl+="]; context: Qt.WindowShortcut; enabled: tools.active; onActivated: tools.zoomIn() }
    Shortcut { sequence: "Ctrl+-"; context: Qt.WindowShortcut; enabled: tools.active; onActivated: tools.zoomOut() }
    Shortcut { sequence: "Ctrl+0"; context: Qt.WindowShortcut; enabled: tools.active; onActivated: tools.resetZoom() }
}
