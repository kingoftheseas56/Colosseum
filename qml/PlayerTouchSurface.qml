import QtQuick

Item {
    id: root
    property bool seekEnabled: false
    property bool adjustmentsEnabled: false
    property bool fitEnabled: false
    property bool locked: false
    property bool modalOpen: false
    property real seekSeconds: 10
    property real volumeLevel: 100
    property real brightnessLevel: 0.5
    property real topExclusion: 32
    property real bottomExclusion: 32
    property real edgeExclusion: 24
    property string feedback: ""
    signal singleTapped()
    signal pressed()
    signal seekRequested(real seconds)
    signal volumeRequested(real level)
    signal brightnessRequested(real level)
    signal fitRequested(bool fill)
    signal unlockRequested()
    Theme { id: theme }

    function cancelPendingTap() { singleTap.stop() }
    function report(message) { feedback = message; feedbackTimer.restart() }
    onEnabledChanged: if (!enabled) singleTap.stop()
    onVisibleChanged: if (!visible) singleTap.stop()
    onLockedChanged: singleTap.stop()
    onModalOpenChanged: singleTap.stop()
    Timer {
        id: singleTap
        interval: Qt.styleHints.mouseDoubleClickInterval
        onTriggered: if (root.enabled && root.visible && !root.locked) root.singleTapped()
    }
    Timer { id: feedbackTimer; interval: 900; onTriggered: root.feedback = "" }

    MouseArea {
        id: touch
        objectName: "playerTouchGestureArea"
        anchors.fill: parent
        anchors.leftMargin: Math.max(root.edgeExclusion, SafeArea.margins.left)
        anchors.rightMargin: Math.max(root.edgeExclusion, SafeArea.margins.right)
        anchors.topMargin: Math.max(root.topExclusion, SafeArea.margins.top)
        anchors.bottomMargin: Math.max(root.bottomExclusion, SafeArea.margins.bottom)
        enabled: !root.locked
        property point startPoint
        property bool moved: false
        property string dragKind: ""
        property real initialVolume: 0
        property real initialBrightness: 0
        onPressed: function(mouse) {
            root.pressed()
            startPoint = Qt.point(mouse.x,mouse.y)
            moved = false; dragKind = ""
            initialVolume = root.volumeLevel; initialBrightness = root.brightnessLevel
        }
        onPositionChanged: function(mouse) {
            if (!pressed) return
            const dx = mouse.x - startPoint.x, dy = mouse.y - startPoint.y
            if (Math.hypot(dx,dy) > Qt.styleHints.startDragDistance) {
                moved = true; singleTap.stop()
            }
            if (!root.adjustmentsEnabled || root.modalOpen || pinch.active) return
            if (!dragKind.length && Math.abs(dy) > Math.max(24, Qt.styleHints.startDragDistance)
                    && Math.abs(dy) > Math.abs(dx) * 1.5)
                dragKind = startPoint.x < width / 2 ? "brightness" : "volume"
            if (!dragKind.length) return
            const amount = -dy / Math.max(1,height)
            if (dragKind === "volume") {
                const level = Math.max(0,Math.min(100,initialVolume + amount * 100))
                root.volumeRequested(level); root.report("Volume " + Math.round(level) + "%")
            } else {
                const level = Math.max(0.01,Math.min(1,initialBrightness + amount))
                root.brightnessRequested(level); root.report("Brightness " + Math.round(level * 100) + "%")
            }
        }
        onCanceled: { moved = true; singleTap.stop() }
        onClicked: if (!moved && !pinch.active) singleTap.restart()
        onDoubleClicked: function(mouse) {
            singleTap.stop()
            if (moved || root.modalOpen || !root.seekEnabled) return
            const local = mapToItem(root,mouse.x,mouse.y)
            if (local.x < root.width * 0.4 || local.x > root.width * 0.6) {
                const delta = local.x < root.width / 2 ? -root.seekSeconds : root.seekSeconds
                root.seekRequested(delta)
                root.report((delta < 0 ? "−" : "+") + Math.abs(delta) + " seconds")
            } else root.singleTapped()
        }
    }
    PinchHandler {
        id: pinch
        parent: touch
        target: null
        enabled: root.adjustmentsEnabled && root.fitEnabled && !root.locked && !root.modalOpen
        minimumPointCount: 2; maximumPointCount: 2
        property bool fired: false
        onActiveChanged: {
            if (active) { singleTap.stop(); fired = false }
        }
        onActiveScaleChanged: {
            if (!active || fired) return
            if (activeScale > 1.15 || activeScale < 0.85) {
                fired = true
                root.fitRequested(activeScale > 1)
                root.report(activeScale > 1 ? "Fill" : "Fit")
            }
        }
    }
    Rectangle {
        visible: root.feedback.length > 0 && !root.locked
        anchors.centerIn: parent
        width: message.implicitWidth + 32; height: 56; radius: 16
        color: "#cc171720"; border.width: 1; border.color: theme.gold
        Text {
            id: message; anchors.centerIn: parent; text: root.feedback
            color: theme.ink; font.family: theme.ui; font.pixelSize: theme.fontSize(18)
        }
    }
    Rectangle {
        objectName: "playerTouchUnlock"
        visible: root.locked
        anchors.centerIn: parent
        width: 148; height: 48; radius: 24; color: "#e6171720"
        border.width: 1; border.color: theme.gold
        Text { anchors.centerIn: parent; text: "Unlock controls"; color: theme.gold; font.family: theme.ui; font.pixelSize: theme.fontSize(15) }
        KeyboardAction { anchors.fill: parent; accessibleName: "Unlock player controls"; onTriggered: root.unlockRequested() }
    }
}
