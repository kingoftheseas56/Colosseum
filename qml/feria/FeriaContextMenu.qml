import QtQuick
import QtQuick.Controls
import QtWebEngine
import ".." as Colosseum

// Browser-owned menu. A context snapshot never stores Qt's ephemeral request
// object; all contextual actions go back through the originating WebEngineView.
Menu {
    id: pageMenu
    objectName: "feriaPageMenu"
    required property var browser
    parent: browser
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    palette.window: theme.biblioWashTop
    palette.windowText: theme.ink
    palette.text: theme.ink
    palette.highlight: theme.glassHi
    palette.highlightedText: theme.ink
    font.family: theme.ui
    Colosseum.Theme { id: theme }

    property bool contextActive: false
    property int contextSerial: 0
    property int documentGeneration: -1
    property bool hasLink: false
    property bool hasImage: false
    property bool isEditable: false
    property bool hasSelection: false
    property int editFlags: 0
    property string selectedText: ""
    property string imageAddress: ""

    onClosed: {
        // MenuItem.onTriggered runs during close; invalidate afterwards, not
        // before the selected contextual action is dispatched.
        const serial = contextSerial
        Qt.callLater(function() {
            if (pageMenu.contextSerial === serial && !pageMenu.visible)
                pageMenu.contextActive = false
        })
    }
    function live() {
        return contextActive && browser && browser.visible && !browser.suppressed
            && !browser.closingSession && !browser.clearingSession
            && !browser.adoptingWindowRequest && !browser.rendererLost
            && browser.navigationGeneration === documentGeneration
    }
    function cancel() {
        contextActive = false
        if (visible) close()
    }
    function openForRequest(request) {
        cancel()
        if (!browser || !browser.visible || browser.suppressed
            || browser.closingSession || browser.clearingSession
            || browser.adoptingWindowRequest || browser.rendererLost) return
        // QWebEngineContextMenuRequest is valid for this synchronous signal
        // only. Do not hold it in a QML property or an asynchronous closure.
        hasLink = String(request.linkUrl).length > 0
        imageAddress = String(request.mediaUrl)
        hasImage = request.mediaType === ContextMenuRequest.MediaTypeImage
            && imageAddress.length > 0
        isEditable = Boolean(request.isContentEditable)
        selectedText = String(request.selectedText)
        hasSelection = !isEditable && selectedText.length > 0
        editFlags = Number(request.editFlags)
        documentGeneration = browser.navigationGeneration
        contextActive = true
        ++contextSerial
        x = Math.max(0, Math.round(Number(request.x)))
        y = Math.max(0, Math.round(Number(request.y)))
        popup()
    }
    function invoke(action, editing) {
        if (!live()) return
        contextActive = false
        // Refocus the originating view for Chromium's focused-edit actions.
        if (editing) browser.forceActiveFocus()
        browser.triggerWebAction(action)
        close()
    }
    function navigate(direction) {
        if (!live()) return
        contextActive = false
        close()
        if (direction === "back") browser.pageBack()
        else if (direction === "forward") browser.pageForward()
        else browser.navigation.reload()
    }
    function openImageTab() {
        if (!live() || !hasImage) return
        const url = imageAddress
        contextActive = false
        close()
        browser.imageTabRequested(url)
    }
    function entries() {
        // Also provides deterministic inspection of actual MenuItem objects
        // without relying on a private Qt overlay delegate implementation.
        return [openLink, copyLink, openImage, saveImage, copyImage,
            cutText, copyText, pasteText, selectAll, back, forward, reload]
    }
    function visibleEntries() {
        return entries().filter(function(item) { return item.visible })
            .map(function(item) { return String(item.objectName) })
    }
    function entry(objectName) {
        const rows = entries()
        for (let i = 0; i < rows.length; ++i)
            if (rows[i].objectName === objectName) return rows[i]
        return null
    }

    MenuItem {
        id: openLink
        objectName: "feriaPageMenuOpenLink"
        text: "Open link in new tab"
        visible: pageMenu.hasLink
        onTriggered: pageMenu.invoke(WebEngineView.OpenLinkInNewTab, false)
    }
    MenuItem {
        id: copyLink
        objectName: "feriaPageMenuCopyLinkAddress"
        text: "Copy link address"
        visible: pageMenu.hasLink
        onTriggered: pageMenu.invoke(WebEngineView.CopyLinkToClipboard, false)
    }
    MenuItem {
        id: openImage
        objectName: "feriaPageMenuOpenImage"
        text: "Open image in new tab"
        visible: pageMenu.hasImage
        onTriggered: pageMenu.openImageTab()
    }
    MenuItem {
        id: saveImage
        objectName: "feriaPageMenuSaveImage"
        text: "Save image"
        visible: pageMenu.hasImage
        onTriggered: pageMenu.invoke(WebEngineView.DownloadImageToDisk, false)
    }
    MenuItem {
        id: copyImage
        objectName: "feriaPageMenuCopyImageAddress"
        text: "Copy image address"
        visible: pageMenu.hasImage
        onTriggered: pageMenu.invoke(WebEngineView.CopyImageUrlToClipboard, false)
    }
    MenuItem {
        id: cutText
        objectName: "feriaPageMenuCut"
        text: "Cut"
        visible: pageMenu.isEditable
        enabled: (pageMenu.editFlags & ContextMenuRequest.CanCut) !== 0
        onTriggered: pageMenu.invoke(WebEngineView.Cut, true)
    }
    MenuItem {
        id: copyText
        objectName: "feriaPageMenuCopy"
        text: "Copy"
        visible: pageMenu.isEditable || pageMenu.hasSelection
        enabled: !pageMenu.isEditable
            || (pageMenu.editFlags & ContextMenuRequest.CanCopy) !== 0
        onTriggered: pageMenu.invoke(WebEngineView.Copy, pageMenu.isEditable)
    }
    MenuItem {
        id: pasteText
        objectName: "feriaPageMenuPaste"
        text: "Paste"
        visible: pageMenu.isEditable
        enabled: (pageMenu.editFlags & ContextMenuRequest.CanPaste) !== 0
        onTriggered: pageMenu.invoke(WebEngineView.Paste, true)
    }
    MenuItem {
        id: selectAll
        objectName: "feriaPageMenuSelectAll"
        text: "Select all"
        visible: pageMenu.isEditable
        enabled: (pageMenu.editFlags & ContextMenuRequest.CanSelectAll) !== 0
        onTriggered: pageMenu.invoke(WebEngineView.SelectAll, true)
    }
    MenuItem {
        id: back
        objectName: "feriaPageMenuBack"
        text: "Back"
        enabled: pageMenu.browser ? pageMenu.browser.navCanGoBack : false
        onTriggered: pageMenu.navigate("back")
    }
    MenuItem {
        id: forward
        objectName: "feriaPageMenuForward"
        text: "Forward"
        enabled: pageMenu.browser ? pageMenu.browser.navCanGoForward : false
        onTriggered: pageMenu.navigate("forward")
    }
    MenuItem {
        id: reload
        objectName: "feriaPageMenuReload"
        text: "Reload"
        enabled: pageMenu.browser && !pageMenu.browser.closingSession
        onTriggered: pageMenu.navigate("reload")
    }
}
