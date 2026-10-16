import QtQuick
import "VinylArtistCatalogue.js" as Catalogue

// Presentation projection only. Canonical records, queue and playback belong to
// the existing local source / Collection / VinylLiveSession owners.
QtObject {
    id: root
    property var session: null
    property string name: ""
    property string artistId: ""
    property string filter: "all"
    property string categoryFilter: "All"
    property bool newestFirst: true
    property bool allSongs: false
    property string activeTab: "albums"
    property string selectedWallpaper: "auto"
    property url customBackdropUrl: ""
    property bool reducedMotion: false
    readonly property bool liveMode: true
    readonly property bool saveEnabled: false
    readonly property bool playlistEnabled: false
    readonly property bool saved: false
    readonly property var playlists: []
    readonly property string error: session ? (session.error || session.browseError || "") : "Local audio is unavailable."
    readonly property var albums: ((session && session.library) || []).map(function(entry) {
        return entry.payload && entry.payload.album ? entry.payload.album : entry;
    })
    readonly property var record: {
        var releases = [], songs = [];
        albums.forEach(function(album) {
            var own = artistId ? ((album.artistIds || []).indexOf(artistId) >= 0 || (album.artist && album.artist.id === artistId))
                              : (album.artists || []).indexOf(name) >= 0;
            var contributed = false;
            var edition = album.editions && album.editions.length ? album.editions[0] : album;
            var tracks = (edition.tracks || []).slice().sort(function(a,b) {
                return Math.max(1, Number(a.disc) || 1) - Math.max(1, Number(b.disc) || 1);
            });
            var discNumbers = [], positions = {};
            tracks.forEach(function(track, index) {
                var disc = Math.max(1, Number(track.disc) || 1);
                if (discNumbers.indexOf(disc) < 0) discNumbers.push(disc);
                var trackIndex = positions[disc] || 0;
                positions[disc] = trackIndex + 1;
                if (!own && (artistId || (track.artists || []).indexOf(name) < 0)) return;
                contributed = true;
                songs.push({album:album, track:track, index:index, editionId:edition.id || album.id,
                    discIndex:discNumbers.indexOf(disc), trackIndex:trackIndex});
            });
            if (own || contributed) releases.push({album:album, credit:own ? "album" : "track"});
        });
        return {id:artistId, name:name, releases:releases, songs:songs, collaborators:[]};
    }
    readonly property var artist: {
        for (var i = 0; i < record.releases.length; ++i) {
            var value = record.releases[i].album.artist;
            if (value && typeof value === "object" && value.name === name) return value;
        }
        return {};
    }
    readonly property var context: ({sources:[], facts:[], roles:"Local artist", biography:artist.biography || ""})
    readonly property var releases: record.releases.filter(function(r) {
        return !r.album.kind || r.album.kind === "album";
    })
    readonly property var singles: record.releases.filter(function(r) { return r.album.kind === "single"; }).map(function(r) { return r.album; })
    // Shared prototype cards do not know the canonical local kind field.
    // Keep the displayed subtitle and accessible description consistent with
    // the category used for filtering, without changing non-live projections.
    function releaseCards(records) {
        return Catalogue.releaseCards(records).map(function(card) {
            var localTypes = {album:"Album", single:"Single", video:"Video", live:"Live"};
            card.category = card.category === "Appears on" ? card.category
                : card.album.category || card.album.releaseType || localTypes[card.album.kind] || "Album";
            card.subtitle = [card.year ? String(card.year) : "", card.category].filter(Boolean).join(" · ");
            return card;
        });
    }
    readonly property var albumCards: releaseCards(releases)
    readonly property var singleCards: releaseCards(singles)
    readonly property var allDiscographyCards: releaseCards(releases.concat(singles.map(function(a) { return {album:a}; })))
    readonly property var releaseCategories: {
        var cats = [{id:"All", key:"all", label:"All releases", count:allDiscographyCards.length}];
        var seen = {};
        allDiscographyCards.forEach(function(card) { seen[card.category] = (seen[card.category] || 0) + 1; });
        Object.keys(seen).sort().forEach(function(key) { cats.push({id:key, key:key, label:key, count:seen[key]}); });
        return cats;
    }
    readonly property var discographyCards: allDiscographyCards.filter(function(card) {
        return categoryFilter === "All" || card.category === categoryFilter;
    }).sort(function(a,b) { return (newestFirst ? -1 : 1) * (a.year-b.year) || a.title.localeCompare(b.title); })
    readonly property string currentCategoryLabel: categoryFilter === "All" ? "All releases" : categoryFilter
    readonly property var filters: [{key:"all", label:"All releases"}]
    readonly property var libraryReleases: record.releases
    readonly property var libraryCards: releaseCards(libraryReleases)
    readonly property var releaseHistory: Catalogue.releaseHistory(record, [], 4)
    readonly property var songOrder: record.songs
    readonly property var songs: allSongs ? songOrder : songOrder.slice(0,5)
    readonly property var popularSongs: []
    readonly property string popularSource: ""
    readonly property var songCards: songOrder.map(function(song) {
        return {title:song.track.title, subtitle:song.album.title, cover:song.album.cover || "", song:song};
    })
    // Preserve the approved Singles track-list contract without curated fixtures.
    readonly property var singlesRows: songOrder.map(function(song,index) {
        return {rowIndex:index, number:("0" + (index+1)).slice(-2), title:song.track.title,
            subtitle:song.album.title, albumTitle:song.album.title, year:song.album.year || "",
            cover:song.album.cover || "", playable:song.track.available !== false,
            album:song.album, track:song.track, index:song.index, song:song};
    })
    function mediaCards(kind) {
        return record.releases.filter(function(r) { return r.album.kind === kind; }).map(function(r) {
            var a = r.album;
            return {id:a.id, title:a.title, cover:a.cover || "", subtitle:"Local " + kind,
                provider:"Local media", sourceUrl:"", snapshotAt:"", kind:kind, album:a,
                playable:(a.tracks || []).some(function(t) { return t.available !== false; })};
        });
    }
    readonly property var media: ({portrait:{cover:artist.cover || ""}, videos:mediaCards("video"),
        livePerformances:mediaCards("live"), relatedArtists:[], popularSongs:[], featuredPlaylists:[], artistPlaylists:[], provider:"", sourceUrl:""})
    readonly property var liveCards: media.livePerformances
    readonly property var backdropOptions: (artist.backdrops || []).map(function(photo,index) {
        return {id:"local-" + index, name:"Photo " + (index+1), era:"Local artwork", cover:photo,
            fallbackCover:record.releases.length ? record.releases[0].album.cover || "" : "",
            focalX:0.5, focalY:0.4, autoRotate:true};
    })
    readonly property var heroPhotos: backdropOptions
    readonly property int queuedSongIndex: {
        var trackId = session && session.playingTrack ? session.playingTrack.id : "";
        if (!trackId) return -1;
        for (var i = 0; i < singlesRows.length; ++i) {
            var song = singlesRows[i].song;
            // A file ID identifies audio, not a release-track occurrence. The same
            // audio may appear twice on a disc or in several editions.
            if (song.track.id === trackId && session.playingEdition
                    && (session.playingEdition.albumId || session.playingEdition.id) === song.album.id
                    && session.playingEdition.id === song.editionId
                    && session.playingDisc === song.discIndex
                    && session.playingIndex === song.trackIndex) return i;
        }
        return -1;
    }
    readonly property var queuedSong: queuedSongIndex >= 0 ? singlesRows[queuedSongIndex] : null
    readonly property bool demoPlaying: queuedSongIndex >= 0 && session && !session.paused && !session.error
    readonly property real playbackFraction: session && session.duration > 0 ? Math.max(0,Math.min(1,session.position/session.duration)) : 0
    readonly property bool playable: singlesRows.some(function(row) { return row.playable; })
    function artwork(url) { return url ? String(url) : ""; }
    function selectReleaseCategory(category) { categoryFilter = category || "All"; filter = categoryFilter === "All" ? "all" : categoryFilter; }
    function search(query) { return Catalogue.searchArtistCatalogue(allDiscographyCards, singlesRows, media.videos, liveCards, query); }
    function open(value) {
        var nextName = typeof value === "object" ? value.name || "" : String(value || "");
        var nextId = typeof value === "object" ? value.id || "" : "";
        if (name === nextName && artistId === nextId) return;
        name = nextName; artistId = nextId;
        filter = "all"; categoryFilter = "All"; newestFirst = true; allSongs = false;
        activeTab = "albums"; selectedWallpaper = "auto"; customBackdropUrl = "";
    }
    function queueDemoSong(index) {
        var row = singlesRows[index];
        if (session && row && row.playable) session.playAlbum(row.album, row.index, row.song.editionId);
    }
    function toggleDemoPlayback() {
        if (!session) return;
        if (queuedSongIndex >= 0) session.togglePause();
        else for (var i=0; i<singlesRows.length; ++i) if (singlesRows[i].playable) { queueDemoSong(i); return; }
    }
    function stepDemoSong(delta) { if (session) session.next(delta); }
    function shuffleDemoSong() {
        if (session && playable) session.playSongs(songOrder.filter(function(s) { return s.track.available !== false; }), true);
    }
    function toggleSaved() { return false; }
    function createPlaylist(title) { return false; }
}
