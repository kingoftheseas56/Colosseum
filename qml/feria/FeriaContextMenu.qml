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
    // Group dividers use the app's hairline, not the style's bright default.
    palette.mid: theme.edge
    font.family: theme.ui
    Colosseum.Theme { id: theme }

    // Basic MenuItem's IconLabel reads palette.windowText, which did not
    // inherit the Menu's disabled palette group. Bind the real label instead,
    // retaining the style's original contentItem layout and plain text.
    Component.onCompleted: {
        for (const item of entries()) {
            if (!item.contentItem) continue
            const row = item
            item.contentItem.color = Qt.binding(function() {
                return row.enabled ? theme.ink : theme.inkDimmer
            })
        }
    }

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
    property real hitX: 0
    property real hitY: 0

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
        // ContextMenuRequest exposes a 'position' point, not x/y members.
        hitX = Math.max(0, Math.round(Number(request.position.x)))
        hitY = Math.max(0, Math.round(Number(request.position.y)))
        documentGeneration = browser.navigationGeneration
        contextActive = true
        ++contextSerial
        // popup() without coordinates uses the mouse cursor, which is wrong
        // for keyboard and programmatically requested context positions.
        const menuWidth = Math.max(pageMenu.width, pageMenu.implicitWidth)
        const menuHeight = Math.max(pageMenu.height, pageMenu.implicitHeight)
        const popupX = Math.max(0, Math.min(hitX, browser.width - menuWidth))
        const popupY = Math.max(0, Math.min(hitY, browser.height - menuHeight))
        popup(popupX, popupY)
    }
    function invoke(action) {
        if (!live()) return
        // Dispatch while QtWebEngine's native hit-test context remains current.
        // Edit commands do not run page scripts or guess an editable DOM target.
        contextActive = false
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
        height: visible ? implicitHeight : 0
        onTriggered: pageMenu.invoke(WebEngineView.OpenLinkInNewTab)
    }
    MenuItem {
        id: copyLink
        objectName: "feriaPageMenuCopyLinkAddress"
        text: "Copy link address"
        visible: pageMenu.hasLink
        height: visible ? implicitHeight : 0
        onTriggered: pageMenu.invoke(WebEngineView.CopyLinkToClipboard)
    }
    MenuSeparator {
        objectName: "feriaPageMenuLinkImageDivider"
        visible: pageMenu.hasLink && pageMenu.hasImage
        height: visible ? implicitHeight : 0
    }
    MenuItem {
        id: openImage
        objectName: "feriaPageMenuOpenImage"
        text: "Open image in new tab"
        visible: pageMenu.hasImage
        height: visible ? implicitHeight : 0
        onTriggered: pageMenu.openImageTab()
    }
    MenuItem {
        id: saveImage
        objectName: "feriaPageMenuSaveImage"
        text: "Save image"
        visible: pageMenu.hasImage
        height: visible ? implicitHeight : 0
        onTriggered: pageMenu.invoke(WebEngineView.DownloadImageToDisk)
    }
    MenuItem {
        id: copyImage
        objectName: "feriaPageMenuCopyImageAddress"
        text: "Copy image address"
        visible: pageMenu.hasImage
        height: visible ? implicitHeight : 0
        onTriggered: pageMenu.invoke(WebEngineView.CopyImageUrlToClipboard)
    }
    MenuSeparator {
        objectName: "feriaPageMenuBeforeEditDivider"
        visible: (pageMenu.isEditable || pageMenu.hasSelection)
            && (pageMenu.hasLink || pageMenu.hasImage)
        height: visible ? implicitHeight : 0
    }
    MenuItem {
        id: cutText
        objectName: "feriaPageMenuCut"
        text: "Cut"
        visible: pageMenu.isEditable
        height: visible ? implicitHeight : 0
        enabled: (pageMenu.editFlags & ContextMenuRequest.CanCut) !== 0
        onTriggered: pageMenu.invoke(WebEngineView.Cut)
    }
    MenuItem {
        id: copyText
        objectName: "feriaPageMenuCopy"
        text: "Copy"
        visible: pageMenu.isEditable || pageMenu.hasSelection
        height: visible ? implicitHeight : 0
        enabled: !pageMenu.isEditable
            || (pageMenu.editFlags & ContextMenuRequest.CanCopy) !== 0
        onTriggered: pageMenu.invoke(WebEngineView.Copy)
    }
    MenuItem {
        id: pasteText
        objectName: "feriaPageMenuPaste"
        text: "Paste"
        visible: pageMenu.isEditable
        height: visible ? implicitHeight : 0
        enabled: (pageMenu.editFlags & ContextMenuRequest.CanPaste) !== 0
        onTriggered: pageMenu.invoke(WebEngineView.Paste)
    }
    MenuItem {
        id: selectAll
        objectName: "feriaPageMenuSelectAll"
        text: "Select all"
        visible: pageMenu.isEditable
        height: visible ? implicitHeight : 0
        enabled: (pageMenu.editFlags & ContextMenuRequest.CanSelectAll) !== 0
        onTriggered: pageMenu.invoke(WebEngineView.SelectAll)
    }
    MenuSeparator {
        objectName: "feriaPageMenuBeforeNavigationDivider"
        visible: pageMenu.hasLink || pageMenu.hasImage
            || pageMenu.isEditable || pageMenu.hasSelection
        height: visible ? implicitHeight : 0
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
