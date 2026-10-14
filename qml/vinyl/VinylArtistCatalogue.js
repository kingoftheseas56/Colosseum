.pragma library

// Prototype projection of the frozen catalogue, not an identity store. Only
// spelling variants are folded; the snapshot's broad people aliases are unsafe.
function identity(name) {
    var value = String(name || "").trim();
    var aliases = {
        "Ilayaraja": "Ilaiyaraaja",
        "A.R. Rahman": "A. R. Rahman",
        "A.R.Rahman": "A. R. Rahman",
        "S.P. Balasubrahmanyam": "S. P. Balasubrahmanyam",
        "S.P.Balasubrahmanyam": "S. P. Balasubrahmanyam",
        "S.P.Balasubramaniam": "S. P. Balasubrahmanyam",
        "S.P.Balasubramanyam": "S. P. Balasubrahmanyam"
    };
    return aliases[value] || value;
}

function contains(names, name) {
    return (names || []).some(function(n) { return identity(n) === name; });
}

function artist(catalogue, requestedName) {
    var name = identity(requestedName);
    var releases = [], songs = [], credits = {};
    (catalogue.albums || []).forEach(function(album) {
        var own = contains(album.artists, name);
        var contributed = false;
        (album.tracks || []).forEach(function(track, index) {
            var credited = contains(track.artists, name);
            contributed = contributed || credited;
            if (!own && !credited) return;
            songs.push({album: album, track: track, index: index});
            (track.artists || []).forEach(function(other) {
                var key = identity(other);
                if (key && key !== name) credits[key] = true;
            });
        });
        if (own || contributed)
            releases.push({album: album, credit: own ? "album" : "track"});
    });
    return {name: name, releases: releases, songs: songs,
        collaborators: Object.keys(credits).sort()};
}

function releases(record, filter, newestFirst) {
    return record.releases.filter(function(r) {
        return filter === "all" || r.credit === filter;
    }).sort(function(a, b) {
        var delta = Number(a.album.year || 0) - Number(b.album.year || 0);
        return (newestFirst ? -delta : delta) || a.album.title.localeCompare(b.album.title);
    });
}

// A small, deterministic cross-section of years represented in this collection.
// These are release records, not claims about an artist's career milestones.
function releaseHistory(record, singles, limit) {
    var byYear = {};
    var candidates = (record.releases || []).concat((singles || []).map(function(album) {
        return {album:album, credit:"single"};
    }));
    var priority = {album:2, single:1, track:0};
    candidates.forEach(function(release) {
        var year = Number(release.album.year);
        if (!Number.isInteger(year) || year <= 0) return;
        var current = byYear[year];
        if (!current || priority[release.credit] > priority[current.credit]
                || release.credit === current.credit && release.album.title.localeCompare(current.album.title) < 0)
            byYear[year] = release;
    });
    var years = Object.keys(byYear).map(Number).sort(function(a, b) { return a - b; });
    var count = Math.min(Math.max(0, limit === undefined ? 4 : limit), years.length);
    var selected = [];
    for (var i = 0; i < count; ++i) {
        var index = count === 1 ? 0 : Math.round(i * (years.length - 1) / (count - 1));
        selected.push(byYear[years[index]]);
    }
    return selected;
}

// Optional, provider-qualified presentation records. Absence is not a request
// to infer a recommendation, a live recording, or an artist-owned playlist.
function mediaForArtist(catalogue, name) {
    var source = (catalogue || {})[name];
    var result = {portrait:{cover:""}, videos:[], livePerformances:[],
        featuredPlaylists:[], artistPlaylists:[], relatedArtists:[], provider:"", sourceUrl:""};
    function sourced(value) {
        return value && typeof value.sourceUrl === "string" && /^https:\/\/[^\s/]+\//.test(value.sourceUrl);
    }
    if (!sourced(source) || typeof source.name !== "string" || identity(source.name) !== name
            || typeof source.provider !== "string" || !source.provider.trim())
        return result;
    result.provider = source.provider;
    result.sourceUrl = source.sourceUrl;
    if (sourced(source.portrait) && typeof source.portrait.cover === "string" && source.portrait.cover)
        result.portrait = {cover:source.portrait.cover, sourceUrl:source.portrait.sourceUrl};
    ["videos", "livePerformances", "featuredPlaylists", "artistPlaylists", "relatedArtists"].forEach(function(key) {
        var seen = Object.create(null);
        result[key] = (Array.isArray(source[key]) ? source[key] : []).filter(function(item) {
            if (!sourced(item) || typeof item.id !== "string" || !item.id.trim()
                    || typeof item.title !== "string" || !item.title.trim() || seen[item.id]) return false;
            seen[item.id] = true;
            return true;
        }).map(function(item) {
            return {id:item.id, title:item.title, cover:typeof item.cover === "string" ? item.cover : "",
                subtitle:typeof item.subtitle === "string" && item.subtitle ? item.subtitle : source.provider, provider:source.provider,
                sourceUrl:item.sourceUrl, snapshotAt:source.snapshotAt || "", kind:key};
        });
    });
    return result;
}

function releaseCards(records) {
    return (records || []).map(function(record) {
        var album = record.album || record;
        var type = record.credit === "track" ? "Appears on" : album.releaseType || "Album";
        var year = Number(album.year);
        return {id:album.id, title:album.title, cover:album.cover || "", album:album,
            subtitle:[Number.isInteger(year) && year > 0 ? String(year) : "", type].filter(Boolean).join(" · ")};
    });
}

function libraryReleases(record, singles, profile) {
    var saved = Array.isArray((profile || {}).saved) ? profile.saved : [];
    var records = (record.releases || []).concat((singles || []).map(function(album) { return {album:album}; }));
    return records.filter(function(release, index) {
        return saved.indexOf(release.album.id) >= 0
            && records.findIndex(function(other) { return other.album.id === release.album.id; }) === index;
    });
}
