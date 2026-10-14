.pragma library
// Native transport owns persistence, validation and network lifecycle. Bundled values
// are a declared starter snapshot, replaced atomically by validated API documents.
var _reader = null;
var _assetResolver = null;
var _subscribers = [];
var _generation = -1;
function setReader(reader) { _reader = reader; reload(); }
function setAssetResolver(resolver) { _assetResolver = resolver; }
function register(callback) { _subscribers.push(callback); if (_reader) callback(); }
function reload(generation) {
    if (generation !== undefined && generation === _generation) return;
    if (generation !== undefined) _generation = generation;
    for (var i = 0; i < _subscribers.length; ++i) _subscribers[i]();
}
function document(key) {
    // Q_INVOKABLE QVariantMaps contain Qt sequences, which are not JavaScript arrays.
    // Normalize the JSON contract before adapters map/filter or resolve nested assets.
    return _reader ? JSON.parse(JSON.stringify(_reader(key) || {})) : {};
}
function asset(path) { return _assetResolver ? _assetResolver(String(path || "")) : path; }
function resolveAssets(value) {
    if (typeof value === "string") return value.indexOf("../assets/") === 0 ? asset(value) : value;
    if (!value || typeof value !== "object") return value;
    if (Array.isArray(value)) return value.map(resolveAssets);
    var out = {};
    for (var key in value) out[key] = resolveAssets(value[key]);
    return out;
}
