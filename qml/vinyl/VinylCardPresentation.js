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
function secondary(item, artistText) {
    var values = [];
    if (item.unavailable) values.push("Unavailable");
    if (item.reason) values.push(String(item.reason));
    var subtitle = String(item.subtitle || "");
    if (subtitle && subtitle !== item.reason
        && !(kind(item) === "songs" && subtitle === artistText)) values.push(subtitle);
    return values.join("\n");
}
