.pragma library
.import "UniverseContentApi.js" as ContentApi

function slugFor(name) {
    return String(name || "").normalize("NFD").toLowerCase().replace(/[\u0300-\u036f]/g, "")
        .replace(/[^a-z0-9]+/g, "-").replace(/^-+|-+$/g, "");
}

function list(value) { return Array.isArray(value) ? value : []; }
function names(value) { return list(value).filter(function(name) { return typeof name === "string" && name.trim(); }); }
function httpsUrl(value) {
    return /^https:\/\/[a-z0-9][a-z0-9.-]*(?::[0-9]+)?(?:[/?#][^\s\\]*)?$/i.test(value || "") ? value : "";
}
function validId(value, pattern) {
    return typeof value === "string" && pattern.test(value) ? value : "";
}

function song(row, parent, id, title, subtitle) {
    const videoId = validId(row.youtubeVideoId, /^[a-zA-Z0-9_-]{11}$/);
    const listenUrl = httpsUrl(row.listenUrl || parent.listenUrl)
        || (videoId ? "https://www.youtube.com/watch?v=" + videoId : "");
    return {
        key: "music:" + id, medium: "music", title: title, subtitle: subtitle,
        artists: names(row.artists || parent.artists || parent.composers),
        cover: httpsUrl(row.coverUrl || parent.coverUrl),
        videoId: videoId, listenUrl: listenUrl, resolved: !!listenUrl
    };
}

function audioShelves(document) {
    const books = [], albums = [], tracks = [], openings = [], endings = [];
    for (const row of list(document.audiobooks)) {
        if (!row || !row.id || !row.title) continue;
        const workId = validId(row.workId, /^[a-zA-Z0-9:_-]{1,200}$/);
        const narrators = names(row.narrators);
        const listenUrl = httpsUrl(row.listenUrl);
        books.push({
            key: "audiobook:" + row.id, medium: "audiobook", title: row.title,
            subtitle: narrators.length ? narrators.join(", ") : names(row.authors).join(", "),
            cover: httpsUrl(row.coverUrl), workId: workId,
            recordingId: validId(row.recordingId, /^[a-zA-Z0-9:_-]{1,200}$/),
            listenUrl: listenUrl, resolved: !!(workId || listenUrl)
        });
    }
    for (const row of list(document.soundtrackReleases)) {
        if (!row || !row.id || !row.title) continue;
        const browseId = validId(row.youtubeMusicBrowseId, /^MPREb_[a-zA-Z0-9_-]+$/);
        const playlistId = validId(row.youtubePlaylistId, /^[a-zA-Z0-9_-]{10,100}$/);
        const listenUrl = httpsUrl(row.listenUrl)
            || (browseId ? "https://music.youtube.com/browse/" + browseId
                : playlistId ? "https://www.youtube.com/playlist?list=" + playlistId : "");
        const albumTracks = list(row.tracks).filter(function(track) { return track && track.title; })
            .map(function(track, index) {
                return song(track, { artists: row.artists || row.composers, coverUrl: row.coverUrl },
                    "score:" + row.id + ":" + index, track.title, row.title);
            });
        albums.push({
            key: "music:album:" + row.id, medium: "music", title: row.title,
            subtitle: [row.screenWorkTitle, names(row.artists || row.composers).join(", ")].filter(Boolean).join(" · "),
            cover: httpsUrl(row.coverUrl), browseId: browseId, listenUrl: listenUrl,
            tracks: albumTracks, resolved: !!listenUrl
        });
        // Vinyl resolves album tracks for a pinned album. Surface researched
        // tracks individually when the provider has no album identity.
        if (!browseId)
            for (const track of albumTracks)
                if (track.resolved) tracks.push(track);
    }
    for (const row of list(document.themes)) {
        if (!row || !row.id || !row.title || (row.role !== "opening" && row.role !== "ending")) continue;
        const versions = list(row.versions).filter(Boolean);
        const destination = row.role === "opening" ? openings : endings;
        const subtitle = [row.screenWorkTitle, names(row.artists).join(", ")].filter(Boolean).join(" · ");
        for (const [index, version] of (versions.length ? versions : [row]).entries()) {
            const title = version.edit ? row.title + " (" + version.edit + ")" : row.title;
            destination.push(song(version, row, row.role + ":" + row.id + ":" + index, title, subtitle));
        }
    }
    return [
        { key: "audiobooks", kind: "audiobook", title: "Audiobooks", runs: [{ label: "", works: books }] },
        { key: "music", kind: "music", title: "Music", runs: [
            { label: "Soundtracks", works: albums }, { label: "Soundtrack tracks", works: tracks },
            { label: "Openings", works: openings }, { label: "Endings", works: endings }
        ] }
    ].map(function(shelf) {
        shelf.runs = shelf.runs.filter(function(run) { return run.works.length > 0; });
        return shelf;
    }).filter(function(shelf) { return shelf.runs.length > 0; });
}

function shelvesFor(name) {
    const slug = slugFor(name);
    return slug ? audioShelves(ContentApi.document("audio-" + slug)) : [];
}

function vinylItem(item) {
    if (!item || item.medium !== "music") return null;
    if (item.browseId)
        return { destination: { kind: "album", provider: "youtube-music", providerId: item.browseId } };
    if (!item.videoId) return null;
    const id = "ytm:song:" + item.videoId;
    return {
        destination: { kind: "song", provider: "youtube-music", providerId: item.videoId },
        trackIndex: 0,
        record: {
            id: id, type: "Song", origin: "youtube-music", title: item.title, artists: item.artists,
            cover: item.cover,
            tracks: [{ id: id, videoId: item.videoId, title: item.title, artists: item.artists, available: true }]
        }
    };
}
