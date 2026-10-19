.pragma library
.import "UniverseContentApi.js" as ContentApi

var _arcIds = {
    "fish-man-island": true,
    "punk-hazard": true,
    "dressrosa": true,
    "zou": true,
    "whole-cake-island": true,
    "wano-country": true
}

var _contextIds = {
    "levely": true
}

function _owns(object, key) {
    return typeof key === "string"
            && Object.prototype.hasOwnProperty.call(object, key)
}

function _badgeSource(id) {
    var content = typeof ContentApi !== "undefined" ? ContentApi.document("tribute-one-piece-new-world") : {};
    if (content.artByArc && content.artByArc[id]) return ContentApi.resolveAssets(content.artByArc[id]).markerSource;
    var fallback = "../assets/universes/one-piece/new-world/badges/" + id + ".png";
    return typeof ContentApi !== "undefined" ? ContentApi.asset(fallback) : fallback;
}

function _bannerSource(id) {
    var content = typeof ContentApi !== "undefined" ? ContentApi.document("tribute-one-piece-new-world") : {};
    if (content.artByArc && content.artByArc[id]) return ContentApi.resolveAssets(content.artByArc[id]).heroSource;
    var fallback = "../assets/universes/one-piece/new-world/banners/" + id + ".png";
    return typeof ContentApi !== "undefined" ? ContentApi.asset(fallback) : fallback;
}

function _clone(value) {
    if (value === null || value === undefined) return value
    if (Array.isArray(value)) {
        var arrayOut = []
        for (var i = 0; i < value.length; ++i) arrayOut.push(_clone(value[i]))
        return arrayOut
    }
    if (typeof value === "object") {
        var objectOut = {}
        for (var key in value)
            if (Object.prototype.hasOwnProperty.call(value, key))
                objectOut[key] = _clone(value[key])
        return objectOut
    }
    return value
}

function isRoutableArcId(id) {
    var content = typeof ContentApi !== "undefined" ? ContentApi.document("tribute-one-piece-new-world") : {};
    if (content.story && Array.isArray(content.story.arcs)) return content.story.arcs.some(function(arc) { return arc && arc.id === id; });
    return _owns(_arcIds, id)
}

function isContextId(id) {
    return _owns(_contextIds, id)
}

function _validArc(arc) {
    return !!arc
            && isRoutableArcId(arc.id)
            && typeof arc.title === "string" && arc.title.length > 0
            && typeof arc.locationId === "string" && arc.locationId.length > 0
            && typeof arc.anime === "string" && arc.anime.length > 0
            && typeof arc.chapters === "string" && arc.chapters.length > 0
            && typeof arc.volumes === "string" && arc.volumes.length > 0
            && Array.isArray(arc.onePacePrefixes)
}

function markerSourceForArc(value) {
    var id = typeof value === "string" ? value : value && value.id
    return isRoutableArcId(id) ? _badgeSource(id) : ""
}

function heroSourceForArc(value) {
    var id = typeof value === "string" ? value : value && value.id
    if (isRoutableArcId(id) || isContextId(id)) return _bannerSource(id)
    return ""
}

function cataloguePayload(arc) {
    if (!_validArc(arc)) return null
    var payload = _clone(arc)
    payload.seaId = "new-world"
    payload.markerSource = _badgeSource(arc.id)
    payload.heroSource = _bannerSource(arc.id)
    payload.onePacePrefixes = _clone(arc.onePacePrefixes || [])
    payload.relatedMedia = _clone(arc.relatedMedia || [])
    return payload
}

function contextPayload(context) {
    if (!context || !isContextId(context.id)) return null
    var payload = _clone(context)
    payload.interactive = false
    payload.markerSource = ""
    payload.heroSource = _bannerSource(context.id)
    return payload
}

function relatedMediaForArc(arc) {
    var payload = cataloguePayload(arc)
    return payload ? _clone(payload.relatedMedia) : []
}

function artForArc(value) {
    var content = typeof ContentApi !== "undefined" ? ContentApi.document("tribute-one-piece-new-world") : {};
    var key = typeof value === "string" ? value : (value && value.id);
    if (content.artByArc && content.artByArc[key]) return ContentApi.resolveAssets(content.artByArc[key]);
    var id = typeof value === "string" ? value : value && value.id
    if (!isRoutableArcId(id)) return null
    return {
        markerSource: _badgeSource(id),
        heroSource: _bannerSource(id)
    }
}

function contextArt(value) {
    var id = typeof value === "string" ? value : value && value.id
    if (!isContextId(id)) return null
    return {
        markerSource: "",
        badgeSource: _badgeSource("reverie"),
        heroSource: _bannerSource(id)
    }
}
