import QtQuick
import "VinylArtistCatalogue.js" as Catalogue

QtObject {
    id: root
    property var catalogue: ({albums: []})
    property string name: "Ilaiyaraaja"
    property string filter: "all"
    property bool newestFirst: true
    property bool allSongs: false
    // Deliberately session-only prototype state; production must use library ownership.
    property var savedNames: []
    readonly property var record: Catalogue.artist(catalogue, name)
    readonly property var releases: Catalogue.releases(record, filter, newestFirst)
    readonly property bool saved: savedNames.indexOf(record.name) >= 0
    readonly property var songs: allSongs ? record.songs : record.songs.slice(0, 6)
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
