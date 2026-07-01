function normalizeText(value) {
    return String(value || "")
        .toLowerCase()
        .replace(/&/g, " and ")
        .replace(/[^a-z0-9]+/g, " ")
        .replace(/\bthe\b/g, " ")
        .replace(/\s+/g, " ")
        .trim()
}

function titleVariants(title) {
    var raw = String(title || "").trim()
    var variants = []
    function add(v) {
        var n = normalizeText(v)
        if (n && variants.indexOf(n) < 0)
            variants.push(n)
    }
    add(raw)
    add(raw.replace(/\s*\([^()]*\)\s*$/, ""))
    var colon = raw.indexOf(":")
    if (colon >= 0)
        add(raw.substring(0, colon).trim())
    return variants
}

function surnameKey(author) {
    var parts = String(author || "").toLowerCase().split(/\s+/).filter(Boolean)
    if (!parts.length)
        return ""
    return parts[parts.length - 1].replace(/^[^a-z0-9]+|[^a-z0-9]+$/g, "")
}

function scoreBookCandidate(result, title, author) {
    var titleKeys = titleVariants(title)
    var candidateTitle = normalizeText(result && result.title ? result.title : "")
    var candidateAuthor = String(result && (result.author || result.authors) ? (result.author || result.authors) : "")
    var candidateSurname = surnameKey(candidateAuthor)
    var wantedSurname = surnameKey(author)
    var score = 0

    if (candidateTitle && titleKeys.indexOf(candidateTitle) >= 0)
        score += 100
    else {
        for (var i = 0; i < titleKeys.length; i++) {
            if (candidateTitle === titleKeys[i]) {
                score += 100
                break
            }
            if (candidateTitle.indexOf(titleKeys[i]) >= 0 || titleKeys[i].indexOf(candidateTitle) >= 0)
                score = Math.max(score, 40)
        }
    }

    if (wantedSurname && candidateSurname === wantedSurname)
        score += 30
    else if (wantedSurname && candidateAuthor.toLowerCase().indexOf(wantedSurname) >= 0)
        score += 20

    return score
}

function pickBookMatch(results, title, author) {
    if (!results || !results.length)
        return null

    var best = results[0]
    var bestScore = scoreBookCandidate(best, title, author)
    for (var i = 1; i < results.length; i++) {
        var score = scoreBookCandidate(results[i], title, author)
        if (score > bestScore) {
            best = results[i]
            bestScore = score
        }
    }
    return best
}

if (typeof module !== "undefined" && module.exports) {
    module.exports = {
        normalizeText: normalizeText,
        titleVariants: titleVariants,
        scoreBookCandidate: scoreBookCandidate,
        pickBookMatch: pickBookMatch
    }
}
