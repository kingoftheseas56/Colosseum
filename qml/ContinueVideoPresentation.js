.pragma library

// Progress is the resume authority; this only supplies missing card presentation.
var known = {}

function forEntry(entry, catalog) {
    if (!entry || entry.kind !== "video") return ({})
    var match = /^tt[0-9]+(?::|$)/.exec(String(entry.id || ""))
    if (!match) return ({})
    var id = match[0].replace(/:$/, "")
    var info = known[id]
    if (!info && catalog && catalog.ready()) {
        info = catalog.titlePresentation(id)
        if (info && info.title) known[id] = info
    }
    // A known IMDb id gives us a stable poster route even when the progress
    // snapshot has no artwork. The color fallback stays visible if offline.
    var hue = 0
    for (var i = 2; i < id.length; ++i) hue = (hue * 31 + id.charCodeAt(i)) % 360
    return {
        title: info && info.title ? String(info.title) : "",
        type: info && info.type ? (info.type === "movie" ? "movie" : "series") : "",
        cover: "https://live.metahub.space/poster/small/" + id + "/img",
        c1: Qt.hsla(hue / 360, 0.29, 0.30, 1),
        c2: Qt.hsla(hue / 360, 0.23, 0.12, 1)
    }
}
