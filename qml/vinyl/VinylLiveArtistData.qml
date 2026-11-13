import QtQuick
import "VinylArtistCatalogue.js" as Catalogue

// Presentation projection only. Canonical records, queue and playback belong to
// the existing local source / Collection / VinylLiveSession owners.
Item {
    id: root
    property var session: null
    property var catalogueService: session && session.source && typeof session.source.openArtist === "function" ? session.source : null
    property bool online: false
    readonly property var onlineArtist: online && catalogueService ? catalogueService.artist : ({})
    readonly property bool loading: !!online && !!catalogueService
        && (catalogueService.artistCatalogueBusy === undefined ? catalogueService.artistBusy === true
                                                               : catalogueService.artistCatalogueBusy === true)
    readonly property string catalogueError: online && catalogueService ? catalogueService.artistError || "" : ""
    readonly property string catalogueStatus: loading ? qsTr("Loading artist catalogue…")
        : catalogueError || (online && catalogueService && catalogueService.cached ? qsTr("Showing cached artist catalogue.") : "")
    property string name: ""
    property string artistId: ""
    property string openingCover: ""
    property string filter: "all"
    property string categoryFilter: "All"
    property bool newestFirst: true
    property bool allSongs: false
    property string activeTab: "albums"
    property string selectedWallpaper: "auto"
    property url customBackdropUrl: ""
    property bool reducedMotion: false
    readonly property bool liveMode: true
    property var collectionStore: session && session.collectionStore ? session.collectionStore : null
    onCollectionStoreChanged: { personalError=""; lastCreatedPlaylistId=""; }
    readonly property string canonicalArtistId: artistId || artist.id || ""
    readonly property bool saveEnabled: !!collectionStore && !!canonicalArtistId && collectionStore.healthy !== false
    readonly property bool playlistEnabled: saveEnabled && songOrder.length > 0
    property string personalError: ""
    property string lastCreatedPlaylistId: ""
    readonly property string persistenceError: personalError || (collectionStore ? collectionStore.persistenceError || "" : "")
    readonly property bool saved: {
        var revision = collectionStore ? collectionStore.revision : 0;
        return !!collectionStore && !!canonicalArtistId && collectionStore.has("Vinyl", canonicalArtistId);
    }
    readonly property var playlists: {
        var revision = collectionStore ? collectionStore.revision : 0;
        if (!collectionStore || !canonicalArtistId || !collectionStore.vinylPlaylists) return [];
        return JSON.parse(JSON.stringify(collectionStore.vinylPlaylists(canonicalArtistId))).map(function(entry) {
            return {id:entry.id, name:entry.title, songs:((entry.payload || {}).occurrences || []).map(resolveOccurrence)};
        });
    }
    function resolveOccurrence(ref) {
        var album = albums.find(function(a) { return a.id === ref.albumId; });
        var edition = album && (album.editions || [album]).find(function(e) { return e.id === ref.editionId; });
        var tracks = edition ? (edition.tracks || []).slice().sort(function(a,b) { return (a.disc || 1)-(b.disc || 1); }) : [];
        var track = tracks[ref.index];
        // Never resolve a different occurrence merely because it shares an audio ID.
        var discs = [], position = 0;
        for (var i=0; i<=ref.index && i<tracks.length; ++i) {
            var disc=tracks[i].disc || 1;
            if(discs.indexOf(disc)<0){discs.push(disc);position=0;} else if(i>0) position++;
        }
        var available = track && track.id === ref.trackId && discs.length-1 === ref.discIndex && position === ref.trackIndex;
        return {album:album || {id:ref.albumId}, track:available ? track : {id:ref.trackId,title:ref.title || "Unavailable track",available:false},
            index:ref.index, editionId:ref.editionId, discIndex:ref.discIndex, trackIndex:ref.trackIndex, unavailable:!available};
    }
    readonly property string error: catalogueStatus || (session ? (session.error || session.browseError || "") : "Local audio is unavailable.")
    readonly property var localAlbums: ((session && session.library) || []).map(function(entry) {
        return entry.payload && entry.payload.album ? entry.payload.album : entry;
    })
    readonly property var albums: online && catalogueService ? catalogueService.artistAlbums.concat(catalogueService.artistSingles).concat(localAlbums.filter(function(a){return a.origin === "local" && (a.artists || []).indexOf(name)>=0;})) : localAlbums
    readonly property var record: {
        var releases = [], songs = [];
        albums.forEach(function(album) {
            // A name-only online route acquires a verified provider ID from
            // the header. Keep the existing local credit rule when combining sources.
            var creditId = online && album.origin !== "local" ? canonicalArtistId : artistId;
            var own = creditId ? ((album.artistIds || []).indexOf(creditId) >= 0 || (album.artist && album.artist.id === creditId))
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
        if(online) return onlineArtist;
        for (var i = 0; i < record.releases.length; ++i) {
            var value = record.releases[i].album.artist;
            if (value && typeof value === "object" && value.name === name) return value;
        }
        return {};
    }
    readonly property var context: ({sources:[], facts:[], roles:online ? "Catalogue artist" : "Local artist", biography:artist.biography || ""})
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
            card.browsable = !!card.album.browseId && card.album.origin === "youtube-music";
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
    readonly property var onlineSongRows: online ? (onlineArtist.songs || []) : []
    readonly property var onlineSongs: {
        var songs = [];
        for (var row of onlineSongRows) {
            var album = row && row.record;
            var track = album && album.tracks && album.tracks.length === 1 ? album.tracks[0] : null;
            if (!album || album.origin !== "youtube-music" || !album.id || !track || !track.id
                    || track.available === false || !/^[A-Za-z0-9_-]{11}$/.test(row.videoId || "")
                    || track.videoId !== row.videoId) continue;
            songs.push({album:album, track:track, index:0, editionId:album.id, discIndex:0, trackIndex:0,
                        rank:row.rank, releaseTitle:row.albumName || ""});
        }
        return songs;
    }
    readonly property var songOrder: onlineSongs.length ? onlineSongs : record.songs
    readonly property int trackCount: online && onlineSongRows.length ? onlineSongRows.length : songOrder.length
    readonly property var songs: allSongs ? songOrder : songOrder.slice(0,5)
    readonly property var popularSongs: onlineSongs
    readonly property string popularSource: onlineSongs.length ? "YouTube Music" : ""
    readonly property var songCards: songOrder.map(function(song) {
        return {title:song.track.title, subtitle:song.album.title, cover:song.album.cover || "", song:song};
    })
    // Preserve the approved Singles track-list contract without curated fixtures.
    readonly property var singlesRows: online ? singles.map(function(a,index){return {rowIndex:index,number:("0"+(index+1)).slice(-2),title:a.title,subtitle:a.year || "",albumTitle:a.title,year:a.year || "",cover:a.cover || "",playable:false,album:a,track:{title:a.title,available:false},index:0,song:{album:a,track:{title:a.title,available:false},index:0}};}) : songOrder.map(function(song,index) {
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
    readonly property var localMedia: ({portrait:{cover:artist.cover || ""}, videos:mediaCards("video"),
        livePerformances:mediaCards("live"), relatedArtists:[], popularSongs:[], featuredPlaylists:[], artistPlaylists:[], provider:"", sourceUrl:""})
    function providerMedia(rows,kind) {
        return (rows || []).map(function(row){return {id:row.videoId,title:row.title,cover:row.thumbnail || "",subtitle:kind === "live" ? "Live performance · catalogue" : "Video · catalogue",provider:"YouTube Music",sourceUrl:"https://music.youtube.com/watch?v="+row.videoId,kind:kind,playable:false};});
    }
    readonly property var media: online && catalogueService ? ({portrait:{cover:""}, videos:providerMedia(catalogueService.artistMedia.videos,"video"),livePerformances:providerMedia(catalogueService.artistMedia.livePerformances,"live"), relatedArtists:(onlineArtist.relatedArtists || []).map(function(a){return {id:"ytm:artist:"+a.id,title:a.name,name:a.name,artistRef:{id:"ytm:artist:"+a.id,name:a.name},cover:a.thumbnail || ""};}),popularSongs:[],featuredPlaylists:[],artistPlaylists:[],provider:"YouTube Music",sourceUrl:"https://music.youtube.com/channel/"+(onlineArtist.channelId || "")}) : localMedia
    readonly property var liveCards: media.livePerformances
    readonly property var backdropOptions: online && catalogueService && catalogueService.artistArtwork
        ? (catalogueService.artistArtwork.photos || []) : (artist.backdrops || []).map(function(photo,index) {
        return {id:"local-" + index, name:"Photo " + (index+1), era:"Local artwork", cover:photo,
            fallbackCover:record.releases.length ? record.releases[0].album.cover || "" : "",
            focalX:0.5, focalY:0.4, autoRotate:true};
    })
    readonly property var heroPhotos: backdropOptions
    readonly property string providerHeroUrl: {
        if (!online) return "";
        var best = "";
        var distance = Infinity;
        for (var image of onlineArtist.images || onlineArtist.thumbnails || []) {
            if (!image || !/^https:\/\//i.test(image.url || "") || image.width < 480
                    || !(image.height > 0 && image.width / image.height >= 1.6)) continue;
            var candidate = Math.abs(image.width - 1920);
            if (candidate < distance) {
                best = image.url;
                distance = candidate;
            }
        }
        return best;
    }
    readonly property var providerHeroPhoto: providerHeroUrl
        ? {id:"provider:"+providerHeroUrl, cover:providerHeroUrl, focalX:0.5, focalY:0.4} : null
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
    readonly property bool playingOnlineArtist: {
        if (!online || !session || !session.playingEdition) return false;
        var edition = session.playingEdition;
        var videoId = session.playingTrack && session.playingTrack.videoId;
        return (edition.origin === "youtube-music" && !!videoId
                && onlineSongs.some(song => song.track.videoId === videoId))
            || albums.some(function(album) {
                return album.id === edition.albumId || (album.providerEditions || []).some(e => e.browseId === edition.browseId);
            });
    }
    readonly property bool demoPlaying: (queuedSongIndex >= 0 || playingOnlineArtist) && session && !session.paused && !session.error
    readonly property real playbackFraction: session && session.duration > 0 ? Math.max(0,Math.min(1,session.position/session.duration)) : 0
    readonly property bool playable: onlineSongs.length > 0 || singlesRows.some(function(row) { return row.playable; }) || (online && albums.some(function(a){return !!a.browseId;}))
    function artwork(url) { return url ? String(url) : ""; }
    function selectReleaseCategory(category) { categoryFilter = category || "All"; filter = categoryFilter === "All" ? "all" : categoryFilter; }
    function search(query) { return Catalogue.searchArtistCatalogue(allDiscographyCards, online ? [] : singlesRows, media.videos, liveCards, query); }
    function open(value) {
        var nextName = value && typeof value === "object" ? value.name || "" : String(value || "");
        var nextId = value && typeof value === "object" ? value.id || "" : "";
        var nextCover = value && typeof value === "object" ? String(value.cover || "") : "";
        if (!/^https:\/\//i.test(nextCover)) nextCover = "";
        if (name === nextName && artistId === nextId) {
            if (nextCover) openingCover = nextCover;
            return;
        }
        openingCover = nextCover;
        online = nextId.indexOf("ytm:artist:")===0 || (!!catalogueService && nextName!=="" && !nextId
            && !localAlbums.some(function(a){return (a.artists || []).indexOf(nextName)>=0;}));
        if(catalogueService) {if(online) catalogueService.openArtist({id:nextId,name:nextName,cover:openingCover});else catalogueService.leaveArtist();}
        name = nextName; artistId = nextId; personalError = ""; lastCreatedPlaylistId = "";
        filter = "all"; categoryFilter = "All"; newestFirst = true; allSongs = false;
        activeTab = "albums"; selectedWallpaper = "auto"; customBackdropUrl = "";
    }
    function queueDemoSong(index) {
        var row = singlesRows[index];
        if (session && row && row.playable) session.playAlbum(row.album, row.index, row.song.editionId);
    }
    function enqueueDemoSong(index,playNext) {
        var row=singlesRows[index];
        return !!(session && row && row.playable && session.enqueueSong(row.album,row.index,row.song.editionId,!!playNext));
    }
    function toggleDemoPlayback() {
        if (!session) return;
        if (online) {
            if (playingOnlineArtist) session.togglePause();
            else if (onlineSongs.length) session.playSongs(onlineSongs, false);
            else if (albums.length) session.playCatalogueAlbum(albums[0].id, false);
            return;
        }
        if (queuedSongIndex >= 0) session.togglePause();
        else for (var i=0; i<singlesRows.length; ++i) if (singlesRows[i].playable) { queueDemoSong(i); return; }
    }
    function stepDemoSong(delta) { if (session) session.next(delta); }
    function shuffleDemoSong() {
        if (online && session) {
            if (onlineSongs.length) session.playSongs(onlineSongs, true);
            else if (albums.length) session.playCatalogueAlbum(albums[0].id, true);
            return;
        }
        if (session && playable) session.playSongs(songOrder.filter(function(s) { return s.track.available !== false; }), true);
    }
    function toggleSaved() {
        if (!saveEnabled) return false;
        var ids=online && catalogueService && catalogueService.artistIdentity.mbid ? {musicbrainz:catalogueService.artistIdentity.mbid} : {};
        var ok = collectionStore.setVinylArtistSaved({id:canonicalArtistId,name:name,externalIds:ids}, !saved);
        personalError = ok ? "" : collectionStore.persistenceError || "Artist could not be saved.";
        return ok;
    }
    function createPlaylist(title) {
        if (!playlistEnabled) return false;
        if(session && typeof session.createPlaylistFromSongs==="function") {
            lastCreatedPlaylistId=session.createPlaylistFromSongs(title,songOrder,canonicalArtistId);
            personalError=lastCreatedPlaylistId ? "" : session.personalError || "Playlist could not be saved.";
            return !!lastCreatedPlaylistId;
        }
        var refs = songOrder.map(function(s) { return {albumId:s.album.id,editionId:s.editionId,trackId:s.track.id,
            discIndex:s.discIndex,trackIndex:s.trackIndex,index:s.index,title:s.track.title}; });
        var id = collectionStore.createVinylPlaylist(title, canonicalArtistId, refs);
        personalError = id ? "" : collectionStore.persistenceError || "Playlist could not be saved. Enter a name.";
        lastCreatedPlaylistId = id || "";
        return !!id;
    }
}
