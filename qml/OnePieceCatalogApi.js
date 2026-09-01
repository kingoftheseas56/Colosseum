.pragma library

var KITSU_EPISODES = "https://kitsu.io/api/edge/anime/12/episodes";
var LIVE_ACTION_CINEMETA = "https://v3-cinemeta.strem.io/meta/series/tt11737520.json";
var ANIME_ROOT_ID = "kitsu:12";
var LIVE_ACTION_ROOT_ID = "tt11737520";
var ONE_PACE_EXTENSION_ID = "com.onepace.fedew";
var ONE_PACE_META_ID = "pp_onepace";
var _animeEpisodes = null;
var _liveMeta = null;
var _requestAdapter = null;

function setRequestAdapter(fn) { _requestAdapter = fn || null; }
function resetRequestAdapter() { _requestAdapter = null; }
function resetCache() { _animeEpisodes = null; _liveMeta = null; }

function requestJson(url, done) {
    if (_requestAdapter) { _requestAdapter(url, done); return; }
    var xhr = new XMLHttpRequest();
    xhr.onreadystatechange = function() {
        if (xhr.readyState !== XMLHttpRequest.DONE) return;
        if (xhr.status < 200 || xhr.status >= 300) { done(null); return; }
        try { done(JSON.parse(xhr.responseText)); }
        catch (e) { done(null); }
    };
    xhr.onerror = function() { done(null); };
    xhr.ontimeout = function() { done(null); };
    xhr.open("GET", url);
    xhr.timeout = 10000;
    xhr.send();
}
function numbersFromSpec(spec) {
    var out = [];
    var seen = {};
    var parts = String(spec || "").split(",");
    for (var i = 0; i < parts.length; ++i) {
        var part = parts[i].trim();
        if (!part.length) continue;
        var range = part.split("-");
        var first = Number(range[0]);
        var last = range.length > 1 ? Number(range[1]) : first;
        if (isNaN(first) || isNaN(last)) continue;
        if (last < first) { var swap = first; first = last; last = swap; }
        for (var n = first; n <= last; ++n) {
            if (!seen[n]) { seen[n] = true; out.push(n); }
        }
    }
    return out;
}

function numberSet(spec) {
    var values = numbersFromSpec(spec), out = {};
    for (var i = 0; i < values.length; ++i) out[values[i]] = true;
    return out;
}

function volumeNumbers(arc) {
    return numbersFromSpec(arc && arc.volumes !== undefined ? arc.volumes : arc);
}

function _thumbnail(a) {
    var t = a && a.thumbnail ? a.thumbnail : {};
    return String(t.original || t.large || t.medium || t.small || "");
}

function mapKitsuEpisode(row) {
    var a = row && row.attributes ? row.attributes : {};
    var ep = Number(a.number || 0);
    return {
        id: ANIME_ROOT_ID + ":" + ep,
        rootId: ANIME_ROOT_ID,
        provider: "kitsu",
        season: 1,
        episode: ep,
        title: String(a.canonicalTitle || (a.titles && a.titles.en) || ("Episode " + ep)),
        thumbnail: _thumbnail(a),
        overview: String(a.synopsis || a.description || ""),
        released: String(a.airdate || "")
    };
}

function _maxEpisode(rows) {
    var max = 0;
    for (var i = 0; i < rows.length; ++i)
        max = Math.max(max, Number(rows[i].episode || 0));
    return max;
}

function _loadAnimePage(offset, targetEpisode, all, done) {
    var url = KITSU_EPISODES + "?page%5Blimit%5D=20&page%5Boffset%5D=" + offset;
    requestJson(url, function(json) {
        var rows = json && json.data ? json.data : [];
        for (var i = 0; i < rows.length; ++i) all.push(mapKitsuEpisode(rows[i]));
        if (rows.length && _maxEpisode(all) < targetEpisode)
            _loadAnimePage(offset + 20, targetEpisode, all, done);
        else
            done(all);
    });
}

function _selectAnime(rows, arc) {
    var wanted = numberSet(arc && arc.anime);
    var out = [];
    for (var i = 0; i < rows.length; ++i)
        if (wanted[rows[i].episode]) out.push(rows[i]);
    out.sort(function(a, b) { return a.episode - b.episode; });
    return out;
}

function loadAnimeEpisodes(arc, done) {
    var wanted = numbersFromSpec(arc && arc.anime);
    var targetEpisode = wanted.length ? wanted[wanted.length - 1] : 0;
    if (_animeEpisodes && _maxEpisode(_animeEpisodes) >= targetEpisode) {
        done(_selectAnime(_animeEpisodes, arc));
        return;
    }
    _loadAnimePage(0, targetEpisode, [], function(rows) {
        _animeEpisodes = rows || [];
        done(_selectAnime(_animeEpisodes, arc));
    });
}


function findOnePaceExtension(installedList) {
    for (var i = 0; i < (installedList || []).length; ++i) {
        var ext = installedList[i];
        if (ext && ext.id === ONE_PACE_EXTENSION_ID && ext.enabled === true)
            return ext;
    }
    return null;
}

function selectOnePaceEpisodes(meta, arc) {
    var prefixes = (arc && arc.onePacePrefixes) || [];
    var videos = meta && meta.videos ? meta.videos : [];
    var out = [];
    for (var i = 0; i < videos.length; ++i) {
        var video = videos[i] || ({});
        var id = String(video.id || "");
        var wanted = false;
        for (var j = 0; j < prefixes.length; ++j) {
            if (id.indexOf(String(prefixes[j])) === 0) { wanted = true; break; }
        }
        if (!wanted) continue;
        out.push({
            id: id, provider: "onepace",
            season: Number(video.season || 1), episode: Number(video.episode || 1),
            title: String(video.title || video.name || ("Episode " + video.episode)),
            thumbnail: String(meta.background || meta.poster || ""),
            poster: String(meta.poster || ""), background: String(meta.background || "")
        });
    }
    return out;
}

function loadOnePaceEpisodes(installedList, arc, done) {
    var ext = findOnePaceExtension(installedList);
    if (!ext) { done([], false); return; }
    var transport = String(ext.transportUrl || ext.url || "").replace(/\/manifest\.json(?:[?#].*)?$/i, "");
    if (!transport.length) { done([], false); return; }
    requestJson(transport + "/meta/series/" + ONE_PACE_META_ID + ".json", function(json) {
        var meta = json && json.meta ? json.meta : null;
        done(meta ? selectOnePaceEpisodes(meta, arc) : [], true);
    });
}

function _episodeNumber(v) {
    return Number(v && v.episode !== undefined ? v.episode
                  : (v && v.number !== undefined ? v.number : 0));
}

function _seasonNumber(v) {
    return Number(v && v.season !== undefined ? v.season
                  : (v && v.seasonNumber !== undefined ? v.seasonNumber : 0));
}

function mapCinemetaEpisode(v) {
    var ep = _episodeNumber(v);
    return {
        id: String(v && v.id || (LIVE_ACTION_ROOT_ID + ":" + _seasonNumber(v) + ":" + ep)),
        rootId: LIVE_ACTION_ROOT_ID,
        provider: "cinemeta",
        season: _seasonNumber(v),
        episode: ep,
        title: String(v && (v.name || v.title) || ("Episode " + ep)),
        thumbnail: String(v && (v.thumbnail || v.poster || v.background) || ""),
        overview: String(v && (v.overview || v.description) || ""),
        released: String(v && (v.released || v.releaseInfo) || "")
    };
}

function _selectLiveAction(meta, arc) {
    var season = Number(arc && arc.liveActionSeason || 0);
    var wanted = numberSet(arc && arc.liveActionEpisodes);
    var videos = meta && meta.videos ? meta.videos : [];
    var out = [];
    for (var i = 0; i < videos.length; ++i) {
        if (_seasonNumber(videos[i]) !== season) continue;
        var ep = _episodeNumber(videos[i]);
        if (wanted[ep]) out.push(mapCinemetaEpisode(videos[i]));
    }
    out.sort(function(a, b) { return a.episode - b.episode; });
    return out;
}

function loadLiveActionEpisodes(arc, done) {
    if (_liveMeta) { done(_selectLiveAction(_liveMeta, arc)); return; }
    requestJson(LIVE_ACTION_CINEMETA, function(json) {
        _liveMeta = json && json.meta ? json.meta : null;
        done(_liveMeta ? _selectLiveAction(_liveMeta, arc) : []);
    });
}

function selectVolumes(rows, arc) {
    var wanted = numberSet(arc && arc.volumes);
    var source = rows || [];
    var out = [];
    for (var i = 0; i < source.length; ++i) {
        var n = Number(source[i].number);
        if (!wanted[n]) continue;
        out.push({ number: String(source[i].number),
                   title: String(source[i].name || source[i].title || ("Volume " + n)),
                   cover: String(source[i].cover || "") });
    }
    out.sort(function(a, b) { return Number(a.number) - Number(b.number); });
    return out;
}

function fallbackVolumes(arc) {
    var values = volumeNumbers(arc);
    return values.map(function(n) {
        return { number: String(n), title: "Volume " + n, cover: "" };
    });
}
