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
function uniq(list) {
    var seen = {}, out = []
    list.forEach(function(v) { if (v !== "" && !seen[v]) { seen[v] = true; out.push(v) } })
    return out
}
function overlap(a, b) { return a.some(function(v) { return b.indexOf(v) >= 0 }) }
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
function albumArtists(c, a) { return uniq((a.artists || []).map(function(n) { return canonical(c,n) })) }
function trackSingers(c, a) {
    var names = []
    ;(a.tracks || []).forEach(function(t) { names = names.concat(t.artists || []) })
    return uniq(names.map(function(n) { return canonical(c,n) }))
}
function canonSingersOf(c, a, t) {
    var raw = (t.artists && t.artists.length) ? t.artists : (a.artists || [])
    return { raw: raw, canonical: uniq(raw.map(function(n) { return canonical(c,n) })) }
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
        result.push({id:a.id, type:"album", name:a.title, people:persons(c,a), verb:"played", album:a})
    })
    ;(profile.saved || []).forEach(function(id) {
        var a=albums(c).find(function(a) {return a.id===id})
        if (a && !result.some(function(e) {return e.id===id}))
            result.push({id:a.id,type:"album",name:a.title,people:persons(c,a),verb:"saved",album:a})
    })
    ;(picks || []).forEach(function(person) {
        if (albums(c).some(function(a) {return persons(c,a).indexOf(person)>=0}))
            result.push({id:person,type:"artist",name:person,people:[person],verb:"picked"})
    })
    return result
}
// A reason must be connected to the thing it explains. Preference order for
// the seed behind a recommended item: same composer or album artist, same
// track singer, same decade and language. Zero means not connected.
// seedInfo/albumInfo carry precomputed canonical people lists.
function seedInfoOf(c,seed) {
    var base = {type:seed.type,id:seed.id,name:seed.name,verb:seed.verb}
    if (seed.type !== "album") return base
    base.artists=albumArtists(c,seed.album); base.singers=trackSingers(c,seed.album)
    base.year=seed.album.year; base.language=seed.album.language
    return base
}
function albumInfoOf(c,a) {
    return {artists:albumArtists(c,a),singers:trackSingers(c,a),year:a.year,language:a.language}
}
function levelAlbum(seedInfo,albumInfo) {
    if (seedInfo.type==="artist")
        return albumInfo.artists.indexOf(seedInfo.id)>=0 ? 1 : albumInfo.singers.indexOf(seedInfo.id)>=0 ? 2 : 0
    if (overlap(albumInfo.artists,seedInfo.artists)) return 1
    if (overlap(albumInfo.singers,seedInfo.singers)) return 2
    if (Math.floor(albumInfo.year/10)===Math.floor(seedInfo.year/10) && albumInfo.language===seedInfo.language) return 3
    return 0
}
function levelSong(seedInfo,albumInfo,singers) {
    if (seedInfo.type==="artist") {
        if (albumInfo.artists.indexOf(seedInfo.id)>=0) return 1
        return singers.indexOf(seedInfo.id)>=0 ? 2 : 0
    }
    if (overlap(albumInfo.artists,seedInfo.artists)) return 1
    if (overlap(singers,seedInfo.singers)) return 2
    if (Math.floor(albumInfo.year/10)===Math.floor(seedInfo.year/10) && albumInfo.language===seedInfo.language) return 3
    return 0
}
function levelPerson(seedInfo,person) {
    if (seedInfo.type==="artist") return seedInfo.id===person ? 1 : 0
    if (seedInfo.artists.indexOf(person)>=0) return 1
    if (seedInfo.singers.indexOf(person)>=0) return 2
    return 0
}
function bestSeed(seedInfos,levelFor) {
    var best=null, bestLevel=0
    seedInfos.forEach(function(info) {
        var level=levelFor(info)
        if (level>0 && (!best || level<bestLevel)) { best=info; bestLevel=level }
    })
    return best
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
    var playable=albums(c)
    var seedInfos=evidence.map(function(seed) { return seedInfoOf(c,seed) })
    var albumInfo={}, albumSeed={}
    playable.forEach(function(a) {
        albumInfo[a.id]=albumInfoOf(c,a)
        albumSeed[a.id]=bestSeed(seedInfos,function(info){return levelAlbum(info,albumInfo[a.id])})
    })
    ;["songs","albums","artists","mixes","stations"].forEach(function(kind) {
        var items=[], seen={}
        playable.forEach(function(a) {
            var info=albumInfo[a.id]
            if (kind==="songs") {
                var taken=0
                a.tracks.forEach(function(t,trackIndex) {
                    if (taken>=2) return
                    var singers=canonSingersOf(c,a,t)
                    var seed=bestSeed(seedInfos,function(si){return levelSong(si,info,singers.canonical)})
                    if (!seed) return
                    var item=Object.assign(albumItem(a),{id:a.id+":"+trackIndex,kind:"songs",
                        title:t.title,artist:singers.raw.join(" · "),subtitle:singers.raw.join(" · "),
                        trackIndex:trackIndex,seconds:t.seconds,plays:t.plays})
                    if (seen[item.id]) return
                    seen[item.id]=true; taken++
                    items.push(withReason(item,seed))
                })
            } else if (kind==="albums") {
                var seed=albumSeed[a.id]
                if (!seed) return
                var album=albumItem(a)
                if (seen[album.id]) return
                seen[album.id]=true
                items.push(withReason(album,seed))
            } else if (kind==="artists") {
                persons(c,a).forEach(function(person) {
                    if (seen[person]) return
                    var seed=bestSeed(seedInfos,function(si){return levelPerson(si,person)})
                    if (!seed) return
                    seen[person]=true
                    var from=project(c,[a],"artists").filter(function(i){return i.id===person})[0]
                    if (from) items.push(withReason(from,seed))
                })
            } else {
                var mixSeed=albumSeed[a.id]
                if (!mixSeed) return
                var id=kind+":"+mixSeed.id
                if (seen[id]) return
                seen[id]=true
                var base=project(c,[a],"albums")[0]
                var mix=Object.assign({},base,{id:id,kind:kind,
                    title:mixSeed.name+(kind==="mixes" ? " mix" : " station"),
                    subtitle:"",sequence:playable.filter(function(x){return albumSeed[x.id] && albumSeed[x.id].id===mixSeed.id}).map(function(x){return x.id})})
                items.push(withReason(mix,mixSeed))
            }
        })
        if (items.length) result.push({key:"recommend-"+kind,title:"Recommended "+kind,
            icon:kind,kind:kind,items:items.slice(0,24)})
    })
    var newer=playable.filter(function(a) {
        return albumSeed[a.id] && a.year>=Number(String(c.snapshotDate).slice(0,4))-2
    }).sort(function(a,b) {return b.year-a.year}).map(function(a) {
        return withReason(albumItem(a),albumSeed[a.id])
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
// The picker leads with names a listener is likeliest to recognise: album
// artists ordered by their albums' total plays, then everyone else by plays.
// One person once, under the canonical name from the catalogue's people table.
function personStats(c) {
    var stats = {}
    albums(c).forEach(function(a) {
        var leads = albumArtists(c,a), credited = persons(c,a)
        credited.forEach(function(p) {
            if (!stats[p]) stats[p] = {person:p, leadPlays:0, creditedPlays:0,
                cover:a.cover, year:a.year, language:a.language, country:a.country}
            stats[p].creditedPlays += a.plays || 0
            if (leads.indexOf(p)>=0) stats[p].leadPlays += a.plays || 0
        })
    })
    return stats
}
function personMatches(c,person,query) {
    if (!query) return true
    var q = String(query).toLocaleLowerCase()
    var names = [person].concat((c.people || {})[person] || [])
    return names.some(function(n) { return String(n).toLocaleLowerCase().indexOf(q) >= 0 })
}
function artistOptions(c,query) {
    var stats = personStats(c), q = String(query || "")
    return Object.keys(stats).map(function(k) { return stats[k] })
        .filter(function(s) { return personMatches(c,s.person,q) })
        .sort(function(a,b) {
            var aLeads=a.leadPlays>0, bLeads=b.leadPlays>0
            if (aLeads!==bLeads) return aLeads ? -1 : 1
            var ap=aLeads ? a.leadPlays : a.creditedPlays
            var bp=bLeads ? b.leadPlays : b.creditedPlays
            return bp-ap || a.person.localeCompare(b.person)
        })
        .map(function(s) {
            return {id:s.person,kind:"artists",title:s.person,artist:s.person,subtitle:"",
                cover:s.cover,year:s.year,language:s.language,country:s.country,
                plays:s.creditedPlays,aspect:1,round:true,reason:""}
        })
}
