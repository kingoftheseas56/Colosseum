.pragma library

var BADGE_ROOT = "../assets/universes/one-piece/paradise/badges/";
var BANNER_ROOT = "../assets/universes/one-piece/paradise/banners/";

var _assets = {
    "reverse-mountain": { markerId: "reverse-mountain" },
    "whisky-peak": { markerId: "whisky-peak" },
    "little-garden": { markerId: "little-garden" },
    "drum-island": { markerId: "drum-island" },
    "arabasta": { markerId: "arabasta" },
    "jaya": { markerId: "jaya" },
    "skypiea": { markerId: "skypiea" },
    "long-ring-long-land": { markerId: "long-ring-long-land" },
    "water-7": { markerId: "water-7" },
    "enies-lobby": { markerId: "enies-lobby" },
    "post-enies-lobby": { markerId: "water-7" },
    "thriller-bark": { markerId: "thriller-bark" },
    "sabaody": { markerId: "sabaody" },
    "amazon-lily": { markerId: "amazon-lily" },
    "impel-down": { markerId: "impel-down" },
    "marineford": { markerId: "marineford" },
    "post-war": { markerId: "post-war" },
    "return-to-sabaody": { markerId: "sabaody" }
};
function _asset(id) {
    var key = typeof id === "string" ? id : "";
    if (!Object.prototype.hasOwnProperty.call(_assets, key)) return null;
    var row = _assets[key];
    return {
        arcId: key,
        markerId: row.markerId,
        badgeSource: BADGE_ROOT + key + ".png",
        posterSource: BANNER_ROOT + key + ".png"
    };
}

function _copyArray(values) {
    var out = [];
    for (var i = 0; i < (values || []).length; ++i) {
        var value = values[i];
        if (value && typeof value === "object") {
            var row = {};
            for (var key in value) row[key] = value[key];
            out.push(row);
        } else {
            out.push(value);
        }
    }
    return out;
}

function artForArc(arc) {
    if (!arc || typeof arc.id !== "string") return null;
    return _asset(arc.id);
}
function markerSourceForArc(arc) {
    var art = artForArc(arc);
    return art ? art.badgeSource : "";
}

function heroSourceForArc(arc) {
    var art = artForArc(arc);
    return art ? art.posterSource : "";
}

function cataloguePayload(arc) {
    var art = artForArc(arc);
    if (!art) return null;

    var out = {};
    for (var key in arc) out[key] = arc[key];

    out.id = art.arcId;
    out.markerId = art.markerId;
    out.markerSource = art.badgeSource;
    out.heroSource = art.posterSource;
    out.seaId = "paradise";
    out.seaLabel = "Paradise";
    out.onePacePrefixes = _copyArray(arc.onePacePrefixes);
    out.relatedMedia = _copyArray(arc.relatedMedia);
    out.liveActionSeason = Number(arc.liveActionSeason || 0);
    out.liveActionEpisodes = String(arc.liveActionEpisodes || "");

    return out;
}
function resolveArc(arc) {
    var payload = cataloguePayload(arc);
    if (!payload) return null;
    return {
        arcId: payload.id,
        locationId: String(payload.locationId || ""),
        markerId: payload.markerId,
        badgeSource: payload.markerSource,
        posterSource: payload.heroSource,
        catalogue: payload
    };
}

function isKnownArcId(id) {
    return _asset(id) !== null;
}
