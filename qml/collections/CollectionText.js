// Unicode-safe text helpers for the native Collections field limits.
// JS strings are UTF-16, so slicing at an arbitrary code unit can split a
// surrogate pair; the native store rejects lone surrogates on save.
.pragma library

function clamp(text, limit) {
    var value = text === undefined || text === null ? "" : String(text)
    if (value.length <= limit) return value
    var end = limit
    if (end > 0) {
        var last = value.charCodeAt(end - 1)
        if (last >= 0xD800 && last <= 0xDBFF) --end
    }
    return value.slice(0, end)
}
