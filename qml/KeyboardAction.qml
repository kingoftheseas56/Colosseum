// KeyboardAction — one semantic input face for Colosseum's hand-built controls.
// Atomic controls route pointer, keyboard and accessibility through `triggered()`.
// Complex controls may set pointerEnabled=false and keep specialized pointer handlers.
import QtQuick
import QtQuick.Controls
import QtQuick.Window

Item {
    id: action

    property string accessibleName: ""
    property string accessibleDescription: ""
    property string tooltipText: ""
    // Legacy is intentional: world slices opt in only after their layout reserves caption space.
    // "caption" / "drop" / "longPress" affect Android only; desktop hover is untouched.
    property string androidTooltipPolicy: "legacy"
    property string androidCaptionText: ""
    property string androidCaptionPlacement: "below" // "below" | "right"
    property bool androidPresentationEnabled: Qt.platform.os === "android" // test-injectable platform seam
    readonly property string androidEffectiveCaption: androidCaptionText.length > 0 ? androidCaptionText : tooltipText
    readonly property bool androidCaptionVisible: androidPresentationEnabled
        && androidTooltipPolicy === "caption" && androidEffectiveCaption.length > 0
    property bool _longPressTooltip: false
    property bool tooltipHovered: action.hovered
    property bool focusEnabled: visible && enabled
    property bool pointerEnabled: true
    property bool focusOnPointer: true
    // PlayStation-style map: Enter is the universal primary action. Space remains
    // opt-in for controls whose native/scoped convention actually owns it.
    property bool spaceActivates: false
    property bool contextEnabled: false
    property QtObject command: null
    property bool showFocusFrame: true
    readonly property bool televisionMode: {
        const w = action.Window.window
        return !!(w && w["televisionMode"] === true)
    }
    property real focusRadius: 10
    property real focusInset: -2
    // Retained for callers; the ring is always FocusRing's gold (one look everywhere).
    property color focusColor: Qt.rgba(240 / 255, 196 / 255, 74 / 255, 0.28)
    // Gold buttons (Watch / Read): FocusRing's dark gap keeps the gold ring visible.
    property bool focusPrimary: false
    property int cursorShape: Qt.PointingHandCursor

    readonly property bool hovered: hover.hovered
    readonly property bool pressed: primaryTap.pressed
    readonly property bool interactionActive: hovered || pressed || activeFocus

    readonly property bool tooltipVisibilityRequested: action.visible && action.enabled
        && action.tooltipText.length > 0
        && (!action.androidPresentationEnabled
            ? action.tooltipHovered
            : (action.androidTooltipPolicy === "legacy" && action.tooltipHovered)
              || (action.androidTooltipPolicy === "longPress" && action._longPressTooltip))
    ToolTip.delay: 350
    ToolTip.text: action.tooltipText
    ToolTip.visible: action.tooltipVisibilityRequested
    onAndroidTooltipPolicyChanged: action._longPressTooltip = false

    signal triggered()
    signal contextRequested()

    // Some visible delegates are clipped offscreen or are pointer-only faces.
    // Their explicit focusEnabled contract must also govern the native Tab chain.
    // Qt cannot revoke Tab focus on the active item. Keep it until focus moves,
    // then re-evaluate so an excluded face cannot re-enter the next traversal.
    focusPolicy: (action.focusEnabled || action.activeFocus) ? Qt.TabFocus : Qt.NoFocus

    function activate(focusReason) {
        if (!action.enabled || !action.visible)
            return false
        if (action.focusEnabled && focusReason !== undefined
                && (focusReason !== Qt.MouseFocusReason || action.focusOnPointer))
            action.forceActiveFocus(focusReason)
        if (action.command && action.command.invoke)
            return action.command.invoke(action)
        action.triggered()
        return true
    }
    function requestContext(focusReason) {
        if (!action.enabled || !action.visible || !action.contextEnabled)
            return
        if (action.focusEnabled && focusReason !== undefined)
            action.forceActiveFocus(focusReason)
        action.contextRequested()
        return true
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
        onLongPressed: {
            if (action.androidPresentationEnabled && action.androidTooltipPolicy === "longPress"
                    && action.tooltipText.length > 0) {
                action._longPressTooltip = true
                longPressTimeout.restart()
            }
        }
    }
    TapHandler {
        enabled: action.pointerEnabled && action.contextEnabled && action.enabled
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        acceptedButtons: Qt.RightButton
        gesturePolicy: TapHandler.ReleaseWithinBounds
        onTapped: action.requestContext(Qt.MouseFocusReason)
    }

    Timer {
        id: longPressTimeout
        interval: 2000
        onTriggered: action._longPressTooltip = false
    }

    // Opted-in captions load their typography lazily. Hundreds of legacy controls
    // must not instantiate Theme's FontLoaders just to render no caption.
    // The owning world control reserves space outside this input overlay.
    Loader {
        active: action.androidCaptionVisible
        anchors.top: action.androidCaptionPlacement === "below" ? parent.bottom : undefined
        anchors.topMargin: action.androidCaptionPlacement === "below" ? 4 : 0
        anchors.left: action.androidCaptionPlacement === "right" ? parent.right : parent.left
        anchors.leftMargin: action.androidCaptionPlacement === "right" ? 8 : 0
        anchors.verticalCenter: action.androidCaptionPlacement === "right" ? parent.verticalCenter : undefined
        width: action.androidCaptionPlacement === "right" ? implicitWidth : action.width
        sourceComponent: Text {
            objectName: "androidActionCaption"
            Theme { id: captionTheme }
            text: action.androidEffectiveCaption
            textFormat: Text.PlainText
            color: captionTheme.inkDim
            font.family: captionTheme.ui
            font.pixelSize: captionTheme.fontSize(12)
            elide: Text.ElideRight
            wrapMode: Text.NoWrap
            horizontalAlignment: action.androidCaptionPlacement === "right"
                ? Text.AlignLeft : Text.AlignHCenter
        }
    }

    // Colosseum's one focus look (world-feel R1): a solid gold ring outside the control.
    FocusRing {
        anchors.fill: parent
        anchors.margins: action.focusInset + 2
        radius: action.focusRadius
        primary: action.focusPrimary
        shown: action.showFocusFrame && action.activeFocus
    }

    Accessible.role: Accessible.Button
    Accessible.name: action.accessibleName
    Accessible.description: action.accessibleDescription
    Accessible.focusable: action.focusEnabled
    Accessible.onPressAction: action.activate(Qt.OtherFocusReason)
}
