import QtQuick
import "Catalog.js" as Catalog
import "InteractionMath.js" as Picker
Item {
    id: root
    objectName: "tribute"
    width: 1440
    height: 900
    focus: true
    property var catalog: Catalog.items
    property int selectedIndex: -1
    property int lastIndex: 0
    property bool reducedMotion: false
    signal reducedMotionToggleRequested()
    property bool animating: false
    // The pointer grabs one specimen; simply hovering never changes selection.
    property int pressedIndex: -1
    property bool pressWillOpen: false
    property bool dragging: false
    property point pressPoint: Qt.point(0, 0)
    property var dragStart: null
    readonly property real dragThreshold: 6
    // Keep the lifted top edge inside its own projected rack lane.
    readonly property real maxHoverLift: .095
    property var progressValues: Array(catalog.length).fill(0)
    property var hoverLifts: Array(catalog.length).fill(0)
    property var dragOffsets: Array(catalog.length).fill(null)
    property var cameraMath: Picker.camera(width, height)
    property point lastPointer: Qt.point(-10000, -10000)
    readonly property bool windowActive: Window.active
    readonly property var pointerHit: physicalHit(lastPointer.x, lastPointer.y)
    readonly property real hoverCoordinate: pressedIndex < 0 && pointerHit.withinRail
                                             ? pointerHit.coordinate : -10
    onHoverCoordinateChanged: {
        if (!reducedMotion) animating = true
    }
    onReducedMotionChanged: {
        if (reducedMotion) advance(0)
        else animating = true
    }
    onActiveFocusChanged: {
        if (!activeFocus) losePointer()
    }
    onWindowActiveChanged: {
        if (!windowActive) losePointer()
    }
    signal watchRequested(var item)
    function choose(index) {
        if (index < -1 || index >= catalog.length) return
        selectedIndex = index
        if (index >= 0) lastIndex = index
        if (reducedMotion) {
            progressValues = catalog.map(function(_, i) { return i === index ? 1 : 0 })
            dragOffsets = catalog.map(function() { return null })
        }
        animating = !reducedMotion
    }
    function advance(dt) {
        var moving = false
        var elapsed = Picker.clamp(dt, 0.001, 0.08)
        var slideEase = 1 - Math.exp(-elapsed * 8.5)
        var hoverEase = 1 - Math.exp(-elapsed * 19)
        function approach(value, target, ease, tolerance) {
            if (reducedMotion || Math.abs(target - value) < tolerance) return target
            moving = true
            return Picker.mix(value, target, ease)
        }
        progressValues = progressValues.map(function(value, i) {
            if (dragging && i === pressedIndex) return value
            return approach(value, i === selectedIndex ? 1 : 0, slideEase, .001)
        })
        hoverLifts = hoverLifts.map(function(value, i) {
            // A continuous influence crosses the gaps without changing selection.
            var weight = i !== selectedIndex && progressValues[i] === 0
                       ? Picker.smooth(1 - Math.abs(i - hoverCoordinate) / 1.35) : 0
            return approach(value, reducedMotion ? 0 : maxHoverLift * weight, hoverEase, .0005)
        })
        dragOffsets = dragOffsets.map(function(offset, i) {
            if (!offset || (dragging && i === pressedIndex)) return offset
            var next = offset.map(function(value) { return approach(value, 0, slideEase, .001) })
            return next.some(function(value) { return value !== 0 }) ? next : null
        })
        animating = moving
    }
    function physicalHit(x, y) { return Picker.hitTest(x, y, cameraMath) }
    function overRaisedGlass(x, y) {
        var index = selectedIndex
        return index >= 0 && progressValues[index] > .25 &&
               Picker.inPolygon(Picker.corners(index, progressValues[index], cameraMath,
                                               hoverLifts[index], dragOffsets[index]), x, y)
    }
    function restingPick(x, y, lane) {
        var index = Picker.restingGlassHit(x, y, cameraMath, selectedIndex, hoverLifts)
        return index >= 0 ? index : lane.index !== selectedIndex ? lane.index : -1
    }
    function beginPick(x, y) {
        var hit = physicalHit(x, y)
        var index = restingPick(x, y, hit)
        pressPoint = Qt.point(x, y)
        // The rack remains interactive while a selected specimen crosses it.
        if (index >= 0) {
            pressedIndex = index
            choose(index)
        } else if (overRaisedGlass(x, y)) {
            pressedIndex = selectedIndex
            pressWillOpen = true
        } else if (!hit.withinRail && selectedIndex >= 0) {
            choose(-1)
        }
    }
    function moveGrab(x, y) {
        if (pressedIndex < 0 || pressedIndex !== selectedIndex) return
        var dx = x - pressPoint.x
        var dy = y - pressPoint.y
        if (!dragging) {
            if (Math.hypot(dx, dy) < dragThreshold) return
            var index = pressedIndex
            var start = Picker.glassCenter(index, progressValues[index], cameraMath,
                                           hoverLifts[index], dragOffsets[index])
            dragStart = {
                progress: progressValues[index],
                center: Picker.project(start, cameraMath),
                rest: Picker.project(Picker.glassCenter(index, 0, cameraMath, 0, null), cameraMath),
                groove: slotPoint(index),
                raised: Picker.project(Picker.glassCenter(index, 1, cameraMath, 0, null), cameraMath)
            }
            dragging = true
        }
        var gx = dragStart.groove.x - pressPoint.x
        var gy = dragStart.groove.y - pressPoint.y
        var targetX = dragStart.center.x + dx
        var targetY = dragStart.center.y + dy
        var progress
        if (pressWillOpen) {
            var distanceToGroove = Math.hypot(dragStart.groove.x - x, dragStart.groove.y - y)
            var towardGroove = Picker.clamp(1 - distanceToGroove / Math.max(1, Math.hypot(gx, gy)), 0, 1)
            progress = dragStart.progress * (1 - towardGroove)
            // Align with the original groove when approaching its center.
            targetX += towardGroove * (dragStart.rest.x - dragStart.center.x - gx)
            targetY += towardGroove * (dragStart.rest.y - dragStart.center.y - gy)
        } else {
            var travel = Math.hypot(dragStart.raised.x - dragStart.groove.x,
                                    dragStart.raised.y - dragStart.groove.y)
            progress = Picker.mix(dragStart.progress, 1, Picker.clamp(Math.hypot(dx, dy) / Math.max(1, travel), 0, 1))
        }
        var center = Picker.glassCenter(pressedIndex, progress, cameraMath, hoverLifts[pressedIndex], null)
        var values = progressValues.slice()
        var offsets = dragOffsets.slice()
        values[pressedIndex] = progress
        offsets[pressedIndex] = Picker.offsetToPoint(center, targetX, targetY, cameraMath)
        progressValues = values
        dragOffsets = offsets
    }
    function overDropTarget(index, x, y) {
        if (Picker.overBox(x, y, cameraMath)) return true
        var center = Picker.glassCenter(index, progressValues[index], cameraMath,
                                       hoverLifts[index], dragOffsets[index])
        var projected = Picker.project(center, cameraMath)
        return Picker.overBox(projected.x, projected.y, cameraMath)
    }
    function endPick(x, y) {
        moveGrab(x, y)
        var index = pressedIndex
        var hit = physicalHit(x, y)
        var returning = pressWillOpen && dragging && overDropTarget(index, x, y)
        var open = pressWillOpen && !dragging && index === selectedIndex &&
                   (!hit.withinRail || hit.index === index) && overRaisedGlass(x, y)
        if (returning) {
            lastPointer = Qt.point(-10000, -10000)
            choose(-1)
        }
        cancelPick()
        if (open) openStory(index)
    }
    function cancelPick() {
        if (dragging) animating = true
        pressedIndex = -1
        pressWillOpen = false
        dragging = false
        dragStart = null
        if (reducedMotion) advance(0)
    }
    function losePointer() {
        lastPointer = Qt.point(-10000, -10000)
        cancelPick()
    }
    function openStory(index) {
        if (index < 0 || index >= catalog.length) return
        var item = Catalog.theatreItem(index)
        if (!item) return
        watchRequested(item)
    }
    function keyboard(key) {
        if (key===Qt.Key_Right || key===Qt.Key_Left || key===Qt.Key_Home || key===Qt.Key_End) {
            var next = key===Qt.Key_Home ? 0 : key===Qt.Key_End ? catalog.length-1 : selectedIndex < 0 ? (key===Qt.Key_Left ? catalog.length-1 : 0) : Math.max(0, Math.min(catalog.length-1, selectedIndex+(key===Qt.Key_Right ? 1 : -1)))
            cancelPick()
            choose(next)
        } else if (key===Qt.Key_Return || key===Qt.Key_Enter) {
            if (selectedIndex < 0) choose(lastIndex); else openStory(selectedIndex)
        } else if (key===Qt.Key_Escape) {
            losePointer()
            choose(-1)
        } else if (key===Qt.Key_R) {
            reducedMotionToggleRequested()
        }
    }
    function slotPoint(index) { return Picker.slotPoint(index, cameraMath) }
    Keys.onPressed: function(event) {
        if (event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier)) return
        if ([Qt.Key_Right, Qt.Key_Left, Qt.Key_Home, Qt.Key_End,
             Qt.Key_Return, Qt.Key_Enter, Qt.Key_Escape, Qt.Key_R].indexOf(event.key) < 0) return
        keyboard(event.key)
        event.accepted = true
    }
    Rectangle { anchors.fill: parent; color: "#100e0c" }
    DexterScene {
        objectName: "nativeScene"
        anchors.fill: parent
        catalog: root.catalog
        progressValues: root.progressValues
        hoverLifts: root.hoverLifts
        dragOffsets: root.dragOffsets
        selectedIndex: root.selectedIndex
    }
    Image { anchors.fill: parent; source: "materials/vignette.png"; fillMode: Image.Stretch; smooth: true }
    MouseArea {
        id: input
        objectName: "pointerSurface"
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton
        cursorShape: root.dragging ? Qt.ClosedHandCursor :
                     root.pointerHit.withinRail || root.overRaisedGlass(root.lastPointer.x, root.lastPointer.y) ||
                     root.restingPick(root.lastPointer.x, root.lastPointer.y, root.pointerHit) >= 0
                     ? Qt.PointingHandCursor : Qt.ArrowCursor
        onPositionChanged: function(mouse) {
            if (mouse.source === Qt.MouseEventNotSynthesized)
                root.lastPointer = Qt.point(mouse.x, mouse.y)
            if (pressed) root.moveGrab(mouse.x, mouse.y)
        }
        onPressed: function(mouse) {
            root.forceActiveFocus()
            root.beginPick(mouse.x,mouse.y)
        }
        onReleased: function(mouse) { root.endPick(mouse.x,mouse.y) }
        onCanceled: root.losePointer()
        onExited: root.lastPointer = Qt.point(-10000,-10000)
        Accessible.role: Accessible.List
        Accessible.name: root.selectedIndex < 0 ? "Dexter collection, 13 glass specimens"
                                              : root.catalog[root.selectedIndex].title
        Accessible.description: "Click or drag a slide to lift it. Click raised glass to open in Theatre, or drop it over the box to return it. Arrow keys and Enter also work."
    }
    FrameAnimation { running: root.animating; onTriggered: root.advance(frameTime) }
}
