import QtQuick
import QtTest

TestCase {
    id: test
    name: "VinylPersonalLibraryNativeBridge"
    width: 960
    height: 720
    when: windowShown

    property var host: null
    property var world: null
    property var collection: null
    property var rawAlbum: null
    property string audioPath: ""

    Component {
        id: windowFactory
        Window { width: 960; height: 720; visible: true }
    }

    Component {
        id: collectionFactory
        QtObject {
            property var entries: []
            property int revision: 0
            property bool healthy: true
            property string persistenceError: ""
            property bool failWrites: false
            signal changed()

            function items(worldName) { return entries; }
            function has(worldName, id) {
                return entries.some(function(entry) { return entry.id === id; });
            }
            function replaceItems(worldName, removeIds, replacements) {
                if (failWrites || !healthy) {
                    persistenceError = "Synthetic Collection write failure.";
                    return false;
                }
                var removed = removeIds || [];
                var writes = replacements || [];
                var next = entries.filter(function(entry) {
                    return removed.indexOf(entry.id) < 0
                        && !writes.some(function(write) { return write.id === entry.id; });
                });
                writes.forEach(function(write) { next.push(write); });
                entries = next;
                persistenceError = "";
                revision++;
                changed();
                return true;
            }
            function add(worldName, entry) {
                return replaceItems(worldName, [], [entry]);
            }
            function seed(values) {
                entries = values.slice();
                revision++;
                changed();
            }
        }
    }

    function albumFor(path) {
        return {
            id: "personal-test-album",
            title: "Personal Test Album",
            origin: "local",
            artists: ["Personal Test Artist"],
            artistIds: ["personal-test-artist"],
            cover: "",
            tracks: [
                {id:"personal-test-track-one", title:"Track One", artists:["Personal Test Artist"],
                    seconds:41, disc:1, number:1, available:true, localPath:path},
                {id:"personal-test-track-two", title:"Track Two", artists:["Personal Test Artist"],
                    seconds:53, disc:1, number:2, available:true, localPath:path}
            ]
        };
    }

    function albumEntry(album) {
        return {id:album.id, type:"album", title:album.title, cover:album.cover,
            artist:album.artists.join(" · "), browseId:album.id,
            payload:{version:1, album:album, saved:true}};
    }

    function rowAt(index) {
        var edition = world.session.normalize(rawAlbum);
        return world.session.rowFor(edition, 0, index);
    }

    function init() {
        if (typeof BridgeFixtures === "undefined" || typeof NativeSource === "undefined") {
            skip("Requires the vinyl_native_session_bridge runner.");
            return;
        }
        host = windowFactory.createObject(test);
        collection = collectionFactory.createObject(test);
        audioPath = BridgeFixtures.createIdentityAudio();
        rawAlbum = albumFor(audioPath);
        collection.seed([albumEntry(rawAlbum)]);

        var component = Qt.createComponent("../../qml/vinyl/VinylLiveWorld.qml");
        compare(component.status, Component.Ready, component.errorString());
        world = component.createObject(host.contentItem, {width:960, height:720});
        verify(world !== null);
        world.session.source = NativeSource;
        world.session.collectionStore = collection;
        world.session.refreshLibrary();
        world.personalLibrary.refreshModels();
        wait(0);
        verify(world.session.personalEnabled, "The real native session must bind to the fake Collection.");
    }

    function cleanup() {
        if (world) {
            world.destroy();
            wait(0);
            world = null;
        }
        if (host) {
            host.close();
            host.destroy();
            wait(0);
            host = null;
        }
        if (collection) {
            collection.destroy();
            wait(0);
            collection = null;
        }
        rawAlbum = null;
    }

    function test_savedSongCanBeSavedAndUnsavedThroughLibraryAdapter() {
        var row = rowAt(0);
        verify(world.personalLibrary.toggleSong({row:row}));
        compare(world.personalLibrary.savedSongs.length, 1);
        verify(world.session.songSaved(row));

        var saved = world.personalLibrary.savedSongs[0];
        compare(saved.ref.trackId, row.trackId);
        compare(saved.ref.albumId, row.albumId);
        compare(saved.ref.editionId, row.editionId);
        compare(saved.ref.discIndex, row.discIndex);
        compare(saved.ref.trackIndex, row.trackIndex);
        verify(world.personalLibrary.toggleSong(saved));
        compare(world.personalLibrary.savedSongs.length, 0);
        verify(!world.session.songSaved(row));
        compare(collection.entries.filter(function(entry) { return entry.type === "song"; }).length, 0);
    }

    function test_duplicatePlaylistOccurrencesMoveAndRemoveIndependently() {
        var first = rowAt(0), second = rowAt(1);
        var playlistId = world.personalLibrary.createPlaylist("Repeated tracks", [first, second, first]);
        verify(playlistId.length > 0);
        verify(world.personalLibrary.openPlaylist(playlistId));
        var items = world.personalLibrary.selectedPlaylist.items;
        compare(items.length, 3);
        compare(items[0].ref.trackId, first.trackId);
        compare(items[1].ref.trackId, second.trackId);
        compare(items[2].ref.trackId, first.trackId);
        verify(items[0].itemId.length > 0 && items[1].itemId.length > 0 && items[2].itemId.length > 0);
        verify(items[0].itemId !== items[1].itemId && items[0].itemId !== items[2].itemId
            && items[1].itemId !== items[2].itemId, "Duplicate tracks need independent occurrence IDs.");

        var originalIds = items.map(function(item) { return item.itemId; });
        var movedId = items[0].itemId;
        verify(world.personalLibrary.movePlaylistItem(playlistId, movedId, 2));
        items = world.personalLibrary.selectedPlaylist.items;
        compare(JSON.stringify(items.map(function(item) { return item.ref.trackId; })),
            JSON.stringify([second.trackId, first.trackId, first.trackId]));
        verify(world.personalLibrary.removePlaylistItem(playlistId, movedId));
        items = world.personalLibrary.selectedPlaylist.items;
        compare(JSON.stringify(items.map(function(item) { return item.ref.trackId; })),
            JSON.stringify([second.trackId, first.trackId]));
        compare(JSON.stringify(items.map(function(item) { return item.itemId; })),
            JSON.stringify([originalIds[1], originalIds[2]]));
        verify(items.every(function(item) { return item.itemId !== movedId; }));

        verify(world.personalLibrary.openPlaylist(""));
        compare(world.personalLibrary.section, "playlists");
        compare(world.personalLibrary.selectedPlaylistId, "");
        compare(world.personalLibrary.selectedPlaylist.id, "");
        verify(world.personalLibrary.openPlaylist(playlistId));
        world.personalLibrary.restoreViewState({section:"playlists", selectedPlaylistId:""});
        compare(world.personalLibrary.selectedPlaylistId, "");
        compare(world.personalLibrary.selectedPlaylist.items.length, 0);
    }

    function test_unavailableSavedSongCanBeRemovedWhileAlbumMetadataRemains() {
        var row = rowAt(1);
        verify(world.personalLibrary.toggleSong({row:row}));
        compare(world.personalLibrary.savedSongs.length, 1);

        var retained = JSON.parse(JSON.stringify(collection.entries.find(function(entry) {
            return entry.id === rawAlbum.id;
        })));
        retained.payload.album.tracks[1].localPath = audioPath + ".missing";
        verify(collection.replaceItems("Vinyl", [], [retained]));
        wait(0);

        var unavailable = world.personalLibrary.savedSongs[0];
        compare(unavailable.id.length > 0, true);
        compare(unavailable.unavailable, true);
        compare(unavailable.row, null);
        verify(world.personalLibrary.toggleSong(unavailable), "Unavailable saved songs must still be removable.");
        compare(world.personalLibrary.savedSongs.length, 0);
        verify(collection.entries.some(function(entry) { return entry.id === rawAlbum.id && entry.type === "album"; }),
            "Removing a saved song must retain its source album record.");
    }

    function test_profileClearHidesPreviousPersonalRecords() {
        var row = rowAt(0);
        verify(world.personalLibrary.toggleSong({row:row}));
        var playlistId = world.personalLibrary.createPlaylist("Profile-only playlist", [row]);
        verify(playlistId.length > 0);
        compare(world.personalLibrary.savedSongs.length, 1);
        compare(world.personalLibrary.playlists.length, 1);

        world.session.clearProfile();
        wait(0);
        verify(!world.session.personalEnabled);
        compare(world.personalLibrary.savedSongs.length, 0);
        compare(world.personalLibrary.playlists.length, 0);
        compare(world.personalLibrary.albums.length, 0);
        verify(collection.entries.some(function(entry) { return entry.type === "song"; }),
            "Profile clearing should hide the old Collection view, not mutate its records.");
        verify(collection.entries.some(function(entry) { return entry.id === playlistId; }));
    }

    function test_failedPersonalWriteLeavesCollectionUnchanged() {
        var before = JSON.stringify(collection.entries);
        collection.failWrites = true;
        var result = world.personalLibrary.createPlaylist("Must not persist", [rowAt(0)]);
        compare(result, "");
        compare(JSON.stringify(collection.entries), before);
        verify(world.personalLibrary.error.length > 0);
        compare(collection.entries.some(function(entry) { return entry.type === "playlist"; }), false);
    }

    function test_backRestoresSelectedPlaylistAfterOpeningSavedSong() {
        var row = rowAt(0);
        verify(world.personalLibrary.toggleSong({row:row}));
        var playlistId = world.personalLibrary.createPlaylist("Back target", [row]);
        verify(playlistId.length > 0);
        verify(world.personalLibrary.openPlaylist(playlistId));
        compare(world.personalLibrary.captureViewState().selectedPlaylistId, playlistId);

        verify(world.personalLibrary.openSong(world.personalLibrary.savedSongs[0]));
        compare(world.page, "song");
        verify(world.goBack());
        wait(0);
        compare(world.page, "library");
        compare(world.personalLibrary.section, "playlists");
        compare(world.personalLibrary.selectedPlaylistId, playlistId);
        compare(world.personalLibrary.selectedPlaylist.items.length, 1);
    }

    function test_savedSongNavigationKeepsDiscMajorCoordinateExact() {
        var interleaved = albumFor(audioPath);
        interleaved.id = "personal-interleaved-album";
        interleaved.title = "Interleaved Album";
        interleaved.tracks = [
            {id:"disc-two-track", title:"Disc Two Track", artists:["Personal Test Artist"],
                seconds:53, disc:2, number:1, available:true, localPath:audioPath},
            {id:"disc-one-track", title:"Disc One Track", artists:["Personal Test Artist"],
                seconds:41, disc:1, number:1, available:true, localPath:audioPath}
        ];
        interleaved.editions = [{id:"interleaved-edition", title:"Interleaved Edition",
            tracks:interleaved.tracks}];
        verify(collection.replaceItems("Vinyl", [], [albumEntry(interleaved)]));
        world.session.refreshLibrary();
        world.personalLibrary.refreshModels();
        var edition = world.session.normalizeEditions(interleaved)[0];
        var exact = world.session.rowFor(edition, 0, 0);
        compare(exact.trackId, "disc-one-track");
        verify(world.personalLibrary.toggleSong({row:exact}));
        var saved = world.personalLibrary.savedSongs.find(function(song) {
            return song.ref.albumId === interleaved.id;
        });
        verify(saved !== undefined);
        verify(world.personalLibrary.openSong(saved));

        var songPage = findChild(world, "vinylSongPage");
        verify(songPage !== null);
        compare(songPage.pageData.rawTrack.id, exact.trackId);
        compare(songPage.pageData.personalRow.trackId, exact.trackId);
        compare(songPage.pageData.personalRow.discIndex, exact.discIndex);
        compare(songPage.pageData.personalRow.trackIndex, exact.trackIndex);
    }

    function test_appendRowsRejectsRowsBeyondRemainingQueueCapacity() {
        var row = rowAt(0);
        verify(world.session.appendRows([row], false));
        compare(world.session.queue.length, 1);
        var tooMany = [];
        for (var i = 0; i < 10000; ++i) tooMany.push(row);
        verify(!world.session.appendRows(tooMany, false));
        compare(world.session.queue.length, 1);
        compare(world.session.browseError, "The queue is full. Remove some tracks and try again.");
    }

    function test_rowForProjectionDoesNotAllocateQueueSerial() {
        var before = world.session.queueSerial;
        var row = rowAt(0);
        compare(world.session.queueSerial, before);
        verify(row.trackId === rawAlbum.tracks[0].id);
        verify(row.queueId === undefined, "Only native queue insertion should allocate occurrence IDs.");
    }
    function test_savedSongTitlePlaysInPlaceAndCoverOpensSong() {
        verify(world.personalLibrary.toggleSong({row:rowAt(0)}));
        verify(world.personalLibrary.toggleSong({row:rowAt(1)}));
        world.personalLibrary.setSection("songs");wait(0);
        world.forceActiveFocus(Qt.OtherFocusReason);
        var play=findChild(world,"vinylLibrarySavedSongPlay_1");verify(play!==null);
        play.activate(Qt.MouseFocusReason);
        compare(world.page,"library");compare(world.session.queue.length,2);
        compare(world.session.playingTrack.id,world.personalLibrary.savedSongs[1].row.trackId);
        findChild(world,"vinylLibrarySavedSongCover_1").activate(Qt.MouseFocusReason);
        compare(world.page,"song");verify(world.goBack());wait(0);
        compare(world.page,"library");compare(world.personalLibrary.section,"songs");
        verify(!findChild(world,"vinylLibrarySavedSongPlay_1").activeFocus,"Pointer Back must not create keyboard highlighting.");
    }
    function test_reenteringSameProfileRehydratesPersonalRecords() {
        verify(world.personalLibrary.toggleSong({row:rowAt(0)}));
        verify(world.personalLibrary.createPlaylist("Return profile",[rowAt(0)]));
        world.session.clearProfile();wait(0);
        compare(world.personalLibrary.savedSongs.length,0);compare(world.personalLibrary.playlists.length,0);
        NativeSource.profileEntered();wait(0);
        verify(world.session.profileActive);
        compare(world.personalLibrary.savedSongs.length,1);compare(world.personalLibrary.playlists.length,1);
    }
}
