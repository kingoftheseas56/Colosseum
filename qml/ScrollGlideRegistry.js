.pragma library

// Which ScrollGlide owns which Flickable. A pointer handler blocks the wheel from items behind it,
// so a nested glide already at its end hands the wheel to the glide of the nearest scrolling
// ancestor itself (a Library wall at its bottom passes the wheel to the world page).

var _entries = []

function register(flick, glide) {
    unregister(glide)
    if (flick)
        _entries.push({ flick: flick, glide: glide })
}

function unregister(glide) {
    _entries = _entries.filter(function(e) { return e.glide !== glide })
}

function glideFor(flick) {
    for (var i = 0; i < _entries.length; ++i) {
        if (_entries[i].flick === flick)
            return _entries[i].glide
    }
    return null
}

// A keyboard landing glides the page to its new row. The next key must see the settled
// geometry, never a half-scrolled page, so every key handler settles keyboard glides first.
function settleKeyboardGlides() {
    for (var i = 0; i < _entries.length; ++i)
        _entries[i].glide.settleKeyboardGlide()
}

function outerGlide(flick) {
    for (var p = flick ? flick.parent : null; p; p = p.parent) {
        for (var i = 0; i < _entries.length; ++i) {
            if (_entries[i].flick === p)
                return _entries[i].glide
        }
    }
    return null
}
