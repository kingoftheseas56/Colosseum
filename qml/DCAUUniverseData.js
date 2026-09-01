.pragma library

var hubs = [
    { id: "gotham", title: "Gotham", environment: "gotham", portal: "gotham.jpg", portalImdb: "tt0103359",
      videoIds: ["tt0103359","tt0106364","tt0118266","tt0143127","tt0337763","tt0346578","tt6556890"],
      comicPosts: [11366,153724,50187,15941,10470,183948,80956] },
    { id: "metropolis", title: "Metropolis", environment: "metropolis", portal: "metropolis.jpg", portalImdb: "tt0115378",
      videoIds: ["tt0115378","tt6075386"], comicPosts: [14615] },
    { id: "justice", title: "Justice League", environment: "space", portal: "watchtower.jpg", portalImdb: "tt0275137",
      videoIds: ["tt0247729","tt0275137","tt6025022","tt8752474"], comicPosts: [48881,10563,8823] },
    { id: "future", title: "Future Gotham", environment: "future", portal: "future-gotham.jpg", portalImdb: "tt0147746",
      videoIds: ["tt0147746","tt0231237","tt0233298","tt0260662"], comicPosts: [190572,163954,282726] }
]

function hub(id) {
    for (var i=0;i<hubs.length;++i) if (hubs[i].id === id) return hubs[i]
    return hubs[0]
}
function _section(payload,id) {
    var rows = payload ? payload.sections || [] : []
    for (var i=0;i<rows.length;++i) if (rows[i].id === id) return rows[i]
    return null
}
function _videos(payload) {
    var out=[]; var ids=["tv","shorts","movies"]
    for (var i=0;i<ids.length;++i) {
        var s=_section(payload,ids[i]); var rows=s ? s.entries || [] : []
        for (var j=0;j<rows.length;++j) out.push(rows[j])
    }
    return out
}
function videosForHub(payload,id) {
    var h=hub(id), all=_videos(payload), by={}
    for (var i=0;i<all.length;++i) by[String(all[i].id)]=all[i]
    var out=[]
    for (var j=0;j<h.videoIds.length;++j) if (by[h.videoIds[j]]) out.push(by[h.videoIds[j]])
    return out
}
function comicsForHub(payload,id) {
    var h=hub(id), s=_section(payload,"comics"), rows=s ? s.entries || [] : [], by={}
    for (var i=0;i<rows.length;++i) {
        var posts=rows[i].posts || []
        for (var j=0;j<posts.length;++j) by[Number(posts[j])]=rows[i]
    }
    var out=[]
    for (var k=0;k<h.comicPosts.length;++k) if (by[h.comicPosts[k]]) out.push(by[h.comicPosts[k]])
    return out
}
function progressBelongsToJustice(entry) {
    if (!entry) return false
    var h=hub("justice"), kind=String(entry.kind || ""), id=String(entry.id || "")
    if (kind === "video") {
        var root=id.split(":")[0]
        return h.videoIds.indexOf(root) >= 0
    }
    if (kind === "comic") {
        var ids=["gcd:5719","gcd:10924","gcd:11988"]
        return ids.indexOf(id) >= 0
    }
    return false
}
