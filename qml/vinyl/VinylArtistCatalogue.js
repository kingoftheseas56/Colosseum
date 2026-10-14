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
