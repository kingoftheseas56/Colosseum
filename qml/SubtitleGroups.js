.pragma library

function langKey(t) {
    return ("" + ((t && t.lang) || "")).trim().toLowerCase() || "unknown";
}

function langLabel(t) {
    var label = "" + ((t && t.label) || "");
    label = label.replace(/\s*\(.*\)$/, "");
    return label || (t && t.lang) || "Unknown";
}

function groupByLanguage(tracks) {
    var order = [];
    var map = {};
    for (var i = 0; i < (tracks || []).length; i++) {
        var t = tracks[i];
        var key = langKey(t);
        if (!map[key]) {
            map[key] = { key: key, label: key === "unknown" ? "Unknown" : langLabel(t), count: 0 };
            order.push(key);
        }
        map[key].count++;
    }
    var out = [];
    for (var j = 0; j < order.length; j++)
        out.push(map[order[j]]);
    return out;
}

function filterTracks(tracks, opts) {
    opts = opts || {};
    var out = [];
    for (var i = 0; i < (tracks || []).length; i++) {
        var t = tracks[i];
        if (opts.lang && opts.lang !== "__all__" && langKey(t) !== opts.lang)
            continue;
        if (opts.source === "embedded" && t.external)
            continue;
        if (opts.source === "external" && !t.external)
            continue;
        if (opts.hi === false && t.hearingImpaired)
            continue;
        if (opts.forced === true && !t.forced)
            continue;
        out.push(t);
    }
    return out;
}
