import QtQuick

Item {
    id: root
    objectName: "accountKeyboardViewport"

    required property Item container
    required property Flickable flick
    property var inputMethod: Qt.inputMethod
    property var keyboardPlatform: typeof PlatformRuntime !== "undefined" ? PlatformRuntime : null
    property bool touchInput: Qt.platform.os === "android"
        || !!(container.Window.window && container.Window.window.androidHost)
    readonly property bool keyboardVisible: touchInput && container.visible
        && !!(inputMethod && inputMethod.visible)
    readonly property real keyboardOverlap: {
        if (!keyboardVisible) return 0
        const nativeTop = keyboardPlatform ? keyboardPlatform.dockedKeyboardTop : -1
        if (nativeTop >= 0) {
            const top = container.mapFromItem(null, 0, nativeTop).y
            return Math.min(container.height, Math.max(0, container.height - top))
        }
        const keyboard = inputMethod.keyboardRectangle
        if (!keyboard || keyboard.height <= 0) return 0
        const top = container.mapFromItem(null, 0, keyboard.y).y
        return Math.min(container.height, Math.max(0, container.height - top))
    }
    readonly property Item focusedTextControl: {
        const window = container.Window.window
        const focused = window ? window.activeFocusItem : null
        if (!touchInput || !container.visible || !focused || focused.cursorRectangle === undefined) return null
        let ancestor = focused
        while (ancestor && ancestor !== container) ancestor = ancestor.parent
        return ancestor === container ? focused : null
    }
    // Floating IMEs may supply no rectangle: keep room to lift the field near the top.
    readonly property real focusedScrollExtent: {
        if (!keyboardVisible || !focusedTextControl || keyboardOverlap > 0) return 0
        return focusedTextControl.mapToItem(flick.contentItem, 0, 0).y + flick.height + 16
    }
    function revealFocusedControl() {
        if (!touchInput || !container.visible || flick.height <= 0) return
        const window = container.Window.window
        const focused = window ? window.activeFocusItem : null
        if (!focused) return
        let ancestor = focused
        while (ancestor && ancestor !== container) ancestor = ancestor.parent
        if (ancestor !== container) return
        const position = focused.mapToItem(flick.contentItem, 0, 0)
        const imeUnreported = focused === focusedTextControl && keyboardVisible && keyboardOverlap <= 0
        let target = flick.contentY
        if (imeUnreported || position.y - 16 < target)
            target = position.y - 16
        else if (position.y + focused.height + 24 > target + flick.height)
            target = position.y + focused.height + 24 - flick.height
        flick.contentY = Math.max(0, Math.min(target, flick.contentHeight - flick.height))
    }
    onFocusedTextControlChanged: Qt.callLater(root.revealFocusedControl)
    onKeyboardOverlapChanged: Qt.callLater(root.revealFocusedControl)
    onKeyboardVisibleChanged: Qt.callLater(root.revealFocusedControl)
    Connections {
        target: root.container.Window.window
        ignoreUnknownSignals: true
        function onActiveFocusItemChanged() { Qt.callLater(root.revealFocusedControl) }
    }
    Connections {
        target: root.container
        function onVisibleChanged() { Qt.callLater(root.revealFocusedControl) }
        function onWidthChanged() { Qt.callLater(root.revealFocusedControl) }
        function onHeightChanged() { Qt.callLater(root.revealFocusedControl) }
    }
    Connections {
        target: root.flick
        function onWidthChanged() { Qt.callLater(root.revealFocusedControl) }
        function onHeightChanged() { Qt.callLater(root.revealFocusedControl) }
        function onContentHeightChanged() { Qt.callLater(root.revealFocusedControl) }
    }
    Connections {
        target: root.inputMethod
        ignoreUnknownSignals: true
        function onKeyboardRectangleChanged() { Qt.callLater(root.revealFocusedControl) }
    }
}
