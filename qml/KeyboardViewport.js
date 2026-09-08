.pragma library

// Shared bounds and synchronous reveal geometry. Directional focus commits only
// after its viewport has moved; wheel/page animation remains ScrollGlide-owned.
var controllerEntries = []

function registerController(flick, controller) {
    if (!flick || !controller)
        return
    for (var i = 0; i < controllerEntries.length; ++i) {
        if (controllerEntries[i].flick === flick) {
            controllerEntries[i].controller = controller
            return
        }
    }
    controllerEntries.push({ flick: flick, controller: controller })
}

function unregisterController(flick, controller) {
    for (var i = controllerEntries.length - 1; i >= 0; --i) {
        if (controllerEntries[i].flick === flick
                && (!controller || controllerEntries[i].controller === controller))
            controllerEntries.splice(i, 1)
    }
}

function controllerFor(flick) {
    for (var i = controllerEntries.length - 1; i >= 0; --i) {
        if (controllerEntries[i].flick === flick)
            return controllerEntries[i].controller
    }
    return null
}

function isFlickable(item) {
    return item && item.contentItem !== undefined && item.contentY !== undefined
        && item.originY !== undefined && item.cancelFlick !== undefined
}

function contains(root, item) {
    for (var node = item; node; node = node.parent) {
        if (node === root)
            return true
    }
    return false
}

function minimum(flick, horizontal) {
    return horizontal ? flick.originX - flick.leftMargin : flick.originY - flick.topMargin
}

function maximum(flick, horizontal) {
    var end = horizontal ? flick.originX + flick.contentWidth - flick.width + flick.rightMargin
                         : flick.originY + flick.contentHeight - flick.height + flick.bottomMargin
    return Math.max(minimum(flick, horizontal), end)
}

function position(flick, horizontal) {
    return horizontal ? flick.contentX : flick.contentY
}

function bounded(flick, horizontal, value) {
    return Math.max(minimum(flick, horizontal), Math.min(maximum(flick, horizontal), value))
}

function setPosition(flick, horizontal, value) {
    var target = bounded(flick, horizontal, value)
    if (Math.abs(target - position(flick, horizontal)) < 0.5)
        return false
    flick.cancelFlick()
    if (horizontal)
        flick.contentX = target
    else
        flick.contentY = target
    return true
}

// Compute every inner-to-outer scroll before mutating anything. A fixed clip or
// an unreachable scroll extent rejects the plan, never a hidden final landing.
function revealPlan(item, root, horizontal) {
    var plan = []
    for (var ancestor = item.parent; ancestor; ancestor = ancestor.parent) {
        if (ancestor.clip || ancestor === root) {
            var p = item.mapToItem(ancestor, 0, 0)
            var q = item.mapToItem(ancestor, item.width, item.height)
            for (var i = 0; i < plan.length; ++i) {
                var entry = plan[i]
                var a = entry.flick.mapToItem(ancestor, 0, 0)
                var b = entry.flick.mapToItem(ancestor,
                    horizontal ? entry.delta : 0, horizontal ? 0 : entry.delta)
                p.x -= b.x - a.x; q.x -= b.x - a.x
                p.y -= b.y - a.y; q.y -= b.y - a.y
            }
            var start = horizontal ? Math.min(p.x, q.x) : Math.min(p.y, q.y)
            var end = horizontal ? Math.max(p.x, q.x) : Math.max(p.y, q.y)
            var extent = horizontal ? ancestor.width : ancestor.height
            var crossCenter = horizontal ? (p.y + q.y) / 2 : (p.x + q.x) / 2
            var crossExtent = horizontal ? ancestor.height : ancestor.width
            if (crossCenter < 0 || crossCenter > crossExtent)
                return null
            if (isFlickable(ancestor) && contains(ancestor.contentItem, item)) {
                var delta = start < 0 ? start : (end > extent ? end - extent : 0)
                // Oversized content needs an identifiable portion, not impossible containment.
                if (end - start > extent)
                    delta = start < 0 && end > extent ? 0 : start
                var before = position(ancestor, horizontal)
                var target = bounded(ancestor, horizontal, before + delta)
                delta = target - before
                if ((start + end) / 2 - delta < 0 || (start + end) / 2 - delta > extent)
                    return null
                if (Math.abs(delta) >= 0.5)
                    plan.push({ flick: ancestor, target: target, delta: delta })
            } else if ((start + end) / 2 < 0 || (start + end) / 2 > extent) {
                return null
            }
        }
        if (ancestor === root)
            return plan
    }
    return null
}

function applyPlan(plan, horizontal, maxDistance) {
    var distance = 0
    for (var i = 0; i < plan.length; ++i)
        distance += Math.abs(plan[i].delta)
    if (maxDistance !== undefined && distance > maxDistance + 0.000001)
        return false
    for (var j = 0; j < plan.length; ++j)
        setPosition(plan[j].flick, horizontal, plan[j].target)
    return true
}
