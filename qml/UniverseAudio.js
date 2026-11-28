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

function englishTitle(row) {
    return typeof row.englishTitle === "string" ? row.englishTitle.trim() : "";
}

function sourceConfirmed(row, provider, id) {
    const check = row.availability;
    return !!id && !!check && check.provider === provider && check.identity === id
        && validId(check.sourceId, /^[a-zA-Z0-9:_-]{1,200}$/) !== ""
        && (provider !== "youtube-music" || check.sourceId === id)
        && /^\d{4}-\d{2}-\d{2}$/.test(check.checkedOn || "");
}

function isEnglishRecording(row) {
    return !!englishTitle(row) && /^(?:en(?:[-_][a-z]{2})?|english)$/i.test(row.language || "")
        && !!validId(row.workId, /^[a-zA-Z0-9:_-]{1,200}$/)
        && sourceConfirmed(row, "audiobookbay", validId(row.recordingId, /^[a-zA-Z0-9:_-]{1,200}$/));
}

function isScreenRecording(row) {
    return !!englishTitle(row) && /^tt\d{7,}$/.test(row.screenWorkId || "");
}

function hasAlbum(row) {
    return sourceConfirmed(row, "youtube-music", validId(row.youtubeMusicBrowseId, /^MPREb_[a-zA-Z0-9_-]+$/));
}

function hasVideo(row) {
    return sourceConfirmed(row, "youtube-music", validId(row.youtubeVideoId, /^[a-zA-Z0-9_-]{11}$/));
}

function song(row, parent, id, title, subtitle) {
    if (!title || !hasVideo(row)) return null;
    const videoId = row.youtubeVideoId;
    return {
        key: "music:" + id, medium: "music", title: title, subtitle: subtitle,
        artists: names(row.artists || parent.artists || parent.composers),
        cover: httpsUrl(row.coverUrl || parent.coverUrl),
        videoId: videoId, listenUrl: "https://www.youtube.com/watch?v=" + videoId
    };
}

function audioShelves(document) {
    const books = [], albums = [], tracks = [], openings = [], endings = [];
    for (const row of list(document.audiobooks)) {
        if (!row || !row.id || !isEnglishRecording(row)) continue;
        const narrators = names(row.narrators);
        books.push({
            key: "audiobook:" + row.id, medium: "audiobook", title: englishTitle(row),
            subtitle: narrators.length ? narrators.join(", ") : names(row.authors).join(", "),
            cover: httpsUrl(row.coverUrl), workId: row.workId, recordingId: row.recordingId
        });
    }
    for (const row of list(document.soundtrackReleases)) {
        if (!row || !row.id || !isScreenRecording(row)) continue;
        const albumAvailable = hasAlbum(row);
        const browseId = row.youtubeMusicBrowseId;
        const albumTracks = list(row.tracks).filter(Boolean)
            .map(function(track, index) {
                return song(track, { artists: row.artists || row.composers, coverUrl: row.coverUrl },
                    "score:" + row.id + ":" + index, englishTitle(track), englishTitle(row));
            }).filter(Boolean);
        if (albumAvailable) albums.push({
            key: "music:album:" + row.id, medium: "music", title: englishTitle(row),
            subtitle: [row.screenWorkEnglishTitle, names(row.artists || row.composers).join(", ")].filter(Boolean).join(" · "),
            cover: httpsUrl(row.coverUrl), browseId: browseId, listenUrl: "https://music.youtube.com/browse/" + browseId,
            tracks: albumTracks
        });
        else tracks.push.apply(tracks, albumTracks);
    }
    for (const row of list(document.themes)) {
        if (!row || !row.id || !isScreenRecording(row)
                || (row.role !== "opening" && row.role !== "ending")) continue;
        const versions = list(row.versions).filter(Boolean);
        const destination = row.role === "opening" ? openings : endings;
        const subtitle = [row.screenWorkEnglishTitle, names(row.artists).join(", ")].filter(Boolean).join(" · ");
        for (const [index, version] of (versions.length ? versions : [row]).entries()) {
            const title = version.edit ? englishTitle(row) + " (" + version.edit + ")" : englishTitle(row);
            const item = song(version, row, row.role + ":" + row.id + ":" + index, title, subtitle);
            if (item) destination.push(item);
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

function shelvesFor(name, musicAvailable) {
    const slug = slugFor(name);
    const shelves = slug ? audioShelves(ContentApi.document("audio-" + slug)) : [];
    return musicAvailable === false ? shelves.filter(shelf => shelf.kind !== "music") : shelves;
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
