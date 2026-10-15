.pragma library

function feedStatus(row) {
    if (!row) return {kind:"removed", message:"This row is no longer available for the selected apps or lens.", retry:false}
    var count = (row.items || []).length
    var source = row.sourceLabel || row.src || row.title || "this source"
    if (row.loading) return {kind:"loading", message:"Loading " + source + (count ? ". Showing cached titles." : "…"), retry:false}
    if (row.error) return {kind:"error", message:"Could not refresh " + source + (count ? ". Showing cached titles." : ". No titles are available yet."), retry:true}
    if (row.stale || row.fallbackReason) return {kind:"cached", message:"Showing cached titles from " + source + ". This row may be out of date.", retry:true}
    if (!count) return {kind:"empty", message:"No titles are listed by " + source + " for this selection.", retry:true}
    return {kind:"ready", message:"", retry:false}
}
function aggregate(rows, started) {
    if (!rows.length) return {kind:started ? "empty" : "idle",
        message:started ? "No catalogue rows are available for these apps and this lens." : "The catalogue has not loaded yet.", retry:true}
    var loading = 0, errors = 0, cached = 0
    for (var i = 0; i < rows.length; ++i) {
        var s = feedStatus(rows[i])
        if (s.kind === "loading") loading++
        if (s.kind === "error") errors++
        if (s.kind === "cached") cached++
    }
    if (loading) return {kind:"loading",message:"Refreshing catalogue rows. Search uses titles already loaded.",retry:false}
    if (errors || cached) return {kind:"cached",message:"Some catalogue rows could not be refreshed. Loaded titles remain available.",retry:true}
    return {kind:"ready",message:"",retry:false}
}
function entryKey(entry) {
    if (typeof entry === "string") return entry
    return entry && entry.id !== undefined ? String(entry.id) : ""
}
function identityIndex(entries, identity) {
    for (var i = 0; i < entries.length; ++i)
        if (entryKey(entries[i]) === identity) return i
    return -1
}
function remapIndex(previous, next, index) {
    if (index < 0 || index >= previous.length) return -1
    return identityIndex(next, entryKey(previous[index]))
}
function availabilityMessage(value, region, queryable, delayed) {
    value = value || ({status:"unsupported"})
    var country = value.region || region || "your region"
    if (!queryable || value.status === "unsupported") return "Availability lookup is not supported for this title. Provider links below are not access guarantees."
    if (value.status === "idle") return "Availability has not been checked for " + country + "."
    if (value.status === "loading") return delayed ? "Availability is taking longer than usual. Existing links remain available." : "Checking availability in " + country + "…"
    if (value.status === "error") return "Availability could not be checked for " + country + ". Existing links may be out of date."
    if (value.status === "unmatched") return "No matching title was found for " + country + ". Try another lookup or use a provider search link."
    if (value.status === "ready") return (value.offers || []).length
        ? "Listed offers for " + country + ". Login, plan access and playback are not confirmed."
        : "No streaming offers are listed for this title in " + country + "."
    return "Availability is not known for this title."
}
function doorKey(door) {
    return door ? String(door.pk || door.providerId || "") + "|" + String(door.url || "") + "|" + String(door.offerType || "") + "|" + String(door.level || "") : ""
}
