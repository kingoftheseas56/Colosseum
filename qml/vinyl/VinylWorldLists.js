.pragma library

// Fixture projections, not production recommendation/ranking algorithms.
// trend and essential are used verbatim from the explicitly invented snapshot fields.
var listDefinitions = [
    {key: "popular", title: "Popular", icon: "popular"},
    {key: "trending", title: "Trending", icon: "trending"},
    {key: "new", title: "New and notable", icon: "new"},
    {key: "essentials", title: "Essentials", icon: "essentials"},
    {key: "global", title: "Global", icon: "global"}
]
function copy(o) { return Object.assign({}, o) }
function canonical(c, name) {
    var table = c.people || {}, keys = Object.keys(table)
    if (keys.indexOf(name) >= 0) return name
    function simple(s) { return String(s).toLowerCase().replace(/[^a-z0-9]/g, "") }
    var exact = keys.filter(function(k) { return simple(k) === simple(name) })
    if (exact.length === 1) return exact[0]
    var aliases = keys.filter(function(k) { return table[k].indexOf(name) >= 0 })
    return aliases.length === 1 ? aliases[0] : name
}
function persons(c, a) {
    var seen = {}, result = []
    var names = (a.artists || []).slice()
    ;(a.tracks || []).forEach(function(t) { names = names.concat(t.artists || []) })
    names.forEach(function(n) {
        var key = canonical(c,n)
        if (key && !seen[key]) { seen[key] = true; result.push(key) }
    })
    return result
}
function albums(c) { return (c.albums || []).filter(function(a) { return a.playable !== false }) }
function matches(a, lens) {
    return ["genre","country","language"].every(function(k) { return !lens[k] || a[k] === lens[k] })
        && (!lens.decade || Math.floor(a.year / 10) * 10 + "s" === lens.decade)
}
function descending(key) {
    return function(a,b) { return (b[key] || 0) - (a[key] || 0) || String(a.id).localeCompare(String(b.id)) }
}
function albumItem(a) {
    return {id: a.id, albumId: a.id, kind: "albums", title: a.title,
        subtitle: a.artists.join(" · ") + " · " + a.year, artist: a.artists.join(" · "),
        cover: a.cover, year: a.year, language: a.language, country: a.country,
        plays: a.plays, aspect: 1, round: false, reason: ""}
}
function project(c, source, kind) {
    if (kind === "albums") return source.map(albumItem)
    var result = [], seen = {}
    source.forEach(function(a) {
        if (kind === "artists") {
            persons(c,a).forEach(function(name) {
                if (seen[name]) return
                seen[name] = true
                result.push(Object.assign(albumItem(a), {id: name, kind: "artists",
                    title: name, artist: name, subtitle: "", round: true}))
            })
        } else {
            a.tracks.forEach(function(t,index) {
                var id = a.id + ":" + index
                result.push(Object.assign(albumItem(a), {id:id, kind:"songs", title:t.title,
                    artist:(t.artists.length ? t.artists : a.artists).join(" · "),
                    subtitle:(t.artists.length ? t.artists : a.artists).join(" · "),
                    trackIndex:index, seconds:t.seconds, plays:t.plays}))
            })
        }
    })
    return result
}
function listItems(c, key, lens) {
    var source = albums(c).filter(function(a) { return matches(a,lens) })
    if (key === "trending") source = source.filter(function(a) { return a.trend >= 1.3 }).sort(descending("trend"))
    else if (key === "new") source = source.filter(function(a) { return a.year >= Number(String(c.snapshotDate).slice(0,4)) - 2 }).sort(descending("year"))
    else if (key === "essentials") source = source.filter(function(a) { return a.essential }).sort(descending("plays"))
    else if (key === "global") source = source.filter(function(a) { return (c.homeRegion || []).indexOf(a.country) < 0 }).sort(descending("plays"))
    else source.sort(descending("plays"))
    var out = project(c,source,lens.kind || "albums")
    if (key === "popular" && lens.kind === "songs") out.sort(descending("plays"))
    return out
}
function historyAlbums(c, profile) {
    var byId = {}, result = [], seen = {}
    albums(c).forEach(function(a) { byId[a.id] = a })
    ;(profile.history || []).slice().sort(function(a,b) { return Date.parse(b.playedAt)-Date.parse(a.playedAt) }).forEach(function(h) {
        if (byId[h.albumId] && !seen[h.albumId]) { seen[h.albumId] = true; result.push(byId[h.albumId]) }
    })
    return result
}
function threads(c,lens,profile) {
    var history = historyAlbums(c,profile), result = [], seen = {}
    var historic = history.map(function(a) {return a.id})
    history.forEach(function(seed) {
        persons(c,seed).forEach(function(person) {
            if (seen[person] || result.length >= 3) return
            seen[person] = true
            var source = albums(c).filter(function(a) {
                return matches(a,lens) && historic.indexOf(a.id)<0 && persons(c,a).indexOf(person)>=0
            }).sort(descending("plays"))
            var items = project(c,source,lens.kind)
            if (items.length) result.push({key:"thread-"+encodeURIComponent(person), title:"More from "+person,
                icon:"threads",items:items, kind:lens.kind})
        })
    })
    return result
}
function shelves(c,lens,profile) {
    var result = []
    listDefinitions.forEach(function(d) {
        var items = listItems(c,d.key,lens)
        if (items.length) result.push({key:d.key,title:d.title,icon:d.icon,items:items,kind:lens.kind})
    })
    return result.concat(threads(c,lens,profile))
}
function seedEvidence(c,profile,picks) {
    var result = []
    historyAlbums(c,profile).forEach(function(a) {
        result.push({id:a.id, type:"album", name:a.title, people:persons(c,a), verb:"played"})
    })
    ;(profile.saved || []).forEach(function(id) {
        var a=albums(c).find(function(a) {return a.id===id})
        if (a && !result.some(function(e) {return e.id===id}))
            result.push({id:a.id,type:"album",name:a.title,people:persons(c,a),verb:"saved"})
    })
    ;(picks || []).forEach(function(person) {
        if (albums(c).some(function(a) {return persons(c,a).indexOf(person)>=0}))
            result.push({id:person,type:"artist",name:person,people:[person],verb:"picked"})
    })
    return result
}
function withReason(item,seed) {
    return Object.assign({},item,{reason:"Because you "+seed.verb+" "+seed.name,
        reasonSubjectId:seed.id,reasonSubjectType:seed.type})
}
function homeRows(c,profile,picks) {
    var evidence=seedEvidence(c,profile,picks), result=[], history=historyAlbums(c,profile)
    if (!evidence.length) return result
    if (history.length) result.push({key:"jump-back",title:"Jump back in",icon:"history",
        kind:"albums",items:history.slice(0,12).map(albumItem)})
    var related=[]
    albums(c).forEach(function(a) {
        var people=persons(c,a)
        var seed=evidence.find(function(e) {return e.people.some(function(p) {return people.indexOf(p)>=0})})
        if (seed) related.push({album:a,seed:seed})
    })
    ;["songs","albums","artists","mixes","stations"].forEach(function(kind) {
        var items=[], seen={}
        related.forEach(function(pair) {
            var baseKind=kind==="mixes" || kind==="stations" ? "albums" : kind
            project(c,[pair.album],baseKind).slice(0,kind==="songs" ? 2 : 20).forEach(function(item) {
                if (kind==="mixes" || kind==="stations") {
                    item=Object.assign({},item,{id:kind+":"+pair.seed.id,kind:kind,
                        title:pair.seed.name+(kind==="mixes" ? " mix" : " station"),
                        subtitle:"",sequence:related.filter(function(p) {
                            return p.seed.id===pair.seed.id
                        }).map(function(p) {return p.album.id})})
                }
                if (!seen[item.id]) { seen[item.id]=true; items.push(withReason(item,pair.seed)) }
            })
        })
        if (items.length) result.push({key:"recommend-"+kind,title:"Recommended "+kind,
            icon:kind,kind:kind,items:items.slice(0,24)})
    })
    var newer=related.filter(function(p) {
        return p.album.year>=Number(String(c.snapshotDate).slice(0,4))-2
    }).sort(function(a,b) {return b.album.year-a.album.year}).map(function(p) {
        return withReason(albumItem(p.album),p.seed)
    })
    if (newer.length) result.push({key:"new-artists",title:"New from artists you play",
        icon:"new",kind:"albums",items:newer})
    var last={}, cutoff=Date.parse(c.snapshotDate)-90*86400000
    ;(profile.history || []).forEach(function(h) {last[h.albumId]=Math.max(last[h.albumId]||0,Date.parse(h.playedAt))})
    var old=history.filter(function(a) {return last[a.id]<cutoff}).map(albumItem)
    if (old.length) result.push({key:"rediscovery",title:"Rediscovery",icon:"history",kind:"albums",items:old})
    return result
}
function coldShelves(c,profile,picks) {
    if (homeRows(c,profile,picks).length) return []
    var lens={kind:"albums"}
    return shelves(c,lens,{history:[]}).slice(0,3)
}
function artistOptions(c,query) {
    var items=project(c,albums(c),"artists")
    return items.filter(function(i) {return i.title.toLocaleLowerCase().indexOf(String(query||"").toLocaleLowerCase())>=0})
        .sort(function(a,b) {return a.title.localeCompare(b.title)})
}
