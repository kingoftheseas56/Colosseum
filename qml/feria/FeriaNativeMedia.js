.pragma library

// Admission is explicit. Browser-owned blob/MSE and encrypted sources retain
// their existing engine route; resolved CDN URLs stay transient, while canonical
// entry URLs use the existing activity/history owner.
function admit(candidate) {
    if (!candidate) return {ok:false, reason:"No direct video source is available on this page."}
    if (candidate.encrypted) return {ok:false, reason:"This video uses browser-managed protection. Use the website player."}
    if (candidate.ad) return {ok:false, reason:"Wait for the programme video before opening Feria player."}
    var source = String(candidate.source || "")
    if (/^blob:/i.test(source)) return {ok:false, reason:"This stream is managed inside the browser. Use the website player."}
    if (source.length > 8192 || /[\x00-\x20\x7f]/.test(source)
            || !/^https?:\/\/[^/?#@]+(?:[/?#]|$)/i.test(source))
        return {ok:false, reason:"No direct video source is available on this page."}
    var position = Number(candidate.position)
    return {ok:true, source:source, href:String(candidate.href || ""), title:String(candidate.title || ""),
        position:Number.isFinite(position) && position >= 0 ? position : 0,
        paused:candidate.paused === true, volume:Number.isFinite(candidate.volume) ? Math.max(0,Math.min(100,candidate.volume*100)) : 100,
        rate:Number.isFinite(candidate.rate) && candidate.rate > 0 && candidate.rate <= 4 ? candidate.rate : 1}
}
// QML's compiled functions stringify as [native code]. Keep browser code literal.
var probeSource = `(function(token) {
    var media = [], seen = []
    function visit(root, depth) {
        if (!root || !root.querySelectorAll || depth > 5 || seen.indexOf(root) >= 0) return
        seen.push(root)
        Array.from(root.querySelectorAll('video')).forEach(function(m) { media.push(m) })
        Array.from(root.querySelectorAll('iframe,frame')).slice(0,32).forEach(function(f) { try { visit(f.contentDocument,depth+1) } catch (_) {} })
        Array.from(root.querySelectorAll('*')).slice(0,3000).forEach(function(n) { if(n.shadowRoot)visit(n.shadowRoot,depth+1) })
    }
    visit(document,0)
    media.sort(function(a,b) { return Number(!b.paused)-Number(!a.paused) || (b.duration||0)-(a.duration||0) })
    var m = media[0]
    var source = m && (m.currentSrc || m.src) || ''
    if (!source && /\\.(mp4|m4v|webm|mkv|mov|avi|m3u8|mpd)(?:[?#]|$)/i.test(location.href)) source = location.href
    var result = {source:source, href:location.href, title:document.title, encrypted:!!(m && m.mediaKeys),
        ad:!!document.querySelector('.ad-showing,.ad-interrupting'), position:m?m.currentTime:0,
        paused:m?m.paused:false, volume:m?(m.muted?0:m.volume):1, rate:m?m.playbackRate:1}
    // Retain the exact element for handback, never a selector that could pick a replacement.
    window.__feriaNativeHandoff = {token:token,media:m,source:source,href:location.href}
    return result
})`
function probe(token) { return probeSource + '(' + JSON.stringify(token) + ')' }
function pause(token) {
    return '(function(){var h=window.__feriaNativeHandoff;if(!h||h.token!==' + JSON.stringify(token)
        + '||h.href!==location.href)return false;if(h.media){if((h.media.currentSrc||h.media.src)!==h.source)return false;h.media.pause()}return true})()'
}
function handBack(token, position, source) {
    var value = Number.isFinite(position) && position >= 0 ? position : 0
    return '(function(){var h=window.__feriaNativeHandoff;if(!h||h.token!==' + JSON.stringify(token)
        + '||h.href!==location.href)return false;delete window.__feriaNativeHandoff;var m=h.media;'
        + 'if(!m||!m.isConnected||h.source!==' + JSON.stringify(source) + '||(m.currentSrc||m.src)!==h.source)return false;'
        + 'try{m.currentTime=' + value + ';m.pause();return true}catch(_){return false}})()'
}
