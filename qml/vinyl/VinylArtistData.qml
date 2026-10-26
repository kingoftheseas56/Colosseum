import QtQuick
import "VinylArtistCatalogue.js" as Catalogue
import "../../tests/fixtures/vinyl-world/ArtistContext.js" as Context
import "../../tests/fixtures/vinyl-world/ArtistSingles.js" as Singles
import "../../tests/fixtures/vinyl-world/EminemReleases.js" as Eminem

QtObject {
    id: root
    property var catalogue: ({albums: []})
    property var mediaCatalogue: ({})
    property var libraryRecord: ({saved:[]})
    property string name: "Eminem"
    property string filter: "all"
    property string categoryFilter: "All"
    property bool newestFirst: true
    property bool allSongs: false
    property url artworkRoot: ""
    property var customPlaylists: []
    // Deliberately session-only prototype state; production must use library ownership.
    property var savedNames: []

    // HTML-aligned artist destination state (tabs, demo queue, backdrops, reduced motion)
    property string activeTab: "albums"
    property int queuedSongIndex: -1
    property bool demoPlaying: false
    property string selectedWallpaper: "auto"
    property url customBackdropUrl: ""
    property var overrideHeroPhotos: null
    property bool reducedMotion: false

    onFilterChanged: {
        var mapped = (filter === "all" || !filter) ? "All" : filter;
        if (categoryFilter !== mapped)
            categoryFilter = mapped;
    }

    readonly property var record: {
        var albums = (catalogue.albums || []).slice();
        Eminem.data.filter(function(a) { return a.releaseType === "Album"; }).forEach(function(a) {
            if (!albums.some(function(existing) { return existing.id === a.id; })) albums.push(a);
        });
        return Catalogue.artist({albums:albums}, name);
    }
    readonly property var context: Context.forArtist(record.name)
    readonly property var singles: record.name === "Eminem" ? Eminem.data.filter(function(a) { return a.releaseType !== "Album"; }) : context.singlesArtist ? Singles.data : []
    readonly property var releases: Catalogue.releases(record, filter, newestFirst)
    readonly property var releaseHistory: Catalogue.releaseHistory(record, singles, 4)
    readonly property var media: Catalogue.mediaForArtist(mediaCatalogue, record.name)
    readonly property var popularSongs: Catalogue.sourcedPopularSongs(record, media.popularSongs)
    readonly property string popularSource: popularSongs.length ? media.provider : ""
    readonly property var libraryReleases: Catalogue.libraryReleases(record, singles, libraryRecord)
    readonly property var albumCards: Catalogue.releaseCards(releases)
    readonly property var singleCards: Catalogue.releaseCards(singles)
    readonly property var libraryCards: Catalogue.releaseCards(libraryReleases)
    readonly property bool saved: savedNames.indexOf(record.name) >= 0

    // Approved HTML 27-release discography, Theatre dropdown categories, 43 compact singles, live & hero photos
    readonly property var releaseCategories: Catalogue.releaseCategoriesForArtist(record)
    readonly property string currentCategoryLabel: {
        var cats = releaseCategories || [];
        for (var i = 0; i < cats.length; ++i) {
            if (cats[i].id === categoryFilter || cats[i].key === filter)
                return cats[i].label;
        }
        return "All releases";
    }
    readonly property var allDiscographyCards: Catalogue.discographyCardsForArtist(record, singles, "All", newestFirst)
    readonly property var discographyCards: Catalogue.discographyCardsForArtist(record, singles, categoryFilter, newestFirst)
    readonly property var songOrder: {
        var first = [], rest = [], seen = {};
        record.songs.forEach(function(s) {
            if (!seen[s.album.id]) { first.push(s); seen[s.album.id] = true; }
            else rest.push(s);
        });
        // Provider-sourced popular songs are projected separately; unranked
        // catalogue songs use the same collection-first order for every artist.
        return first.concat(rest);
    }
    readonly property var songs: allSongs ? songOrder : songOrder.slice(0, 5)
    readonly property var songCards: songOrder.map(function(song) {
        return {title:song.track.title, subtitle:song.album.title,
            cover:song.album.cover, song:song};
    })
    readonly property var singlesRows: Catalogue.singlesRowsForArtist(record, singles, songOrder)
    readonly property var liveCards: Catalogue.curatedLiveForArtist(record.name, media)
    readonly property var backdropOptions: Catalogue.backdropsForArtist(record.name, media, overrideHeroPhotos)
    readonly property var heroPhotos: Catalogue.autoRotatePhotos(backdropOptions)
    readonly property var queuedSong: (queuedSongIndex >= 0 && queuedSongIndex < singlesRows.length) ? singlesRows[queuedSongIndex] : null

    // Only user-created session playlists belong here. Provider playlists have
    // their own sourced records and must not be synthesized from selected songs.
    readonly property var playlists: customPlaylists.filter(function(p) { return p.artist === root.record.name; })

    function artwork(file) {
        if (!file) return "";
        var s = String(file);
        if (/^(https?:|file:|qrc:|data:)/.test(s)) return s;
        return artworkRoot ? String(artworkRoot) + "/" + s : "";
    }

    function selectReleaseCategory(catId) {
        var nextCat = String(catId || "All");
        categoryFilter = nextCat;
        filter = (nextCat === "All" || nextCat === "all") ? "all" : nextCat;
    }

    function queueDemoSong(index) {
        var total = singlesRows.length;
        if (total <= 0) return;
        queuedSongIndex = ((index % total) + total) % total;
        demoPlaying = true;
    }

    function toggleDemoPlayback() {
        if (queuedSongIndex < 0 && singlesRows.length > 0) {
            queuedSongIndex = 0;
            demoPlaying = true;
            return;
        }
        demoPlaying = !demoPlaying;
    }

    function stepDemoSong(delta) {
        var total = singlesRows.length;
        if (total <= 0) return;
        var base = queuedSongIndex < 0 ? 0 : queuedSongIndex + delta;
        queuedSongIndex = ((base % total) + total) % total;
        demoPlaying = true;
    }

    function shuffleDemoSong() {
        var total = singlesRows.length;
        if (total <= 0) return;
        queuedSongIndex = Math.floor(Math.random() * total);
        demoPlaying = true;
    }

    function search(query) {
        return Catalogue.searchArtistCatalogue(allDiscographyCards, singlesRows, media.videos, liveCards, query);
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
        categoryFilter = "All";
        newestFirst = true;
        allSongs = false;
        activeTab = "albums";
        queuedSongIndex = -1;
        demoPlaying = false;
        selectedWallpaper = "auto";
        customBackdropUrl = "";
    }

    function toggleSaved() {
        var next = savedNames.slice(), index = next.indexOf(record.name);
        if (index < 0) next.push(record.name); else next.splice(index, 1);
        savedNames = next;
    }
}
