import QtQuick
import QtQuick.Controls.Basic
import "PhoneOverlayRegistry.js" as Overlays
import "ViewportAvoidance.js" as Viewport

Popup {
    id: sheet
    parent: Overlay.overlay
    property string title: ""
    property real maximumWidth: 720
    property var focusReturnItem: null
    property Item preferredFocusReturn: null
    property Item hostedItem:null
    property Item _presentedItem:null
    property bool returnFocusOnClose:true
    property var dismissHandler:null
    property var inputMethod:Qt.inputMethod
    readonly property alias bodyViewport:bodyScroll
    readonly property var hostWindow: parent ? parent.Window.window : null
    property var safeInsets: parent ? parent.SafeArea.margins : ({left:0, top:0, right:0, bottom:0})
    property real keyboardInset: inputMethod && inputMethod.visible && parent
        ? Math.max(0, parent.height - inputMethod.keyboardRectangle.y) : 0
    default property alias body: bodyColumn.data
    property rect usableRect: {
        if(!parent)return Qt.rect(0,0,0,0)
        const keyboard=inputMethod && inputMethod.visible && inputMethod.keyboardRectangle
            ? inputMethod.keyboardRectangle : Qt.rect(0,parent.height-keyboardInset,parent.width,keyboardInset)
        const origin=parent.mapFromItem(null,keyboard.x,keyboard.y)
        const box=Viewport.available(parent.width,parent.height,{left:0,top:safeInsets.top,right:0,bottom:0},
            !!(inputMethod && inputMethod.visible) || keyboardInset>0,
            {x:origin.x,y:origin.y,width:keyboard.width,height:keyboard.height},0)
        return Qt.rect(box.x,box.y,box.width,box.height)
    }
    readonly property real maximumHeight:usableRect.height

    modal: true
    dim: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    width:Math.min(maximumWidth,usableRect.width)
    height: Math.min(maximumHeight, header.height + bodyColumn.implicitHeight + topPadding + bottomPadding)
    x:usableRect.x+(usableRect.width-width)/2
    y:usableRect.y+usableRect.height-height
    leftPadding: 16 + safeInsets.left
    rightPadding: 16 + safeInsets.right
    topPadding: 8
    bottomPadding: 16 + (keyboardInset > 0 ? 0 : safeInsets.bottom)
    margins: 0
    enter: Transition {}
    exit: Transition {}
    function presentHostedItem() {
        bodyColumn.state=""
        _presentedItem=hostedItem
        if(visible && _presentedItem)bodyColumn.state="hosted"
    }
    onHostedItemChanged:presentHostedItem()
    function requestDismiss() {
        if(inputMethod && inputMethod.visible && typeof inputMethod.hide==="function") { inputMethod.hide();return }
        if(dismissHandler && dismissHandler()===true)return
        close()
    }
    function reveal(item) {
        if(!visible || !item)return
        let ancestor=item
        while(ancestor && ancestor!==bodyColumn)ancestor=ancestor.parent
        if(!ancestor)return
        const top=item.mapToItem(bodyColumn,0,0).y
        const wanted=top<bodyScroll.contentY ? top : top+item.height>bodyScroll.contentY+bodyScroll.height ? top+item.height-bodyScroll.height : bodyScroll.contentY
        bodyScroll.contentY=Math.max(0,Math.min(wanted,bodyScroll.contentHeight-bodyScroll.height))
    }
    onKeyboardInsetChanged:Qt.callLater(function(){sheet.reveal(hostWindow ? hostWindow.activeFocusItem : null)})
    onUsableRectChanged:Qt.callLater(function(){sheet.reveal(hostWindow ? hostWindow.activeFocusItem : null)})
    Connections {
        target:sheet.hostWindow
        function onActiveFocusItemChanged() { if(sheet.visible)Qt.callLater(function(){sheet.reveal(sheet.hostWindow.activeFocusItem)}) }
    }

    onAboutToShow: {
        const replacing=Overlays.current(hostWindow) !== null
        const previous=Overlays.claim(sheet, hostWindow)
        focusReturnItem=replacing ? previous : preferredFocusReturn || previous
        presentHostedItem()
    }
    onOpened: contentItem.forceActiveFocus(Qt.PopupFocusReason)
    onClosed: {
        bodyColumn.state=""
        Overlays.release(sheet)
        const target = returnFocusOnClose ? focusReturnItem : null
        const window = hostWindow
        focusReturnItem = null
        Qt.callLater(function() {
            if (!Overlays.current(window) && target && target.visible && target.enabled)
                target.forceActiveFocus(Qt.PopupFocusReason)
        })
    }
    Component.onDestruction: Overlays.release(sheet)

    Theme { id: theme }
    Shortcut { sequence:"Back"; enabled:sheet.visible; onActivated:sheet.requestDismiss() }
    Overlay.modal: Rectangle { color: "#99000000" }
    background: Rectangle {
        objectName: "phoneModalSheetBackground"
        color: "#17171f"
        radius: 18
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: parent.radius; color: parent.color }
    }
    contentItem: Column {
        Keys.onBackPressed: function(event) { sheet.requestDismiss(); event.accepted = true }
        Keys.onEscapePressed: function(event) { sheet.requestDismiss(); event.accepted = true }
        Item {
            id: header
            width: parent.width
            height: 48
            Text {
                anchors.left: parent.left
                anchors.right: closeTarget.left
                anchors.verticalCenter: parent.verticalCenter
                text: sheet.title
                elide: Text.ElideRight
                color: theme.ink
                font.family: theme.ui
                font.pixelSize: theme.fontSize(18)
            }
            TouchTarget {
                id: closeTarget
                anchors.right: parent.right
                touch: true
                accessibleName: qsTr("Close")
                onTriggered: sheet.close()
                Text { anchors.centerIn: parent; text: "×"; color: theme.ink; font.pixelSize: 24 }
            }
        }
        Flickable {
            id:bodyScroll
            width: parent.width
            height: Math.max(0, sheet.availableHeight - header.height)
            contentWidth: width
            contentHeight: bodyColumn.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            Column {
                id: bodyColumn
                width: parent.width
                states:State {
                    name:"hosted"
                    ParentChange { target:sheet._presentedItem; parent:bodyColumn; x:0; y:0; width:bodyColumn.width; height:sheet._presentedItem ? sheet._presentedItem.implicitHeight : 0 }
                    AnchorChanges { target:sheet._presentedItem; anchors.left:undefined; anchors.right:undefined; anchors.top:undefined; anchors.bottom:undefined; anchors.horizontalCenter:undefined; anchors.verticalCenter:undefined }
                }
            }
            ScrollBar.vertical: ScrollBar {}
        }
    }
}
