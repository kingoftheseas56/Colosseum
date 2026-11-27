// Presentation adapters for the existing Vinyl item contract.
function kind(item) {
    var value = String(item.kind || (item.song ? "songs" : ""));
    return ["songs", "saved-song", "resume"].indexOf(value) >= 0 ? "songs"
        : value === "stations" ? "radio" : value;
}
function names(values) {
    var result = [];
    for (var index = 0; values && index < values.length; ++index) {
        var value = values[index];
        if (!value) continue;
        var name = String(typeof value === "string" ? value : value.name || value.title || "").trim();
        if (name) result.push(name);
    }
    return result.join(" \u00b7 ");
}
function artist(item) {
    if (item.artist) return String(item.artist);
    var song = item.song || {}, album = song.album || item.record || {};
    var track = song.track || (album.tracks || [])[item.trackIndex] || {};
    return names(track.artists) || names(item.artistRefs) || names(album.artists)
        || String(item.row && item.row.artist || "");
}
function albumDetail(item, artistText) {
    var record = item.record || item.album || {};
    var hints = item.hints || {};
    var year = String(item.year || record.year || hints.year || "");
    if (!year) return artistText || String(item.subtitle || "");
    return artistText ? artistText + " \u00b7 " + year : year;
}
function isCurrentSong(item, session) {
    if (!session || !session.playingTrack) return false;
    var playing = session.playingTrack;
    var track = item.song && item.song.track || item.track
        || ((item.record || {}).tracks || [])[item.trackIndex] || {};
    var id = item.trackId || track.id || item.id;
    var videoId = item.videoId || track.videoId;
    return (!!id && String(id) === String(playing.id || ""))
        || (!!videoId && String(videoId) === String(playing.videoId || ""));
}
function secondary(item, artistText) {
    var values = [];
    if (item.unavailable) values.push("Unavailable");
    if (item.reason) values.push(String(item.reason));
    var subtitle = String(item.subtitle || "");
    if (subtitle && subtitle !== item.reason
        && !(kind(item) === "songs" && subtitle === artistText)) values.push(subtitle);
    return values.join("\n");
}
