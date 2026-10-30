import QtQuick
import QtQuick.Controls.Basic
import "PhoneOverlayRegistry.js" as Overlays

Popup {
    id: sheet
    parent: Overlay.overlay
    property string title: ""
    property real maximumWidth: 720
    property var focusReturnItem: null
    readonly property var hostWindow: parent ? parent.Window.window : null
    property var safeInsets: parent ? parent.SafeArea.margins : ({left:0, top:0, right:0, bottom:0})
    property real keyboardInset: Qt.inputMethod.visible && parent
        ? Math.max(0, parent.height - Qt.inputMethod.keyboardRectangle.y) : 0
    default property alias body: bodyColumn.data
    readonly property real maximumHeight: parent
        ? Math.max(0, parent.height - safeInsets.top - keyboardInset) : 0

    modal: true
    dim: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    width: parent ? Math.min(maximumWidth, parent.width) : 0
    height: Math.min(maximumHeight, header.height + bodyColumn.implicitHeight + topPadding + bottomPadding)
    x: parent ? (parent.width - width) / 2 : 0
    y: parent ? parent.height - keyboardInset - height : 0
    leftPadding: 16 + safeInsets.left
    rightPadding: 16 + safeInsets.right
    topPadding: 8
    bottomPadding: 16 + (keyboardInset > 0 ? 0 : safeInsets.bottom)
    margins: 0
    enter: Transition {}
    exit: Transition {}

    onAboutToShow: focusReturnItem = Overlays.claim(sheet, hostWindow)
    onOpened: contentItem.forceActiveFocus(Qt.PopupFocusReason)
    onClosed: {
        Overlays.release(sheet)
        const target = focusReturnItem
        focusReturnItem = null
        Qt.callLater(function() {
            if (!Overlays.current(sheet.hostWindow) && target && target.visible && target.enabled)
                target.forceActiveFocus(Qt.PopupFocusReason)
        })
    }
    Component.onDestruction: Overlays.release(sheet)

    Theme { id: theme }
    Overlay.modal: Rectangle { color: "#99000000" }
    background: Rectangle {
        objectName: "phoneModalSheetBackground"
        color: "#17171f"
        radius: 18
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: parent.radius; color: parent.color }
    }
    contentItem: Column {
        Keys.onBackPressed: function(event) { sheet.close(); event.accepted = true }
        Keys.onEscapePressed: function(event) { sheet.close(); event.accepted = true }
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
            width: parent.width
            height: Math.max(0, sheet.availableHeight - header.height)
            contentWidth: width
            contentHeight: bodyColumn.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            Column { id: bodyColumn; width: parent.width }
            ScrollBar.vertical: ScrollBar {}
        }
    }
}
