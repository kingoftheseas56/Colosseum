// Pure projection/filter logic for the Top Comics ranked wall.
.pragma library

function prepare(rows, availabilityFn) {
    var source = Array.isArray(rows) ? rows : []
    var canDownload = typeof availabilityFn === "function" ? availabilityFn : function() { return false }
    return source.map(function(row, index) {
        var out = {}
        for (var key in row) out[key] = row[key]
        out.displayRank = index + 1
        out.downloadable = !!canDownload(row.locgId)
        return out
    })
}

function filter(rows, query, downloadableOnly) {
    var needle = String(query || "").trim().toLowerCase()
    var source = Array.isArray(rows) ? rows : []
    return source.filter(function(row) {
        if (downloadableOnly && !row.downloadable) return false
        if (!needle.length) return true
        var title = String(row.title || row.caption || "").toLowerCase()
        var publisher = String(row.publisher || "").toLowerCase()
        return title.indexOf(needle) >= 0 || publisher.indexOf(needle) >= 0
    })
}
