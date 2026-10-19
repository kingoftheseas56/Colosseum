// PosterSourcePolicy — pure poster-URL mechanics, centralized here instead of scattered through
// cards and world APIs. It knows ONLY URL shape: it normalizes the Metahub host to the canonical
// live host and resolves a Metahub poster to the SMALL size. It does NOT know catalogue ranking,
// title metadata, or QML state, and it must never rewrite an arbitrary provider URL.
//
// Why small, not medium (measured 2026-08-02, after Hemanth flagged slow poster loading):
//   metahub small = 300x450, medium = 500x750. The gallery poster (148px) decodes at ≤296px (2x
//   cap), so BOTH sizes downscale to the same ~296px — medium adds no visible sharpness at this
//   display size. Medium also costs 2-3x the bytes AND ~2.1s of dead wait on the many long-tail
//   titles the source has no medium for (it 404s slowly, then falls back). Small is the fast,
//   reliable, sufficient choice. (This is why the pre-arc code forced small; the medium-first
//   experiment was a net loss here and is retired.)
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

function _isMetahubPoster(url) {
    return url.indexOf("live.metahub.space/poster/") !== -1 && POSTER_SEG.test(url);
}

// Derive only the recorded GetComics upload behind this exact WordPress proxy.
// The original remains primary; unknown query semantics are never guessed away.
function getComicsOriginCandidate(url) {
    if (typeof url !== "string" || !url || url.length > 8192
            || /[\u0000-\u0020\u007f\\]/.test(url))
        return "";
    var match = /^https?:\/\/i0\.wp\.com\/getcomics\.org(\/share\/uploads\/[^?#]+)(?:\?([^#]*))?$/i.exec(url);
    if (!match)
        return "";
    var path = match[1];
    var decoded;
    try { decoded = decodeURIComponent(path); } catch (e) { return ""; }
    if (/[\u0000-\u001f\u007f\\]/.test(decoded)
            || !/\.(?:jpe?g|png|webp|gif|avif)$/i.test(decoded))
        return "";
    var segments = decoded.split("/");
    for (var i = 0; i < segments.length; i++)
        if (segments[i] === "." || segments[i] === "..")
            return "";
    if (match[2]) {
        var seenParameters = {};
        var pairs = match[2].split("&");
        for (var j = 0; j < pairs.length; j++) {
            var pair = pairs[j].split("=");
            if (pair.length !== 2)
                return "";
            var key, value;
            try {
                key = decodeURIComponent(pair[0]);
                value = decodeURIComponent(pair[1]);
            } catch (e2) { return ""; }
            if (seenParameters[key])
                return "";
            seenParameters[key] = true;
            if (key === "fit") {
                if (!/^\d{1,5},\d{1,5}$/.test(value))
                    return "";
            } else if (key === "ssl") {
                if (value !== "0" && value !== "1")
                    return "";
            } else {
                return "";
            }
        }
    }
    return "https://getcomics.org" + path;
}

// candidates — the de-duplicated list a card tries. A Metahub poster resolves to a single SMALL
// candidate (host-normalized, size forced to small) regardless of the supplied size. For foreign
// art the supplied URL is the only candidate unless it is the exact GetComics proxy
// above or the provider hands over its own explicit list.
// Empty/invalid input -> []. The single-candidate result means RoundedPosterImage's fallback state
// machine simply never advances for Metahub art — an exhausted small keeps the stable placeholder.
function candidates(url, explicitCandidates) {
    var out = [];
    var seen = {};
    function add(u) {
        if (!u || seen[u])
            return;
        seen[u] = true;
        out.push(u);
    }
    function addWithOrigin(u) {
        add(u);
        add(getComicsOriginCandidate(u));
    }
    var primary = liveUrl(url);
    if (primary) {
        if (_isMetahubPoster(primary))
            primary = primary.replace(POSTER_SEG, "/poster/small/");
        addWithOrigin(primary);
    }
    if (explicitCandidates)
        for (var i = 0; i < explicitCandidates.length; i++) {
            var c = liveUrl(explicitCandidates[i]);
            if (_isMetahubPoster(c))
                c = c.replace(POSTER_SEG, "/poster/small/");
            addWithOrigin(c);
        }
    return out;
}
