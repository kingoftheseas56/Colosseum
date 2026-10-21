import QtQuick

// Presentation adapter for Vinyl's personal library. Collection and the live
// session remain the only owners of album, song, playlist, and queue state.
Item {
    id: root
    objectName: "vinylPersonalLibraryData"
    required property var session

    property string section: "albums"
    property string query: ""
    property string sortOrder: "title"
    property string selectedPlaylistId: ""
    property var pendingRows: []
    property string personalError: ""

    readonly property var collectionStore: session && session.collectionStore ? session.collectionStore : null
    readonly property int collectionRevision: collectionStore ? Number(collectionStore.revision) || 0 : 0
    readonly property bool personalEnabled: !!(session && session.personalEnabled)
    readonly property string error: personalError
        || (session ? String(session.personalError || "") : "")
        || (collectionStore ? String(collectionStore.persistenceError || "") : "")

    property var _albumRecords: []
    property var _albumCards: []
    property var _artistCards: []
    property var _savedSongs: []
    property var _playlistRecords: []
    property var _playlists: []
    property var _selectedPlaylist: ({id:"", name:"", title:"", items:[]})
    property var _availableSongs: []

    readonly property var albums: filteredSorted(_albumCards, "album")
    readonly property var artists: filteredSorted(_artistCards, "artist")
    readonly property var savedSongs: filteredSorted(_savedSongs, "song")
    readonly property var playlists: filteredSorted(_playlists, "playlist")
    readonly property var selectedPlaylist: _selectedPlaylist
    readonly property var availableSongs: _availableSongs

    signal albumRequested(string id)
    signal artistRequested(var item)
    signal songRequested(var song)
    signal pickerRequested()

    function searchText(item, kind) {
        if (!item) return "";
        if (kind === "artist")
            return [item.title, item.name, item.artist].join(" ");
        if (kind === "playlist")
            return [item.name, item.title].join(" ");
        if (kind === "album")
            return [item.title, item.artist, (item.artists || []).join(" ")].join(" ");
        return [item.title, item.artist].join(" ");
    }

    function sortTitle(item, kind) {
        if (!item) return "";
        if (kind === "artist") return String(item.name || item.title || item.artist || "");
        if (kind === "playlist") return String(item.name || item.title || "");
        return String(item.title || item.name || "");
    }

    function filteredSorted(items, kind) {
        var needle = String(query || "").trim().toLowerCase();
        var result = (items || []).filter(function(item) {
            return !needle || searchText(item, kind).toLowerCase().indexOf(needle) >= 0;
        });
        result.sort(function(a, b) {
            if (kind === "album" && sortOrder === "year") {
                var yearOrder = (Number(b.year) || 0) - (Number(a.year) || 0);
                if (yearOrder) return yearOrder;
            }
            return sortTitle(a, kind).localeCompare(sortTitle(b, kind));
        });
        return result;
    }

    function sourceEntries() {
        if (!session || !session.profileActive || session.shuttingDown) return [];
        if (collectionStore && typeof collectionStore.items === "function")
            return session.plainData(collectionStore.items("Vinyl")) || [];
        return session && Array.isArray(session.library) ? session.library : [];
    }

    function sourceAlbum(entry) {
        return entry && entry.payload && entry.payload.album ? entry.payload.album : null;
    }

    function isVisibleAlbum(record) {
        var album = record.album || {};
        var isOnline = String(album.origin || "") === "youtube-music";
        return !isOnline || !record.entry.payload || record.entry.payload.saved !== false;
    }

    function matchingArtistAlbum(artistId, artistName) {
        for (var i = 0; i < _albumRecords.length; ++i) {
            var album = _albumRecords[i].album || {};
            var ids = album.artistIds || [];
            var own = artistId ? ids.indexOf(artistId) >= 0
                || (album.artist && String(album.artist.id || "") === artistId)
                : (album.artists || []).indexOf(artistName) >= 0;
            if (own && isVisibleAlbum(_albumRecords[i])) return album;
        }
        return null;
    }

    function buildAlbumCard(record) {
        var album = record.album || {};
        var id = String(album.id || record.entry.id || "");
        return {
            id: id,
            albumId: id,
            title: String(album.title || record.entry.title || "Untitled album"),
            artist: (album.artists || []).join(" · ") || String(record.entry.artist || ""),
            artists: (album.artists || []).slice(),
            year: String(album.year || ""),
            cover: String(album.cover || record.entry.cover || ""),
            album: album
        };
    }

    function buildArtistCard(entry) {
        var id = String(entry.id || (entry.payload || {}).artistId || "");
        var name = String(entry.title || entry.name || (entry.payload || {}).name || "Artist");
        var album = matchingArtistAlbum(id, name);
        return {
            id: id,
            artistId: id,
            title: name,
            name: name,
            artist: name,
            cover: String(entry.cover || (album ? album.cover || "" : "")),
            albumId: album ? String(album.id || "") : ""
        };
    }

    function exactAlbum(albumId) {
        for (var i = 0; i < _albumRecords.length; ++i) {
            var album = _albumRecords[i].album || {};
            if (String(album.id || _albumRecords[i].entry.id || "") === String(albumId || ""))
                return album;
        }
        return null;
    }

    function unresolvedReference(ref) {
        ref = ref || {};
        return {
            row: null,
            album: exactAlbum(ref.albumId),
            index: Number.isInteger(Number(ref.index)) ? Number(ref.index) : -1,
            editionId: String(ref.editionId || ""),
            title: String(ref.title || "Unavailable track"),
            artist: String(ref.artist || ""),
            cover: String(ref.cover || ""),
            seconds: Number(ref.seconds) || 0,
            unavailable: true
        };
    }

    // A persisted ref is resolved only at its original album/edition/disc/track
    // coordinate. Track IDs and the edition's flat index must agree as well.
    function resolveReference(ref) {
        if (!ref || !session || typeof session.normalizeEditions !== "function"
                || typeof session.rowFor !== "function")
            return unresolvedReference(ref);

        var albumId = String(ref.albumId || "");
        var editionId = String(ref.editionId || "");
        var trackId = String(ref.trackId || "");
        var discIndex = Number(ref.discIndex);
        var trackIndex = Number(ref.trackIndex);
        var flatIndex = Number(ref.index);
        if (!albumId || !editionId || !trackId
                || !Number.isInteger(discIndex) || discIndex < 0
                || !Number.isInteger(trackIndex) || trackIndex < 0
                || !Number.isInteger(flatIndex) || flatIndex < 0)
            return unresolvedReference(ref);

        var album = exactAlbum(albumId);
        if (!album) return unresolvedReference(ref);
        var editions = session.normalizeEditions(album) || [];
        var edition = editions.find(function(value) { return String(value.id || "") === editionId; });
        if (!edition || !Array.isArray(edition.discs) || discIndex >= edition.discs.length)
            return unresolvedReference(ref);
        var disc = edition.discs[discIndex] || [];
        var track = disc[trackIndex];
        if (!track || String(track.id || "") !== trackId)
            return unresolvedReference(ref);

        var calculatedIndex = trackIndex;
        for (var d = 0; d < discIndex; ++d)
            calculatedIndex += (edition.discs[d] || []).length;
        if (calculatedIndex !== flatIndex)
            return unresolvedReference(ref);

        var row = session.rowFor(edition, discIndex, trackIndex);
        if (!row || String(row.albumId || "") !== albumId
                || String(row.editionId || "") !== editionId
                || String(row.trackId || "") !== trackId)
            return unresolvedReference(ref);
        row.index = flatIndex;

        var available = track.available !== false;
        if (available && typeof session.personalRowAvailable === "function")
            available = !!session.personalRowAvailable(row);

        return {
            row: available ? row : null,
            album: album,
            index: flatIndex,
            editionId: editionId,
            title: String(track.title || ref.title || "Untitled track"),
            artist: String(track.artist || ref.artist || ""),
            cover: String(edition.cover || ref.cover || album.cover || ""),
            seconds: Number(track.seconds || ref.seconds) || 0,
            unavailable: !available
        };
    }

    // VinylBrowseSongData uses one index for both the source edition's track
    // list and the normalized, disc-major queue. Usually those orders match,
    // but a retained album can contain interleaved disc tracks. Give the song
    // page a shallow album view whose selected edition uses the normalized
    // order, while keeping the stored Collection album untouched.
    function albumInNormalizedTrackOrder(album, editionId) {
        if (!album) return null;
        var editions = Array.isArray(album.editions) && album.editions.length
            ? album.editions : null;
        var editionIndex = -1;
        if (editions) {
            editionIndex = editions.findIndex(function(value) {
                return String(value && (value.id || album.id) || "") === String(editionId || "");
            });
            if (editionIndex < 0) return null;
        } else if (String(album.id || "") !== String(editionId || "")) {
            return null;
        }

        var sourceEdition = editions ? editions[editionIndex] : album;
        var tracks = Array.isArray(sourceEdition.tracks) ? sourceEdition.tracks : [];
        var ordered = tracks.map(function(track, sourceIndex) {
            return {track:track, sourceIndex:sourceIndex,
                disc:Math.max(1, Number(track && track.disc) || 1)};
        });
        ordered.sort(function(a, b) {
            return a.disc - b.disc || a.sourceIndex - b.sourceIndex;
        });
        var sortedTracks = ordered.map(function(value) { return value.track; });
        var alreadyOrdered = sortedTracks.every(function(track, index) {
            return track === tracks[index];
        });
        var editionNeedsId = editions && String(sourceEdition.id || "") !== String(editionId || "");
        if (alreadyOrdered && !editionNeedsId) return album;

        var orderedEdition = Object.assign({}, sourceEdition, {id:String(editionId), tracks:sortedTracks});
        if (!editions) return Object.assign({}, album, {tracks:sortedTracks});
        var orderedEditions = editions.slice();
        orderedEditions[editionIndex] = orderedEdition;
        return Object.assign({}, album, {editions:orderedEditions});
    }

    function savedSongFor(entry) {
        var payload = entry && entry.payload ? entry.payload : {};
        var ref = payload.ref || {};
        var resolved = resolveReference(ref);
        var id = String(entry && entry.id || "");
        return {
            id: id,
            itemId: id,
            title: resolved.title || String(entry.title || "Unavailable track"),
            artist: resolved.artist || String(ref.artist || ""),
            cover: resolved.cover || String(entry.cover || ""),
            seconds: resolved.seconds,
            unavailable: resolved.unavailable,
            ref: ref,
            row: resolved.row,
            album: resolved.album,
            index: resolved.index,
            editionId: resolved.editionId
        };
    }

    function playlistItemsFor(entry) {
        var occurrences = entry && entry.payload && Array.isArray(entry.payload.occurrences)
            ? entry.payload.occurrences : [];
        return occurrences.map(function(ref) {
            var resolved = resolveReference(ref);
            var itemId = String(ref.itemId || "");
            return {
                id: itemId,
                itemId: itemId,
                title: resolved.title || "Unavailable track",
                artist: resolved.artist || String(ref.artist || ""),
                cover: resolved.cover || String(ref.cover || ""),
                seconds: resolved.seconds,
                unavailable: resolved.unavailable,
                ref: ref,
                row: resolved.row,
                album: resolved.album,
                index: resolved.index,
                editionId: resolved.editionId
            };
        });
    }

    function playlistCard(entry) {
        var payload = entry && entry.payload ? entry.payload : {};
        var occurrences = Array.isArray(payload.occurrences) ? payload.occurrences : [];
        var first = occurrences.length ? occurrences[0] : {};
        return {
            id: String(entry.id || ""),
            name: String(entry.title || payload.name || "Playlist"),
            title: String(entry.title || payload.name || "Playlist"),
            count: occurrences.length,
            cover: String(first.cover || entry.cover || "")
        };
    }

    function buildAvailableSongs() {
        var result = [];
        if (!session || typeof session.normalizeEditions !== "function"
                || typeof session.rowFor !== "function")
            return result;

        _albumRecords.forEach(function(record) {
            var album = record.album || {};
            var editions = session.normalizeEditions(album) || [];
            editions.forEach(function(edition) {
                var flatIndex = 0;
                (edition.discs || []).forEach(function(disc, discIndex) {
                    (disc || []).forEach(function(track, trackIndex) {
                        var row = session.rowFor(edition, discIndex, trackIndex);
                        if (row) {
                            row.index = flatIndex;
                            var available = track && track.available !== false;
                            if (available && typeof session.personalRowAvailable === "function")
                                available = !!session.personalRowAvailable(row);
                            if (available) {
                                result.push({
                                    id: JSON.stringify([row.albumId, row.editionId, discIndex, trackIndex, row.trackId]),
                                    itemId: JSON.stringify([row.albumId, row.editionId, discIndex, trackIndex, row.trackId]),
                                    title: String(row.title || (track && track.title) || "Untitled track"),
                                    artist: String(row.artist || ""),
                                    cover: String(row.cover || ""),
                                    seconds: Number(row.seconds) || 0,
                                    unavailable: false,
                                    row: row,
                                    album: album,
                                    index: flatIndex,
                                    editionId: String(row.editionId || "")
                                });
                            }
                        }
                        ++flatIndex;
                    });
                });
            });
        });
        return result;
    }

    function refreshModels() {
        var entries = sourceEntries();
        var albumsFound = [];
        entries.forEach(function(entry) {
            var album = sourceAlbum(entry);
            if (album && album.id)
                albumsFound.push({entry:entry, album:album});
        });
        _albumRecords = albumsFound;

        _albumCards = albumsFound.filter(isVisibleAlbum).map(buildAlbumCard);
        _artistCards = entries.filter(function(entry) {
            return String(entry.type || "") === "artist";
        }).map(buildArtistCard);

        var records = session && session.personalRecords ? session.personalRecords : {};
        var songs = Array.isArray(records.songs) ? records.songs : [];
        _savedSongs = songs.filter(function(entry) { return String(entry.type || "") === "song"; })
            .map(savedSongFor);

        _playlistRecords = entries.filter(function(entry) {
            return String(entry.type || "") === "playlist";
        });
        if (Array.isArray(records.playlists) && records.playlists.length)
            _playlistRecords = records.playlists.slice();
        _playlists = _playlistRecords.map(playlistCard);
        _availableSongs = buildAvailableSongs();

        if (selectedPlaylistId && !_playlistRecords.some(function(entry) {
            return String(entry.id || "") === selectedPlaylistId;
        }))
            selectedPlaylistId = "";
        refreshSelectedPlaylist();
    }

    function refreshSelectedPlaylist() {
        var id = String(selectedPlaylistId || "");
        var entry = _playlistRecords.find(function(value) { return String(value.id || "") === id; });
        if (!entry) {
            _selectedPlaylist = {id:"", name:"", title:"", items:[]};
            return;
        }
        var card = playlistCard(entry);
        _selectedPlaylist = {
            id: card.id,
            name: card.name,
            title: card.title,
            count: card.count,
            cover: card.cover,
            items: playlistItemsFor(entry)
        };
    }

    function resetProfileState() {
        section = "albums";
        selectedPlaylistId = "";
        pendingRows = [];
        personalError = "";
        refreshModels();
    }

    function setSection(value) {
        var next = String(value || "");
        if (["albums", "artists", "songs", "playlists"].indexOf(next) < 0)
            return false;
        section = next;
        if (next !== "playlists") selectedPlaylistId = "";
        personalError = "";
        return true;
    }

    function openPlaylist(id) {
        var next = String(id || "");
        if (!next) {
            section = "playlists";
            selectedPlaylistId = "";
            personalError = "";
            refreshSelectedPlaylist();
            return true;
        }
        if (!_playlistRecords.some(function(entry) { return String(entry.id || "") === next; }))
            return false;
        section = "playlists";
        selectedPlaylistId = next;
        personalError = "";
        refreshSelectedPlaylist();
        return true;
    }

    function activateAlbum(item) {
        var id = String(item && (item.albumId || item.id) || "");
        if (!id) return false;
        albumRequested(id);
        return true;
    }

    function activateArtist(item) {
        if (!item) return false;
        artistRequested(item);
        return true;
    }

    function openSong(item) {
        if (!item || item.unavailable || !item.row || !item.album) {
            personalError = "This song is no longer available.";
            return false;
        }
        var navigationAlbum = albumInNormalizedTrackOrder(item.album, item.editionId);
        if (!navigationAlbum) {
            personalError = "This song is no longer available.";
            return false;
        }
        personalError = "";
        songRequested({album:navigationAlbum, index:item.index, editionId:item.editionId});
        return true;
    }

    function currentQueueRows(items) {
        var rows = [];
        (items || []).forEach(function(item) {
            if (item && item.row && !item.unavailable)
                rows.push(item.row);
        });
        return rows;
    }

    function playRows(rows, shuffle, index) {
        personalError = "";
        if (!rows.length) {
            personalError = "No available songs could be played.";
            return false;
        }
        if (!session || typeof session.playPersonalRows !== "function"
                || !session.playPersonalRows(rows, !!shuffle, index === undefined ? 0 : index)) {
            personalError = session && session.browseError
                ? String(session.browseError) : "The saved songs could not be played.";
            return false;
        }
        return true;
    }

    function playSavedSongs(shuffle) {
        return playRows(currentQueueRows(savedSongs), shuffle, 0);
    }
    function playSavedSong(id) {
        var items=savedSongs;
        var selected=items.find(item=>item.id===id);
        if(!selected || selected.unavailable || !selected.row) {personalError="This song is no longer available.";return false;}
        var rows=currentQueueRows(items);
        return playRows(rows,false,rows.indexOf(selected.row));
    }

    function playlistRecord(id) {
        var wanted = String(id || "");
        return _playlistRecords.find(function(entry) { return String(entry.id || "") === wanted; }) || null;
    }

    function playPlaylist(id, shuffle) {
        var entry = playlistRecord(id);
        if (!entry) {
            personalError = "This playlist is no longer available.";
            return false;
        }
        return playRows(currentQueueRows(playlistItemsFor(entry)), shuffle, 0);
    }

    function playPlaylistItem(id, itemId) {
        var entry = playlistRecord(id);
        var occurrenceId = String(itemId || "");
        if (!entry || !occurrenceId) {
            personalError = "This playlist item is no longer available.";
            return false;
        }
        var items = playlistItemsFor(entry);
        var selected = items.find(function(item) { return item.itemId === occurrenceId; });
        if (!selected) {
            personalError = "This playlist item is no longer available.";
            return false;
        }
        if (!selected.row || selected.unavailable) {
            personalError = "This song is no longer available.";
            return false;
        }
        var playable = currentQueueRows(items);
        var start = playable.findIndex(function(row) { return row === selected.row; });
        if (start < 0) {
            personalError = "This song is no longer available.";
            return false;
        }
        return playRows(playable, false, start);
    }

    function enqueuePlaylist(id, next) {
        var entry = playlistRecord(id);
        if (!entry) {
            personalError = "This playlist is no longer available.";
            return false;
        }
        var rows = currentQueueRows(playlistItemsFor(entry));
        if (!rows.length) {
            personalError = "No available songs could be queued.";
            return false;
        }
        personalError = "";
        if (!session || typeof session.appendRows !== "function") {
            personalError = "The playlist could not be queued.";
            return false;
        }
        if (session.appendRows(rows, !!next) === false) {
            personalError = session.browseError || "The playlist could not be queued.";
            return false;
        }
        return true;
    }

    function validatedQueueRows(values, allowEmpty) {
        if (!Array.isArray(values)) return null;
        if (!values.length && allowEmpty) return [];
        if (!values.length) return null;
        if (!session || typeof session.personalRowAvailable !== "function") return null;
        var result = [];
        for (var i = 0; i < values.length; ++i) {
            var value = values[i];
            var row = value && value.row !== undefined ? value.row : value;
            if (!row || typeof session.personalRowAvailable !== "function"
                    || !session.personalRowAvailable(row))
                return null;
            result.push(row);
        }
        return result;
    }

    function command(operation, args) {
        personalError = "";
        if (!session || typeof session.personalCommand !== "function") {
            personalError = "Your music library is unavailable.";
            return {ok:false, error:personalError};
        }
        var result = null;
        try {
            result = session.personalCommand(operation, args || {});
        } catch (failure) {
            result = {ok:false, error:String(failure)};
        }
        if (!result || result.ok !== true) {
            personalError = result && result.error
                ? String(result.error) : "The music library could not be updated.";
            return result || {ok:false, error:personalError};
        }
        personalError = "";
        return result;
    }

    function createPlaylist(name, rows) {
        var fullRows = validatedQueueRows(rows === undefined ? [] : rows, true);
        if (!fullRows) {
            personalError = "Some selected songs are no longer available.";
            return "";
        }
        var result = command("createPlaylist", {name:String(name === undefined ? "" : name), rows:fullRows});
        if (!result.ok || !result.id) {
            if (result.ok) personalError = "The playlist could not be created.";
            return "";
        }
        pendingRows = [];
        return String(result.id);
    }

    function renamePlaylist(id, name) {
        return command("renamePlaylist", {id:String(id || ""), name:String(name === undefined ? "" : name)}).ok === true;
    }

    function deletePlaylist(id) {
        var playlistId = String(id || "");
        var ok = command("deletePlaylist", {id:playlistId}).ok === true;
        if (ok && selectedPlaylistId === playlistId) selectedPlaylistId = "";
        return ok;
    }

    function addToPlaylist(id, rows) {
        var fullRows = validatedQueueRows(rows, false);
        if (!fullRows) {
            personalError = "Some selected songs are no longer available.";
            return false;
        }
        var ok = command("addPlaylistRows", {id:String(id || ""), rows:fullRows}).ok === true;
        if (ok) pendingRows = [];
        return ok;
    }

    function removePlaylistItem(id, itemId) {
        return command("removePlaylistItem", {
            id:String(id || ""), itemId:String(itemId || "")
        }).ok === true;
    }

    function movePlaylistItem(id, itemId, index) {
        return command("movePlaylistItem", {
            id:String(id || ""), itemId:String(itemId || ""), index:Number(index)
        }).ok === true;
    }

    function toggleSong(item) {
        var savedId = String(item && item.id || "");
        var savedRecord = savedId && _savedSongs.some(function(song) { return song.id === savedId; });
        if (savedRecord && item.unavailable) {
            return command("removeSavedSong", {id:savedId}).ok === true;
        }
        var row = item && item.row ? item.row : item;
        if (!row || !session || typeof session.toggleSongSaved !== "function"
                || !session.personalRowAvailable(row)) {
            personalError = "This song is no longer available.";
            return false;
        }
        personalError = "";
        var ok = !!session.toggleSongSaved(row);
        if (!ok) personalError = String(session.personalError || "The song could not be saved.");
        return ok;
    }

    function openAddPicker(rows) {
        if (!Array.isArray(rows) || !rows.length) return false;
        var fullRows = validatedQueueRows(rows, false);
        if (!fullRows) {
            personalError = "Some selected songs are no longer available.";
            return false;
        }
        pendingRows = fullRows;
        personalError = "";
        pickerRequested();
        return true;
    }

    function closeAddPicker() {
        pendingRows = [];
    }

    function captureViewState() {
        return {section:section, selectedPlaylistId:selectedPlaylistId};
    }

    function restoreViewState(state) {
        if (!state) return;
        var nextSection = String(state.section || "albums");
        if (["albums", "artists", "songs", "playlists"].indexOf(nextSection) < 0)
            nextSection = "albums";
        section = nextSection;
        var restoredPlaylistId = state.selectedPlaylistId === undefined || state.selectedPlaylistId === null
            ? "" : String(state.selectedPlaylistId);
        if (nextSection !== "playlists" || (restoredPlaylistId && !playlistRecord(restoredPlaylistId)))
            restoredPlaylistId = "";
        selectedPlaylistId = restoredPlaylistId;
        refreshSelectedPlaylist();
    }

    function artwork(value) {
        return value || "";
    }

    onCollectionRevisionChanged: refreshModels()
    onSelectedPlaylistIdChanged: refreshSelectedPlaylist()
    onSessionChanged: {
        resetProfileState();
    }

    Connections {
        target: root.session
        ignoreUnknownSignals: true
        function onLibraryChanged() { root.refreshModels(); }
        function onPersonalRecordsChanged() { root.refreshModels(); }
        function onCollectionStoreChanged() { root.resetProfileState(); }
        function onProfileGenerationChanged() { root.resetProfileState(); }
    }

    Connections {
        target: root.collectionStore
        ignoreUnknownSignals: true
        function onChanged() { root.refreshModels(); }
    }

    Component.onCompleted: refreshModels()
}
