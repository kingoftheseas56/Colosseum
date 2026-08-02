// PosterSourcePolicy — pure poster-URL mechanics, centralized here instead of scattered through
// cards and world APIs. It knows ONLY URL shape: it normalizes the Metahub host to the canonical
// live host, and for a Metahub poster it builds the ordered candidate ladder [medium, small] so a
// card tries the sharper file first and falls back locally without blanking the long tail. It does
// NOT know catalogue ranking, title metadata, or QML state, and it must never rewrite an arbitrary
// provider URL or claim a resolution it cannot verify. (Design 2026-08-02, §5.)
.pragma library

// Any *.metahub.space host collapses to the canonical live host. Foreign hosts never match.
var METAHUB_HOST = /^https?:\/\/[a-z0-9.-]*metahub\.space\//i;
// Recognize only the poster size segment metahub actually serves.
var POSTER_SEG   = /\/poster\/(small|medium|large)\//;

// liveUrl — hostname normalization only. Metahub -> live.metahub.space; everything else byte-for-byte.
function liveUrl(url) {
    if (!url)
        return "";
    return String(url).replace(METAHUB_HOST, "https://live.metahub.space/");
}

function _withSize(url, size) {
    return url.replace(POSTER_SEG, "/poster/" + size + "/");
}

function _isMetahubPoster(url) {
    return url.indexOf("live.metahub.space/poster/") !== -1 && POSTER_SEG.test(url);
}

// candidates — the ordered, de-duplicated list a card tries in sequence. For a Metahub poster the
// ladder is [medium, small] regardless of the supplied size (medium gives headroom without keeping
// the largest texture; small is the reliable floor). For foreign art the supplied URL is the only
// candidate unless the provider hands over its own explicit list. Empty/invalid input -> [].
function candidates(url, explicitCandidates) {
    var out = [];
    var seen = {};
    function add(u) {
        if (!u || seen[u])
            return;
        seen[u] = true;
        out.push(u);
    }
    var primary = liveUrl(url);
    if (primary) {
        if (_isMetahubPoster(primary)) {
            add(_withSize(primary, "medium"));
            add(_withSize(primary, "small"));
        } else {
            add(primary);
        }
    }
    if (explicitCandidates)
        for (var i = 0; i < explicitCandidates.length; i++)
            add(liveUrl(explicitCandidates[i]));
    return out;
}
