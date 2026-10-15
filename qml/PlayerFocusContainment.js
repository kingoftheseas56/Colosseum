.pragma library

function isWithin(item, container) {
    var node = item
    while (node) {
        if (node === container)
            return true
        node = node.parent
    }
    return false
}

function move(windowObject, container, forward) {
    var active = windowObject ? windowObject.activeFocusItem : null
    var seed = active && active.nextItemInFocusChain ? active : container
    if (!seed || !seed.nextItemInFocusChain)
        return false
    var candidate = seed
    for (var i = 0; i < 256; ++i) {
        candidate = candidate.nextItemInFocusChain(forward)
        if (!candidate)
            break
        if (candidate === seed) {
            if (isWithin(seed, container) && seed.visible && seed.enabled) {
                seed.forceActiveFocus()
                return true
            }
            break
        }
        if (isWithin(candidate, container) && candidate.visible && candidate.enabled) {
            candidate.forceActiveFocus()
            return true
        }
    }
    return false
}