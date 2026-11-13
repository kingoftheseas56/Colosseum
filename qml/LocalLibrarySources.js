.pragma library

// SourcesSheet integration seam. These rows MUST be activated through LocalLaunch
// (localPath), never passed as torrent info hashes or offered as downloads.
function canonical(value) { return String(value || "").replace(/^imdb:/, "") }
function sourceRow(file, detail) {
    detail = detail || {}
    return { vaultLocal: true, localPath: file.path, vaultId: file.id,
        infoHash: "vault-local:" + file.id, fileIdx: 0, streamKind: "Local",
        addonId: "vault-local", addonName: "Your copy", source: "Vault",
        release: detail.where || file.path, quality: detail.quality || "",
        qualityLine: detail.quality || "Local file", size: Number(file.size) || 0,
        sizeBytes: Number(file.size) || 0, sizeText: detail.sizeText || "",
        seeds: 0, seeders: 0, audio: "", languages: [], tags: [],
        offline: !!file.away, available: !file.away, admissionVerdict: file.admissionVerdict || "" }
}
function sources(library, type, id, context) {
    if (!library || typeof library.series !== "function" || typeof library.items !== "function") return []
    context = context || {}
    var isShow = type === "series" || type === "tv"
    if (!isShow && type !== "movie") return []
    var requested = canonical(id), match = /^(tt\d+):(\d+):(\d+)$/.exec(requested)
    var titleId = match ? match[1] : requested
    var season = match ? Number(match[2]) : Number(context.season)
    var episode = match ? Number(match[3]) : Number(context.episode)
    // Never substitute another episode when an exact one is not known.
    if (isShow && (!isFinite(season) || !isFinite(episode) || season < 0 || episode < 1)) return []
    var groups = library.series("video"), candidates = [], detailsByPath = {}, seen = {}, out = []
    var admission = typeof library.admissionById === "function" ? library.admissionById() : {}
    for (var g = 0; g < groups.length; ++g) {
        var group = groups[g]
        if (!group.identityId || canonical(group.identityId) !== titleId) continue
        var files = library.items("video", group.key), paths = {}
        if (isShow) {
            if (typeof library.seasonFactsForShow !== "function") continue
            var seasons = (library.seasonFactsForShow(group.key) || {}).seasons || []
            for (var s = 0; s < seasons.length; ++s) {
                if (seasons[s].supplemental) continue
                var episodes = seasons[s].episodes || []
                for (var e = 0; e < episodes.length; ++e)
                    if (Number(episodes[e].season) === season && Number(episodes[e].episode) === episode)
                        paths[episodes[e].path] = true
            }
        } else {
            // Native browseDetail already distinguishes primary copies from trailers/extras.
            // Do not reproduce its filename grammar in QML.
            if (typeof library.browseDetail !== "function") continue
            var copies = (library.browseDetail(group.key) || {}).copies || []
            for (var c = 0; c < copies.length; ++c) {
                paths[copies[c].path] = true; detailsByPath[copies[c].path] = copies[c]
            }
        }
        for (var f = 0; f < files.length; ++f)
            if (paths[files[f].path]) candidates.push(files[f])
    }
    for (var i = 0; i < candidates.length; ++i) {
        var file = candidates[i]
        if (!file.path || seen[file.path] || file.away || file.identitySuppressed || admission[file.id] !== "Admitted") continue
        seen[file.path] = true
        out.push(sourceRow(file, detailsByPath[file.path]))
    }
    return out
}
