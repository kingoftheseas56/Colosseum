.pragma library
var MIN_UPVOTES = 10
var WORDS_PER_MINUTE = 210
var READING_LEAD_MS = 500
function pick(notes, availableMs) {
    if (!notes || !notes.length) return null
    function words(n) { return String(n.text || "").trim().split(/\s+/).filter(Boolean).length }
    var candidates=notes.filter(function(n){return n.verified || n.votes>=MIN_UPVOTES})
    candidates.sort(function(a,b){
        if (!!a.verified!==!!b.verified) return a.verified?-1:1
        return words(a)-words(b)
    })
    if (!candidates.length) return null
    var chosen=candidates[0]
    return words(chosen)*60000/WORDS_PER_MINUTE+READING_LEAD_MS<=availableMs?chosen:null
}
