.pragma library

// Completed comic rows share the `gc:` prefix across two producer lanes:
// text suffix = GetComics tag shelf; numeric suffix = DB/LOCG catalogue id.
function destination(item) {
    var row = item || ({})
    if (String(row.packRole || "").length > 0)
        return "pack"

    var seriesId = String(row.seriesId || "")
    if (seriesId.indexOf("gcd:") === 0)
        return "gcd"
    if (seriesId.indexOf("gc:") === 0) {
        var suffix = seriesId.slice(3)
        return /^\d+$/.test(suffix) ? "locg" : "getcomics"
    }
    return "unknown"
}
