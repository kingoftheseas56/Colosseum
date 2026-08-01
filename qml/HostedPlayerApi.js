// HostedPlayerApi.js — the trusted, APP-OWNED hosted-player provider registry.
//
// A `hosted-player` extension declares intent; it does NOT get to supply a URL or
// any JavaScript. Every embed URL is built here from constants keyed by an
// app-owned providerId, so a remote manifest can never point the iframe anywhere
// but VidKing's documented embed routes. VidKing is keyless: metadata identity is
// Cinemeta's `moviedb_id` (a TMDB id), never a TMDB API key.
//
// Three surfaces use this file:
//   rowsFor(...)       SourcesSheet builds hosted rows synchronously, before any
//                      stream fetch, and only when a valid TMDB id is known.
//   embedUrl(...)      HostedPlayerPage validates + constructs the real embed URL.
//   normalizeEvent(...) host.js / the page defensively normalize a postMessage so a
//                      hostile frame can never masquerade as a VidKing PLAYER_EVENT.
.pragma library

var VIDKING_EXTENSION_ID = "net.vidking.player";

// Colosseum gold, sent without the leading '#': VidKing's `color` query param.
var VIDKING_COLOR = "e8b923";
var VIDKING_ORIGIN = "https://www.vidking.net";

// The only event names VidKing's player emits that we act on. Anything else is
// rejected outright — an unrecognised name is treated as hostile, not ignored.
var ALLOWED_EVENTS = ["play", "playing", "pause", "timeupdate", "seeked", "ended", "error"];

function _int(v) {
    var n = Math.floor(Number(v));
    return isFinite(n) ? n : 0;
}

// True TMDB id: a positive integer. Cinemeta's moviedb_id, floored.
function _tmdb(v) {
    var n = _int(v);
    return n > 0 ? n : 0;
}

// The rows the Sources sheet shows for a given media context, in installed order.
// hostedExtensions = the enabled hosted-player extensions (AddonClient match);
// media = { type, imdbId, tmdbId, season, episode }.
function rowsFor(hostedExtensions, media) {
    var out = [];
    var ctx = media || ({});
    var tmdbId = _tmdb(ctx.tmdbId);
    var type = ctx.type === "series" ? "series" : "movie";
    var season = _int(ctx.season);
    var episode = _int(ctx.episode);
    // Optimism needs an identity: no id, no row. A series needs real coordinates.
    if (tmdbId <= 0) return out;
    if (type === "series" && (season <= 0 || episode <= 0)) return out;
    for (var i = 0; i < (hostedExtensions || []).length; ++i) {
        var e = hostedExtensions[i];
        if (!e || e.enabled !== true || e.id !== VIDKING_EXTENSION_ID) continue;
        out.push({
            kind: "hostedPlayer", extensionId: e.id, providerId: "vidking",
            addonName: (e.manifest && e.manifest.name) || "VidKing",
            sourceName: "VidKing", streamKind: "Hosted", streamLabel: "Web player",
            media: {
                type: type, imdbId: String(ctx.imdbId || ""), tmdbId: tmdbId,
                season: season, episode: episode
            }
        });
    }
    return out;
}

// The trusted provider adapters. Keyed by APP-OWNED providerId, never by anything
// a remote manifest controls. Each builds only its documented embed route.
function _vidkingEmbedUrl(media, resumeSeconds) {
    var ctx = media || ({});
    var type = ctx.type === "series" ? "series" : "movie";
    var tmdbId = _tmdb(ctx.tmdbId);
    if (tmdbId <= 0) return "";
    var progress = Math.max(0, _int(resumeSeconds));
    var enc = encodeURIComponent;
    if (type === "series") {
        var season = _int(ctx.season);
        var episode = _int(ctx.episode);
        if (season <= 0 || episode <= 0) return "";
        return VIDKING_ORIGIN + "/embed/tv/" + enc(tmdbId) + "/" + enc(season) + "/" + enc(episode)
             + "?color=" + enc(VIDKING_COLOR)
             + "&autoPlay=true&nextEpisode=true&episodeSelector=true"
             + "&progress=" + enc(progress);
    }
    return VIDKING_ORIGIN + "/embed/movie/" + enc(tmdbId)
         + "?color=" + enc(VIDKING_COLOR)
         + "&autoPlay=true"
         + "&progress=" + enc(progress);
}

var PROVIDERS = { vidking: _vidkingEmbedUrl };

// The documented embed URL for a provider, or "" for any unregistered provider.
function embedUrl(providerId, media, resumeSeconds) {
    var build = PROVIDERS[providerId];
    if (!build) return "";
    return build(media, resumeSeconds);
}

function _finiteNonNeg(v) {
    if (v === undefined || v === null) return 0;
    var n = Number(v);
    if (!isFinite(n) || n < 0) return NaN;   // NaN signals "reject"
    return n;
}

// Normalize a raw postMessage payload into a trusted event, or null. Accepts an
// object or a JSON string. Requires type === "PLAYER_EVENT" and an allowed event
// name; clamps finite numeric values and rejects anything malformed.
function normalizeEvent(raw) {
    var msg = raw;
    if (typeof msg === "string") {
        try { msg = JSON.parse(msg); } catch (e) { return null; }
    }
    if (!msg || typeof msg !== "object") return null;
    if (msg.type !== "PLAYER_EVENT") return null;
    var d = msg.data;
    if (!d || typeof d !== "object") return null;
    if (ALLOWED_EVENTS.indexOf(d.event) === -1) return null;
    var ct = _finiteNonNeg(d.currentTime);
    var du = _finiteNonNeg(d.duration);
    if (isNaN(ct) || isNaN(du)) return null;
    // clamp a slightly-over currentTime to duration rather than trust it blindly
    if (du > 0 && ct > du) ct = du;
    var pr = _finiteNonNeg(d.progress);
    if (isNaN(pr)) pr = 0;
    return { event: d.event, currentTime: ct, duration: du, progress: pr };
}
