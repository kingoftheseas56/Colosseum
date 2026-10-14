import QtQuick
import "VinylArtistCatalogue.js" as Catalogue
import "../../tests/fixtures/vinyl-world/ArtistContext.js" as Context
import "../../tests/fixtures/vinyl-world/ArtistSingles.js" as Singles
import "../../tests/fixtures/vinyl-world/EminemReleases.js" as Eminem

QtObject {
    id: root
    property var catalogue: ({albums: []})
    property string name: "Eminem"
    property string filter: "all"
    property bool newestFirst: true
    property bool allSongs: false
    property url artworkRoot: ""
    property var customPlaylists: []
    // Deliberately session-only prototype state; production must use library ownership.
    property var savedNames: []
    readonly property var record: {
        var albums = (catalogue.albums || []).slice();
        Eminem.data.filter(function(a) { return a.releaseType === "Album"; }).forEach(function(a) {
            if (!albums.some(function(existing) { return existing.id === a.id; })) albums.push(a);
        });
        return Catalogue.artist({albums:albums}, name);
    }
    readonly property var context: Context.forArtist(record.name)
    readonly property var singles: name === "Eminem" ? Eminem.data.filter(function(a) { return a.releaseType !== "Album"; }) : context.singlesArtist ? Singles.data : []
    readonly property var releases: Catalogue.releases(record, filter, newestFirst)
    readonly property bool saved: savedNames.indexOf(record.name) >= 0
    readonly property var songOrder: {
        var first = [], rest = [], seen = {};
        record.songs.forEach(function(s) {
            if (!seen[s.album.id]) { first.push(s); seen[s.album.id] = true; }
            else rest.push(s);
        });
        if (name === "Eminem") {
            var picks = ["Without Me", "Mockingbird", "Not Afraid", "Houdini", "Stan"];
            var chosen = [];
            picks.forEach(function(title) {
                var match = record.songs.find(function(s) { return s.track.title === title || s.track.title.indexOf(title + " (") === 0; });
                if (match) chosen.push(match);
            });
            return chosen.concat(first.concat(rest).filter(function(s) { return chosen.indexOf(s) < 0; }));
        }
        return first.concat(rest);
    }
    readonly property var songs: allSongs ? songOrder : songOrder.slice(0, 5)
    readonly property var playlists: {
        var out = [];
        if (songOrder.length) out.push({name:"Across the albums",owner:"Vinyl selection",artist:record.name,songs:songOrder.slice(0,12)});
        return out.concat(customPlaylists.filter(function(p) { return p.artist === root.record.name; }));
    }
    function artwork(file) {
        return /^https?:\/\//.test(file) ? file : file && artworkRoot ? String(artworkRoot) + "/" + file : "";
    }
    function createPlaylist(title) {
        title = String(title || "").trim();
        if (!title || !songs.length) return false;
        customPlaylists = customPlaylists.concat([{name:title.slice(0,80),owner:"Your playlist",artist:record.name,songs:songs.slice()}]);
        return true;
    }
    readonly property var filters: {
        var out = [{key: "all", label: "All releases"}];
        if (record.releases.some(function(r) { return r.credit === "album"; }))
            out.push({key: "album", label: "Album credits"});
        if (record.releases.some(function(r) { return r.credit === "track"; }))
            out.push({key: "track", label: "Appears on"});
        return out;
    }
    function open(value) {
        name = Catalogue.identity(value);
        filter = "all";
        newestFirst = true;
        allSongs = false;
    }
    function toggleSaved() {
        var next = savedNames.slice(), index = next.indexOf(record.name);
        if (index < 0) next.push(record.name); else next.splice(index, 1);
        savedNames = next;
    }
}
