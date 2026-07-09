.pragma library

var histories = ({})

function normalizeScope(scope) {
    var s = String(scope || "").trim()
    return s.length ? s : "default"
}

function cleanQuery(query) {
    return String(query || "").trim()
}

function list(scope) {
    var key = normalizeScope(scope)
    return (histories[key] || []).slice(0)
}

function record(scope, query) {
    var q = cleanQuery(query)
    if (q.length < 2)
        return list(scope)

    var key = normalizeScope(scope)
    var lower = q.toLowerCase()
    var current = histories[key] || []
    var next = current.filter(function(item) {
        return String(item || "").toLowerCase() !== lower
    })
    next.unshift(q)
    histories[key] = next.slice(0, 6)
    return list(key)
}

function remove(scope, query) {
    var key = normalizeScope(scope)
    var lower = cleanQuery(query).toLowerCase()
    histories[key] = (histories[key] || []).filter(function(item) {
        return String(item || "").toLowerCase() !== lower
    })
    return list(key)
}
