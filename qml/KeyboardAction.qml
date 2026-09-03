// KeyboardAction — one semantic input face for Colosseum's hand-built controls.
// Atomic controls route pointer, keyboard and accessibility through `triggered()`.
// Complex controls may set pointerEnabled=false and keep specialized pointer handlers.
import QtQuick
import QtQuick.Window

Item {
    id: action

    property string accessibleName: ""
    property string accessibleDescription: ""
    property bool focusEnabled: visible && enabled
    property bool pointerEnabled: true
    property bool spaceActivates: true
    property bool contextEnabled: false
    property bool showFocusFrame: true
    readonly property bool televisionMode: {
        const w = action.Window.window
        return !!(w && w["televisionMode"] === true)
    }
    property real focusRadius: 10
    property real focusInset: televisionMode ? -5 : -2
    property real focusFrameWidth: televisionMode ? 4 : 2
    property color focusColor: televisionMode ? themeGoldFocus : Qt.rgba(240 / 255, 196 / 255, 74 / 255, 0.72)
    readonly property color themeGoldFocus: Qt.rgba(240 / 255, 196 / 255, 74 / 255, 1.0)
    property int cursorShape: Qt.PointingHandCursor

    readonly property bool hovered: hover.hovered
    readonly property bool pressed: primaryTap.pressed
    readonly property bool interactionActive: hovered || activeFocus

    signal triggered()
    signal contextRequested()

    focusPolicy: focusEnabled ? Qt.TabFocus : Qt.NoFocus

    function activate(focusReason) {
        if (!action.enabled || !action.visible)
            return
        if (action.focusEnabled && focusReason !== undefined)
            action.forceActiveFocus(focusReason)
        action.triggered()
    }
    function requestContext(focusReason) {
        if (!action.enabled || !action.visible || !action.contextEnabled)
            return
        if (action.focusEnabled && focusReason !== undefined)
            action.forceActiveFocus(focusReason)
        action.contextRequested()
    }

    Keys.onPressed: (event) => {
        if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Select
                || (action.spaceActivates && event.key === Qt.Key_Space)) {
            action.activate(Qt.ShortcutFocusReason)
            event.accepted = true
            return
        }
        const menuKey = event.key === Qt.Key_Menu
        const shiftF10 = event.key === Qt.Key_F10
            && (event.modifiers & Qt.ShiftModifier) !== 0
        if (action.contextEnabled && (menuKey || shiftF10)) {
            action.requestContext(Qt.ShortcutFocusReason)
            event.accepted = true
        }
    }

    HoverHandler {
        id: hover
        enabled: action.pointerEnabled && action.enabled
        cursorShape: enabled ? action.cursorShape : undefined
    }

    TapHandler {
        id: primaryTap
        enabled: action.pointerEnabled && action.enabled
        acceptedButtons: Qt.LeftButton
        gesturePolicy: TapHandler.ReleaseWithinBounds
        onTapped: action.activate(Qt.MouseFocusReason)
    }
    TapHandler {
        enabled: action.pointerEnabled && action.contextEnabled && action.enabled
        acceptedButtons: Qt.RightButton
        gesturePolicy: TapHandler.ReleaseWithinBounds
        onTapped: action.requestContext(Qt.MouseFocusReason)
    }

    Rectangle {
        anchors.fill: parent
        anchors.margins: action.focusInset
        radius: action.focusRadius
        visible: action.showFocusFrame && action.activeFocus
        color: "transparent"
        border.width: action.focusFrameWidth
        border.color: action.focusColor
        z: 10000
    }

    Accessible.role: Accessible.Button
    Accessible.name: action.accessibleName
    Accessible.description: action.accessibleDescription
    Accessible.focusable: action.focusEnabled
    Accessible.onPressAction: action.activate(Qt.OtherFocusReason)
}
