.pragma library
// ComicGcSources — pure grouping + sorting for the "Also on GetComics" sources page
// (spec 2026-07-17). The baked catalog rows paint instantly; a single ComicsApi
// postsById() call enriches them with cover/sizeMB/year, and size DESC then wins
// the within-group order (Hemanth's spec-gate call: sort by size, fullest grab first).

var GROUPS = [ { key: "bundle",     label: "Multi-volume packs" },
               { key: "collection", label: "Collected editions" },
               { key: "single",     label: "Single issues" } ]

// sources: baked [{id,title,link,date,kind,fan_made}]; enrich: { "<id>": {cover,sizeMB,year} }
// -> [{key,label,rows:[{id,title,link,date,fan_made,cover,sizeMB,year}]}], empty groups omitted.
// Unknown kinds bucket as singles — a post may never be silently dropped.
function groupSources(sources, enrich) {
    var e = enrich || ({})
    var out = []
    for (var g = 0; g < GROUPS.length; g++) {
        var rows = []
        for (var i = 0; i < (sources || []).length; i++) {
            var s = sources[i]
            var kind = (s.kind === "bundle" || s.kind === "collection") ? s.kind : "single"
            if (kind !== GROUPS[g].key) continue
            var x = e[String(s.id)] || ({})
            rows.push({ id: s.id, title: s.title || "", link: s.link || "",
                        date: s.date || "", fan_made: !!s.fan_made,
                        cover: x.cover || "", sizeMB: x.sizeMB || 0,
                        year: x.year || yearOf(s.date) })
        }
        rows.sort(function(a, b) {
            return (b.sizeMB - a.sizeMB)
                || (a.date < b.date ? 1 : (a.date > b.date ? -1 : 0))
        })
        if (rows.length) out.push({ key: GROUPS[g].key, label: GROUPS[g].label, rows: rows })
    }
    return out
}

function yearOf(date) {
    var m = String(date || "").match(/^(\d{4})/)
    return m ? Number(m[1]) : 0
}

// 350 -> "350 MB", 4300 -> "4.2 GB", 0/unknown -> ""
function sizeText(mb) {
    if (!mb) return ""
    return mb >= 1024 ? (Math.round(mb / 102.4) / 10) + " GB" : Math.round(mb) + " MB"
}
