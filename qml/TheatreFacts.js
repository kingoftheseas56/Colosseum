.pragma library
// AF2 fact block assembly — pure and headless-testable. Input: the Cinemeta meta
// object (fields we previously discarded) + optional anime extras from AniList
// ({studio, source}). Output: [{k, v}] with blank rows OMITTED (hide-when-blank).

function joinNames(v) {
    if (!v) return ""
    if (Array.isArray(v)) return v.filter(Boolean).slice(0, 3).join(", ")
    return String(v)
}

function factRows(meta, extras) {
    var rows = []
    function push(k, v) { var s = joinNames(v); if (s) rows.push({ "k": k, "v": s }) }
    if (meta) {
        push("Director", meta.director)
        push("Writers", meta.writer)
    }
    if (extras) push("Studio", extras.studio)
    if (meta) {
        push("Network", meta.network)
        push("Country", meta.country)
        push("Aired", meta.releaseInfo || (meta.released ? String(meta.released).slice(0, 10) : ""))
        push("Status", meta.status)
    }
    if (extras) push("Source", extras.source)
    return rows
}
