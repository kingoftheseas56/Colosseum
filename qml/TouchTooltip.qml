import QtQuick
import QtQml

// Shares Android long-press state with MouseAreas, AbstractButtons, and existing TapHandlers.
Item {
    id: root
    width: 0
    height: 0
    visible: false

    property MouseArea target: null
    property var buttonTarget: null
    property Item owner: null
    property bool tapOwner: false
    property bool longPressTooltipEnabled: Qt.platform.os === "android"
    property real holdThreshold: 0.4
    readonly property bool active: root._active
    readonly property Item ownerItem: root.target !== null ? root.target
        : root.buttonTarget !== null ? root.buttonTarget : root.owner
    readonly property bool ownerUsable: root.ownerItem !== null
        && root.ownerItem.enabled && root.ownerItem.visible

    property bool _active: false

    function reset() {
        releaseTimer.stop()
        root._active = false
    }

    function pressStarted() {
        releaseTimer.stop()
        root._active = false
    }

    function longPressRecognized() {
        if (!root.longPressTooltipEnabled || !root.ownerUsable)
            return
        releaseTimer.stop()
        root._active = true
    }

    function pressReleased() {
        if (root._active)
            releaseTimer.restart()
        else
            root.reset()
    }

    function pressCanceled() {
        root.reset()
    }

    onTargetChanged: reset()
    onButtonTargetChanged: reset()
    onOwnerChanged: reset()
    onTapOwnerChanged: if (!tapOwner) reset()
    onLongPressTooltipEnabledChanged: if (!longPressTooltipEnabled) reset()
    onOwnerUsableChanged: if (!ownerUsable) reset()

    Binding {
        target: root.target
        property: "pressAndHoldInterval"
        value: 400
        when: root.longPressTooltipEnabled && root.ownerUsable
    }

    TapHandler {
        id: buttonPointer
        parent: root.buttonTarget !== null ? root.buttonTarget : root
        enabled: root.buttonTarget !== null
            && root.longPressTooltipEnabled && root.ownerUsable
        acceptedButtons: Qt.LeftButton
        gesturePolicy: TapHandler.ReleaseWithinBounds
        longPressThreshold: root.holdThreshold

        onPressedChanged: {
            if (pressed) {
                root.pressStarted()
                const button = root.buttonTarget
                if (button && (button.focusPolicy & Qt.ClickFocus) !== 0)
                    button.forceActiveFocus(Qt.MouseFocusReason)
            } else {
                root.pressReleased()
            }
        }
        onLongPressed: root.longPressRecognized()
        onCanceled: root.pressCanceled()
        onTapped: {
            const button = root.buttonTarget
            Qt.callLater(function() {
                if (button && root.buttonTarget === button
                        && root.longPressTooltipEnabled && root.ownerUsable
                        && button.enabled && button.visible)
                    button.click()
            })
        }
    }

    Binding {
        target: root.buttonTarget
        property: "down"
        value: true
        when: buttonPointer.enabled && buttonPointer.pressed
    }

    TapHandler {
        id: passiveOwnerPointer
        parent: root.owner !== null ? root.owner : root
        enabled: root.tapOwner && root.owner !== null
            && root.longPressTooltipEnabled && root.ownerUsable
        acceptedButtons: Qt.LeftButton
        gesturePolicy: TapHandler.DragThreshold
        longPressThreshold: root.holdThreshold
        onPressedChanged: {
            if (pressed)
                root.pressStarted()
            else
                root.pressReleased()
        }
        onLongPressed: root.longPressRecognized()
        onCanceled: root.pressCanceled()
    }

    Connections {
        // A null target on desktop preserves the MouseArea's original hold/click semantics.
        target: root.longPressTooltipEnabled && root.ownerUsable ? root.target : null

        function onPressed(mouse) { root.pressStarted() }
        function onPressAndHold(mouse) { root.longPressRecognized() }
        function onReleased(mouse) { root.pressReleased() }
        function onCanceled(mouse) { root.pressCanceled() }
    }

    Timer {
        id: releaseTimer
        interval: 1500
        onTriggered: root._active = false
    }
}
