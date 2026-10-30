.pragma library

var owners = []
var observers = []

function subscribe(window, observer) {
    unsubscribe(observer)
    if (observer) observers.push({window:window, observer:observer})
}

function unsubscribe(observer) {
    observers=observers.filter(function(entry) { return entry.observer && entry.observer!==observer })
}

function changed(window) {
    for (var i=0;i<observers.length;++i)
        if (observers[i].window===window && observers[i].observer) observers[i].observer.revision++
}

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
    changed(window)
    return returnItem
}

function release(sheet) {
    var window=null
    for (var i=0;i<owners.length;++i)
        if (owners[i].sheet===sheet) window=owners[i].window
    owners = owners.filter(function(owner) { return owner.sheet !== sheet })
    if (window) changed(window)
}

function dismiss(window) {
    var sheet = current(window)
    if (!sheet || !sheet.visible) return false
    sheet.close()
    return true
}
