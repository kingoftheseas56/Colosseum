.pragma library
.import "CatalogDelivery.js" as CatalogDelivery

// Progress is the resume authority; this only supplies missing card presentation.
var known = {}
var knownRevision = -1

function exactDetailId(entry) {
    if (typeof entry.id !== "string" || typeof entry.libraryId !== "string")
        return ""
    var match
    if (entry.type === "movie")
        match = /^(tt[0-9]{1,12}|tmdb:[1-9][0-9]{0,9})$/.exec(entry.id)
    else if (entry.type === "series")
        match = /^(tt[0-9]{1,12}|tmdb:[1-9][0-9]{0,9}):(0|[1-9][0-9]{0,4}):[1-9][0-9]{0,4}$/.exec(entry.id)
    return match && match[0] === entry.id && match[1] === entry.libraryId ? match[1] : ""
}

function forEntry(entry, catalog) {
    if (!entry || entry.kind !== "video") return ({})
    var detailId = exactDetailId(entry)
    var match = /^tt[0-9]+(?::|$)/.exec(String(entry.id || ""))
    if (!detailId && !match) return ({})
    var id = detailId || match[0].replace(/:$/, "")
    var imdb = id.indexOf("tt") === 0
    var revision = catalog && catalog.revision !== undefined ? catalog.revision : -1
    if (revision !== knownRevision) { known = {}; knownRevision = revision }
    var info = known[id]
    if (imdb && !info && CatalogDelivery.available(catalog)) {
        info = catalog.titlePresentation(id)
        if (info && info.title) known[id] = info
    }
    // A known IMDb id gives us a stable poster route even when the progress
    // snapshot has no artwork. The color fallback stays visible if offline.
    var hue = 0
    for (var i = 2; i < id.length; ++i) hue = (hue * 31 + id.charCodeAt(i)) % 360
    return {
        title: info && info.title ? String(info.title) : "",
        type: detailId ? entry.type : info && info.type ? (info.type === "movie" ? "movie" : "series") : "",
        detailId: detailId,
        cover: imdb ? "https://live.metahub.space/poster/small/" + id + "/img" : "",
        c1: Qt.hsla(hue / 360, 0.29, 0.30, 1),
        c2: Qt.hsla(hue / 360, 0.23, 0.12, 1)
    }
}
