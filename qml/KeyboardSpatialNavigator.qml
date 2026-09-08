// KeyboardSpatialNavigator — PlayStation-style spatial focus movement.
//
// The focused control remains the authority for keys it understands. Owners call
// handle() only for an arrow that bubbles out unaccepted; this navigator then picks
// the best visible focusable target in the requested visual direction. Direction
// alignment outranks raw distance, followed by lane overlap and primary/cross-axis
// distance. No candidate means "boundary": the key stays unaccepted for an outer
// owner to handle.
import QtQuick
import "KeyboardViewport.js" as Viewport

Item {
    id: nav

    required property Item root
    property real minimumPrimaryDistance: 6
    property bool preserveEditableArrows: true
    property real scrollStep: 72
    // Platform key repeat is the only repeat source.  A realization callback may
    // retain one pending landing, but it must belong to the current input
    // generation before it can focus anything.
    property int navigationGeneration: 0
    property int activeNavigationKey: -1
    property bool navigationActive: false
    property var pendingNavigation: null

    signal boundaryRequested(int key, Item fromItem)
    signal navigationCancelled(string reason)

    visible: false
    width: 0
    height: 0

    Connections {
        target: nav.root
        function onActiveFocusChanged() {
            if (!nav.root || !nav.root.activeFocus)
                nav.cancelNavigation("focus")
        }
        function onVisibleChanged() {
            if (!nav.root || !nav.root.visible)
                nav.cancelNavigation("route")
        }
        function onEnabledChanged() {
            if (!nav.root || !nav.root.enabled)
                nav.cancelNavigation("route")
        }
    }

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

    function _eligible(item) {
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

        return true
    }

    function _centerVisibleThroughClips(item) {
        if (!nav._eligible(item))
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

    function _isFocusable(item, includeOffscreen) {
        if (!item || item === nav || item === nav.root
                || !(includeOffscreen ? nav._eligible(item) : nav._centerVisibleThroughClips(item)))
            return false
        if (item.focusPolicy !== undefined)
            return item.focusPolicy !== Qt.NoFocus
        return item.activeFocusOnTab === true
    }

    function _appendFocusable(node, result, includeOffscreen) {
        if (!node || node === nav || node.visible === false || node.enabled === false)
            return 0

        var before = result.length
        var children = node.children || []
        for (var i = 0; i < children.length; i++)
            nav._appendFocusable(children[i], result, includeOffscreen)

        // Hand-built controls sometimes leave a legacy focusable wrapper around a
        // KeyboardAction. Prefer the deepest focusable face so one visual control is
        // one D-pad stop rather than two stacked stops at identical geometry.
        if (node !== nav.root && result.length === before && nav._isFocusable(node, includeOffscreen))
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

    function beginNavigation(key) {
        if (!nav.isDirectionalKey(key))
            return nav.navigationGeneration
        if (nav.navigationActive && nav.activeNavigationKey !== key)
            nav.cancelNavigation("direction")
        nav.navigationActive = true
        nav.activeNavigationKey = key
        return nav.navigationGeneration
    }

    function isNavigationGenerationCurrent(generation, key) {
        if (!nav.navigationActive || Number(generation) !== nav.navigationGeneration)
            return false
        return key === undefined || key === null || nav.activeNavigationKey === key
    }

    function cancelNavigation(reason) {
        nav.navigationGeneration += 1
        nav.pendingNavigation = null
        nav.navigationActive = false
        nav.activeNavigationKey = -1
        nav.navigationCancelled(reason || "cancelled")
    }

    // Keep a single deferred landing slot.  This is intentionally not a queue:
    // every newer key replaces the prior intent, and release/focus/route loss
    // invalidates the generation before a realization callback can land.
    function deferLanding(target, key, reason, generation) {
        var token = generation === undefined ? nav.beginNavigation(key) : Number(generation)
        if (!nav.isNavigationGenerationCurrent(token, key) || !target)
            return false
        nav.pendingNavigation = { target: target, key: key,
                                  reason: reason === undefined ? Qt.OtherFocusReason : reason,
                                  generation: token }
        return true
    }

    function settlePendingLanding() {
        var pending = nav.pendingNavigation
        nav.pendingNavigation = null
        if (!pending || !nav.isNavigationGenerationCurrent(pending.generation, pending.key))
            return false
        var owner = nav._flickableOwner(pending.target)
        var controller = owner ? nav._scrollControllerFor(owner) : null
        var horizontal = pending.key === Qt.Key_Left || pending.key === Qt.Key_Right
        var budget = owner ? nav._directionalStep(owner, horizontal, controller)
                           : nav.scrollStep
        if (controller && controller.arrowScrolling === false)
            return false
        return nav._land(pending.target, pending.key, pending.reason, budget)
    }

    function handleRelease(event) {
        if (!event || !nav.isDirectionalKey(event.key))
            return false
        if (nav.navigationActive && nav.activeNavigationKey === event.key)
            nav.cancelNavigation("release")
        return true
    }

    function _scrollControllerFor(flick) {
        if (!nav.root || !flick)
            return null
        var registered = Viewport.controllerFor(flick)
        if (registered)
            return registered
        var pending = [nav.root]
        while (pending.length > 0) {
            var node = pending.shift()
            if (node !== nav && node.flick !== undefined && node.flick === flick
                    && node.lineStep !== undefined && node.arrowScrolling !== undefined)
                return node
            var children = node.children || []
            for (var i = 0; i < children.length; ++i)
                pending.push(children[i])
        }
        return null
    }

    function _flickableOwner(item) {
        for (var node = item; node; node = node.parent) {
            if (Viewport.isFlickable(node))
                return node
            if (node === nav.root)
                break
        }
        return null
    }

    function _directionalStep(flick, horizontal, controller) {
        var extent = horizontal ? Number(flick.width) : Number(flick.height)
        var configured = controller && controller.lineStep !== undefined
            ? Number(controller.lineStep) : Number(nav.scrollStep)
        if (!isFinite(extent) || !isFinite(configured) || extent <= 0 || configured <= 0)
            return 0
        return Math.min(configured, extent * 0.25)
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

    function targetFrom(fromItem, key, scope, includeOffscreen) {
        if (!fromItem || !nav.root || !nav.isDirectionalKey(key))
            return null
        var items = []
        nav._appendFocusable(scope || nav.root, items, includeOffscreen === true)
        var fromRect = nav._rect(fromItem)
        // A viewport holding pure-scroll focus enters from the opposite edge,
        // rather than selecting relative to its large centre rectangle.
        if (Viewport.isFlickable(fromItem)) {
            if (key === Qt.Key_Down) fromRect.cy = fromRect.top
            if (key === Qt.Key_Up) fromRect.cy = fromRect.bottom
            if (key === Qt.Key_Right) fromRect.cx = fromRect.left
            if (key === Qt.Key_Left) fromRect.cx = fromRect.right
        }
        var bestItem = null
        var bestMetric = null
        for (var i = 0; i < items.length; i++) {
            var candidate = items[i]
            if (candidate === fromItem)
                continue
            if (includeOffscreen && Viewport.revealPlan(candidate, nav.root,
                    key === Qt.Key_Left || key === Qt.Key_Right) === null)
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
        var horizontal = key === Qt.Key_Left || key === Qt.Key_Right
        var forward = key === Qt.Key_Down || key === Qt.Key_Right
        var reason = (key === Qt.Key_Up || key === Qt.Key_Left)
            ? Qt.BacktabFocusReason : Qt.TabFocusReason
        var ownedViewport = false
        var blockedViewport = false
        // Exhaust each owning viewport before exporting into unrelated chrome.
        // Indexed collections still consume their arrows before this handler.
        for (var owner = fromItem; owner; owner = owner.parent) {
            if (Viewport.isFlickable(owner) && Viewport.contains(nav.root, owner)) {
                ownedViewport = true
                var controller = nav._scrollControllerFor(owner)
                if (controller && controller.arrowScrolling === false) {
                    blockedViewport = true
                    continue
                }
                var step = nav._directionalStep(owner, horizontal, controller)
                var local = nav.targetFrom(fromItem, key, owner.contentItem, false)
                if (!local)
                    local = nav.targetFrom(fromItem, key, owner.contentItem, true)
                if (local && nav._land(local, key, reason, step))
                    return true
                if (step > 0 && Viewport.setPosition(owner, horizontal,
                        Viewport.position(owner, horizontal)
                        + ((key === Qt.Key_Up || key === Qt.Key_Left) ? -step : step))) {
                    // The step is the complete budget for this event. A target that
                    // becomes visible after the write lands on the next key, avoiding
                    // a second reveal in the same transition.
                    if (!nav._centerVisibleThroughClips(fromItem))
                        owner.forceActiveFocus(reason)
                    return true
                }
            }
            if (owner === nav.root)
                break
        }
        if (blockedViewport)
            return false
        var target = nav.targetFrom(fromItem, key)
        if (!target)
            target = nav.targetFrom(fromItem, key, nav.root, true)
        if (!target) {
            nav.boundaryRequested(key, fromItem)
            return false
        }
        var targetOwner = nav._flickableOwner(target)
        if (targetOwner && Viewport.contains(nav.root, targetOwner)) {
            var targetController = nav._scrollControllerFor(targetOwner)
            if (targetController && targetController.arrowScrolling === false)
                return false
            var targetStep = nav._directionalStep(targetOwner, horizontal, targetController)
            if (!nav._land(target, key, reason, targetStep)) {
                if (targetStep > 0 && Viewport.setPosition(targetOwner, horizontal,
                        Viewport.position(targetOwner, horizontal)
                        + (forward ? targetStep : -targetStep))) {
                    targetOwner.forceActiveFocus(reason)
                    return true
                }
                return false
            }
            return true
        }
        // A content viewport owns an exhausted directional boundary. Only an
        // explicit owner transition may export into unrelated chrome; the
        // default surface has no such transition.
        if (ownedViewport && forward)
            return false
        return nav._land(target, key, reason, nav.scrollStep)
    }

    function _land(target, key, reason, maxDistance) {
        var horizontal = key === Qt.Key_Left || key === Qt.Key_Right
        if (maxDistance === undefined || !isFinite(Number(maxDistance)))
            maxDistance = nav.scrollStep
        var plan = Viewport.revealPlan(target, nav.root, horizontal)
        if (plan === null)
            return false
        if (!Viewport.applyPlan(plan, horizontal, maxDistance))
            return false
        if (!nav._centerVisibleThroughClips(target))
            return false
        target.forceActiveFocus(reason)
        return target.activeFocus === true
    }

    function move(key) {
        return nav.moveFrom(nav.activeItem(), key)
    }

    function handle(event) {
        if (!event || !nav.isDirectionalKey(event.key))
            return false
        if (event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier))
            return false
        nav.beginNavigation(event.key)
        if (!nav.move(event.key))
            return false
        event.accepted = true
        return true
    }
}
