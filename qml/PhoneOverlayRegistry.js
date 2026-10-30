.pragma library

var owners = []

function current(window) {
    for (var i = 0; i < owners.length; ++i)
        if (owners[i].window === window) return owners[i].sheet
    return null
}

function claim(sheet, window) {
    var previous = current(window)
    var returnItem = previous ? previous.focusReturnItem : (window ? window.activeFocusItem : null)
    if (previous && previous !== sheet) {
        previous.focusReturnItem = null
        previous.close()
    }
    release(sheet)
    owners.push({window:window, sheet:sheet})
    return returnItem
}

function release(sheet) {
    owners = owners.filter(function(owner) { return owner.sheet !== sheet })
}

function dismiss(window) {
    var sheet = current(window)
    if (!sheet || !sheet.visible) return false
    sheet.close()
    return true
}
