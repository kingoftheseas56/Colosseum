// KeyboardSpatialNavigator — PlayStation-style spatial focus movement.
//
// The focused control remains the authority for keys it understands. Owners call
// handle() only for an arrow that bubbles out unaccepted; this navigator then picks
// the best visible focusable target in the requested visual direction. Direction
// alignment outranks raw distance, followed by lane overlap and primary/cross-axis
// distance. No candidate means "boundary": the key stays unaccepted for an outer
// owner to handle.
import QtQuick

Item {
    id: nav

    required property Item root
    property real minimumPrimaryDistance: 6
    property bool preserveEditableArrows: true

    signal boundaryRequested(int key, Item fromItem)

    visible: false
    width: 0
    height: 0

    function isDirectionalKey(key) {
        return key === Qt.Key_Up || key === Qt.Key_Down
            || key === Qt.Key_Left || key === Qt.Key_Right
    }

    function _isDescendant(item) {
        for (var node = item; node; node = node.parent) {
            if (node === nav.root)
                return true
        }
        return false
    }

    function _isEditable(item) {
        if (!item || !nav.preserveEditableArrows)
            return false
        return item.text !== undefined
            && item.cursorPosition !== undefined
            && (item.readOnly === undefined || item.readOnly === false)
    }

    function _centerVisibleThroughClips(item) {
        if (!item || !nav.root || !nav._isDescendant(item))
            return false

        var node = item
        while (node) {
            if (node.visible === false || node.enabled === false)
                return false
            if (node.opacity !== undefined && Number(node.opacity) <= 0.01)
                return false
            if (node === nav.root)
                break
            node = node.parent
        }
        if (node !== nav.root)
            return false

        if (Number(item.width) <= 0 || Number(item.height) <= 0)
            return false

        var center = item.mapToItem(nav.root, Number(item.width) / 2, Number(item.height) / 2)
        if (center.x < 0 || center.y < 0
                || center.x > Number(nav.root.width) || center.y > Number(nav.root.height))
            return false

        for (var ancestor = item.parent; ancestor && ancestor !== nav.root; ancestor = ancestor.parent) {
            if (ancestor.clip === true) {
                var clippedCenter = item.mapToItem(
                    ancestor, Number(item.width) / 2, Number(item.height) / 2)
                if (clippedCenter.x < 0 || clippedCenter.y < 0
                        || clippedCenter.x > Number(ancestor.width)
                        || clippedCenter.y > Number(ancestor.height))
                    return false
            }
        }
        return true
    }

    function _isFocusable(item) {
        if (!item || item === nav || item === nav.root || !nav._centerVisibleThroughClips(item))
            return false
        if (item.focusPolicy !== undefined)
            return item.focusPolicy !== Qt.NoFocus
        return item.activeFocusOnTab === true
    }

    function _appendFocusable(node, result) {
        if (!node || node === nav || node.visible === false || node.enabled === false)
            return 0

        var before = result.length
        var children = node.children || []
        for (var i = 0; i < children.length; i++)
            nav._appendFocusable(children[i], result)

        // Hand-built controls sometimes leave a legacy focusable wrapper around a
        // KeyboardAction. Prefer the deepest focusable face so one visual control is
        // one D-pad stop rather than two stacked stops at identical geometry.
        if (node !== nav.root && result.length === before && nav._isFocusable(node))
            result.push(node)
        return result.length - before
    }

    function focusableItems() {
        var result = []
        if (nav.root)
            nav._appendFocusable(nav.root, result)
        return result
    }

    function focusNamed(name, reason) {
        if (!name)
            return false
        var items = nav.focusableItems()
        for (var i = 0; i < items.length; i++) {
            if (items[i].objectName !== name)
                continue
            items[i].forceActiveFocus(reason === undefined ? Qt.TabFocusReason : reason)
            return items[i].activeFocus === true
        }
        return false
    }

    function _activeItem(node) {
        if (!node || node === nav)
            return null
        var children = node.children || []
        for (var i = 0; i < children.length; i++) {
            var childActive = nav._activeItem(children[i])
            if (childActive)
                return childActive
        }
        return node.activeFocus === true ? node : null
    }

    function activeItem() {
        return nav.root ? nav._activeItem(nav.root) : null
    }

    function _rect(item) {
        var p0 = item.mapToItem(nav.root, 0, 0)
        var p1 = item.mapToItem(nav.root, Number(item.width), Number(item.height))
        var left = Math.min(p0.x, p1.x)
        var right = Math.max(p0.x, p1.x)
        var top = Math.min(p0.y, p1.y)
        var bottom = Math.max(p0.y, p1.y)
        return {
            left: left, right: right, top: top, bottom: bottom,
            cx: (left + right) / 2, cy: (top + bottom) / 2
        }
    }

    function _metric(fromRect, candidateRect, key, stableIndex) {
        var primary = 0
        var cross = 0
        var laneOverlap = false

        if (key === Qt.Key_Down) {
            primary = candidateRect.cy - fromRect.cy
            cross = Math.abs(candidateRect.cx - fromRect.cx)
            laneOverlap = candidateRect.right >= fromRect.left
                && candidateRect.left <= fromRect.right
        } else if (key === Qt.Key_Up) {
            primary = fromRect.cy - candidateRect.cy
            cross = Math.abs(candidateRect.cx - fromRect.cx)
            laneOverlap = candidateRect.right >= fromRect.left
                && candidateRect.left <= fromRect.right
        } else if (key === Qt.Key_Right) {
            primary = candidateRect.cx - fromRect.cx
            cross = Math.abs(candidateRect.cy - fromRect.cy)
            laneOverlap = candidateRect.bottom >= fromRect.top
                && candidateRect.top <= fromRect.bottom
        } else if (key === Qt.Key_Left) {
            primary = fromRect.cx - candidateRect.cx
            cross = Math.abs(candidateRect.cy - fromRect.cy)
            laneOverlap = candidateRect.bottom >= fromRect.top
                && candidateRect.top <= fromRect.bottom
        }

        if (primary < nav.minimumPrimaryDistance)
            return null
        return {
            alignment: cross / Math.max(1, primary),
            lanePenalty: laneOverlap ? 0 : 1,
            primary: primary,
            cross: cross,
            stableIndex: stableIndex
        }
    }

    function _metricBefore(a, b) {
        if (!b)
            return true
        var epsilon = 0.000001
        if (Math.abs(a.alignment - b.alignment) > epsilon)
            return a.alignment < b.alignment
        if (a.lanePenalty !== b.lanePenalty)
            return a.lanePenalty < b.lanePenalty
        if (Math.abs(a.primary - b.primary) > epsilon)
            return a.primary < b.primary
        if (Math.abs(a.cross - b.cross) > epsilon)
            return a.cross < b.cross
        return a.stableIndex < b.stableIndex
    }

    function targetFrom(fromItem, key) {
        if (!fromItem || !nav.root || !nav.isDirectionalKey(key))
            return null
        var items = nav.focusableItems()
        var fromRect = nav._rect(fromItem)
        var bestItem = null
        var bestMetric = null
        for (var i = 0; i < items.length; i++) {
            var candidate = items[i]
            if (candidate === fromItem)
                continue
            var metric = nav._metric(fromRect, nav._rect(candidate), key, i)
            if (metric && nav._metricBefore(metric, bestMetric)) {
                bestMetric = metric
                bestItem = candidate
            }
        }
        return bestItem
    }

    function moveFrom(fromItem, key) {
        if (!nav.isDirectionalKey(key) || !fromItem || nav._isEditable(fromItem))
            return false
        var target = nav.targetFrom(fromItem, key)
        if (!target) {
            nav.boundaryRequested(key, fromItem)
            return false
        }
        var reason = (key === Qt.Key_Up || key === Qt.Key_Left)
            ? Qt.BacktabFocusReason : Qt.TabFocusReason
        target.forceActiveFocus(reason)
        return target.activeFocus === true
    }

    function move(key) {
        return nav.moveFrom(nav.activeItem(), key)
    }

    function handle(event) {
        if (!event || !nav.isDirectionalKey(event.key))
            return false
        if (!nav.move(event.key))
            return false
        event.accepted = true
        return true
    }
}
