.pragma library
.import "UniverseContentApi.js" as ContentApi

var _arcIds = {
    "egghead": true,
    "elbaf": true
};

function _owns(object, key) {
    return typeof key === "string"
            && Object.prototype.hasOwnProperty.call(object, key);
}

function _clone(value) {
    if (value === null || value === undefined) return value;
    if (Array.isArray(value)) {
        var arrayOut = [];
        for (var i = 0; i < value.length; ++i) arrayOut.push(_clone(value[i]));
        return arrayOut;
    }
    if (typeof value === "object") {
        var objectOut = {};
        for (var key in value)
            if (Object.prototype.hasOwnProperty.call(value, key))
                objectOut[key] = _clone(value[key]);
        return objectOut;
    }
    return value;
}

function _badgeSource(id) {
    var content = ContentApi.document("tribute-one-piece-final-sea");
    if (content.artByArc && content.artByArc[id]) return ContentApi.resolveAssets(content.artByArc[id]).markerSource;
    return ContentApi.asset("../assets/universes/one-piece/final-sea/badges/" + id + ".png");
}

function _bannerSource(id) {
    var content = ContentApi.document("tribute-one-piece-final-sea");
    if (content.artByArc && content.artByArc[id]) return ContentApi.resolveAssets(content.artByArc[id]).heroSource;
    return ContentApi.asset("../assets/universes/one-piece/final-sea/banners/" + id + ".png");
}
function isRoutableArcId(id) {
    var content = ContentApi.document("tribute-one-piece-final-sea");
    if (content.story && Array.isArray(content.story.arcs)) return content.story.arcs.some(function(arc) { return arc && arc.id === id; });
    return _owns(_arcIds, id);
}

function markerSourceForArc(value) {
    var id = typeof value === "string" ? value : value && value.id;
    return isRoutableArcId(id) ? _badgeSource(id) : "";
}

function heroSourceForArc(value) {
    var id = typeof value === "string" ? value : value && value.id;
    return isRoutableArcId(id) ? _bannerSource(id) : "";
}

function cataloguePayload(arc) {
    if (!arc || !isRoutableArcId(arc.id)) return null;

    var payload = _clone(arc);
    payload.seaId = "final-sea";
    payload.markerSource = _badgeSource(arc.id);
    payload.heroSource = _bannerSource(arc.id);

    payload.storyRanges = {
        anime: String(arc.anime || ""),
        chapters: String(arc.chapters || ""),
        volumes: String(arc.volumes || ""),
        status: String(arc.status || "")
    };

    // The shared catalogue parser accepts only finite numeric ranges.
    // Keep open-ended story truth above, but route current provider snapshots below.
    payload.anime = String(arc.providerAnimeSpec || arc.anime || "");
    payload.volumes = String(arc.providerVolumeSpec || arc.volumes || "");

    payload.onePacePrefixes = _clone(arc.onePacePrefixes || []);
    payload.relatedMedia = _clone(arc.relatedMedia || []);
    return payload;
}

function relatedMediaForArc(arc) {
    var payload = cataloguePayload(arc);
    return payload ? _clone(payload.relatedMedia) : [];
}

function artForArc(value) {
    var content = ContentApi.document("tribute-one-piece-final-sea");
    var key = typeof value === "string" ? value : (value && value.id);
    if (content.artByArc && content.artByArc[key]) return ContentApi.resolveAssets(content.artByArc[key]);
    var id = typeof value === "string" ? value : value && value.id;
    if (!isRoutableArcId(id)) return null;
    return {
        markerSource: _badgeSource(id),
        heroSource: _bannerSource(id)
    };
}
