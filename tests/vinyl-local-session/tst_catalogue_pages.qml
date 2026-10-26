import QtQuick
import QtTest

TestCase {
    id: test
    name: "VinylCataloguePagesNativeBridge"
    width: 960
    height: 720
    when: windowShown

    property var host: null
    property var world: null
    property var collection: null
    property var progress: null
    property var source: null

    readonly property var kinds: ["albums", "artists", "songs", "playlists"]

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
            signal changed()
            function items(worldName) { return entries; }
            function has(worldName, id) {
                return entries.some(function(entry) { return entry.id === id; });
            }
            function seed(values) { entries = values.slice(); revision++; changed(); }
        }
    }

    Component {
        id: progressFactory
        QtObject {
            property var entries: []
            property int revision: 0
            function recent(kind, limit) {
                var result = entries.filter(function(entry) { return entry.kind === kind; }).slice().reverse();
                return result.slice(0, Math.max(0, Number(limit) || result.length));
            }
            function get(kind, id) {
                return entries.find(function(entry) { return entry.kind === kind && entry.id === id; }) || null;
            }
            function recordSilent(entry) {
                var next = entries.filter(function(value) {
                    return value.kind !== entry.kind || value.id !== entry.id;
                });
                next.push(JSON.parse(JSON.stringify(entry)));
                entries = next;
                revision++;
            }
        }
    }

    Component {
        id: sourceFactory
        QtObject {
            property int catalogueSearchVersion: 1
            property int discoveryVersion: 1
            property int serial: 0
            property var calls: []
            property var cancellations: []
            signal completed(string lane, int requestId, var data, string error)
            signal profileLeaving()
            signal profileEntered()
            signal checkpointRequested()
            signal shuttingDown()

            function request(lane, operation, key, options) {
                var id = ++serial;
                calls = calls.concat([{
                    lane:lane, operation:operation, key:key,
                    options:options || {}, id:id
                }]);
                return id;
            }
            function cancel(lane: string): void { cancellations = cancellations.concat([lane]); }
            function setAlbums(values) {}
        }
    }

    function albumRecord(id) {
        return {id:id, browseId:id, title:id, origin:"youtube-music",
            artists:["Catalogue Artist"], artistIds:["ytm:artist:catalogue"],
            cover:"", year:"2026", tracks:[{id:id + "-track", title:"Track for " + id,
                artists:["Catalogue Artist"], seconds:47, disc:1, number:1, available:true}]};
    }

    function itemFor(kind, id) {
        if (kind === "albums")
            return {id:id, albumId:id, title:id, artist:"Catalogue Artist", cover:"", kind:kind};
        if (kind === "artists")
            return {id:"ytm:artist:" + id, artistId:"ytm:artist:" + id,
                title:id, artist:id, cover:"", round:true, kind:kind};
        if (kind === "songs")
            return {id:id + "-track", albumId:id, title:"Song for " + id,
                artist:"Catalogue Artist", cover:"", trackIndex:0, trackId:id + "-track", kind:kind};
        return {id:"ytm:playlist:" + id, albumId:"ytm:playlist:" + id,
            title:id, artist:"Catalogue Artist", cover:"", kind:"catalogue-playlists"};
    }

    function playlistRecord(id) {
        var key = "ytm:playlist:" + id;
        return {id:key, browseId:key, playlistId:id, type:"Playlist", origin:"youtube-music",
            title:id, artists:[], artistIds:[], cover:"", tracks:[]};
    }

    function playlistTracks(ids) {
        return ids.map(function(id, index) {
            return {id:id, title:"Recording " + id, artists:["Playlist Artist"],
                seconds:31 + index, disc:1, number:index + 1, available:true};
        });
    }

    function partialPlaylist(id, trackIds, token) {
        var album = playlistRecord(id);
        album.tracks = playlistTracks(trackIds);
        album.complete = false;
        album.nextCursor = {version:1, operation:"playlistPage", value:id,
            region:"IN", token:token, offset:trackIds.length, meta:{}};
        return album;
    }

    function playlistPage(id, offset, trackIds, complete, nextCursor, omittedTracks) {
        var album = playlistRecord(id);
        album.tracks = playlistTracks(trackIds);
        album.offset = offset;
        album.nextOffset = offset + trackIds.length;
        album.complete = complete;
        album.nextCursor = nextCursor || {};
        album.omittedTracks = omittedTracks || 0;
        return album;
    }

    function pageEnvelope(query, kind, offset, items, records, complete, nextCursor, nextOffset) {
        return {version:1, query:query, kind:kind, source:"youtube-music", region:"IN", offset:offset,
            nextOffset:nextOffset, items:items, records:records,
            nextCursor:nextCursor || {}, complete:complete};
    }

    function cursorFor(query, kind, token, offset) {
        return {version:1, operation:"cataloguePage", value:query, kind:kind,
            region:"IN", token:token, offset:offset, meta:{}};
    }

    function firstPage(query, kind, suffix, complete) {
        var id = kind === "albums" ? "album-" + suffix
            : kind === "songs" ? "song-album-" + suffix
            : kind === "artists" ? "artist-" + suffix : "playlist-" + suffix;
        var item = itemFor(kind, id);
        var records = kind === "albums" || kind === "songs" ? [albumRecord(id)]
            : kind === "playlists" ? [playlistRecord(id)] : [];
        var more = complete === false;
        return pageEnvelope(query, kind, 0, [item], records, !more,
            more ? cursorFor(query, kind, "cursor-" + kind + "-2", 1) : {}, 1);
    }

    function searchCalls(query, kind) {
        return source.calls.filter(function(call) {
            return call.lane === "search-" + kind && call.operation === "cataloguePage"
                && call.key === query;
        });
    }

    function searchCall(query, kind, index) {
        return searchCalls(query, kind)[index === undefined ? searchCalls(query, kind).length - 1 : index] || null;
    }

    function enterDiscover() {
        world.enterDiscoverSearch(true, false);
        tryVerify(function() {
            return world.page === "discover" && world.worldData.activeTab === "discover";
        });
    }

    function startSearch(query, selectedKind) {
        enterDiscover();
        verify(world.worldData.setKind(selectedKind));
        world.worldData.setQuery(query);
        world.worldData.submitSearch();
        tryVerify(function() {
            return source.calls.filter(function(call) {
                return call.operation === "cataloguePage" && call.key === query;
            }).length === 4;
        }, 2000, "A catalogue query should request all four typed result pages.");
    }

    function completePage(query, kind, call, data, error) {
        source.completed("search-" + kind, call.id, data, error || "");
    }

    function completeAllFirstPages(query, exceptKind) {
        kinds.forEach(function(kind) {
            if (kind === exceptKind) return;
            var call = searchCall(query, kind, 0);
            completePage(query, kind, call, firstPage(query, kind, "first-" + kind, true));
        });
        wait(0);
    }

    function init() {
        if (typeof BridgeFixtures === "undefined" || typeof NativeSource === "undefined") {
            skip("Requires the vinyl_native_session_bridge runner.");
            return;
        }
        host = windowFactory.createObject(test);
        collection = collectionFactory.createObject(test);
        progress = progressFactory.createObject(test);
        source = sourceFactory.createObject(test);

        var component = Qt.createComponent("../../qml/vinyl/VinylLiveWorld.qml");
        compare(component.status, Component.Ready, component.errorString());
        world = component.createObject(host.contentItem, {width:960, height:720});
        verify(world !== null);
        world.session.source = source;
        world.session.collectionStore = collection;
        world.session.progressStore = progress;
        world.session.refreshLibrary();
        world.personalLibrary.refreshModels();
        world.worldData.preferencesStore = null;
        world.worldData.searchHistoryStore = null;
        world.worldData.loadTaste();
        wait(0);
        var nativeBackend = findChild(world, "vinylBackendSession");
        verify(nativeBackend !== null, "The test must use the native Vinyl session bridge.");
        verify(typeof nativeBackend.checkpointSummary === "function");
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
        if (source) { source.destroy(); source = null; }
        if (collection) { collection.destroy(); collection = null; }
        if (progress) { progress.destroy(); progress = null; }
    }

    function test_typedSearchFansOutAndKindSwitchUsesCachedPages() {
        enterDiscover();
        world.worldData.setOnlineMode(false);
        verify(!world.worldData.setKind("playlists"), "Playlist search is only available online.");
        world.worldData.setOnlineMode(true);
        verify(world.worldData.setKind("albums"));
        world.worldData.setQuery("four categories");
        world.worldData.submitSearch();

        tryVerify(function() {
            return source.calls.filter(function(call) {
                return call.operation === "cataloguePage" && call.key === "four categories";
            }).length === 4;
        }, 2000);
        compare(world.worldData.cataloguePagingSupported, true);
        for (var i = 0; i < kinds.length; ++i) {
            var call = searchCall("four categories", kinds[i], 0);
            verify(call !== null);
            compare(call.lane, "search-" + kinds[i]);
            compare(call.operation, "cataloguePage");
            compare(call.key, "four categories");
            compare(call.options.kind, kinds[i]);
            compare(Object.keys(call.options.cursor).length, 0);
            completePage("four categories", kinds[i], call,
                firstPage("four categories", kinds[i], "first-" + kinds[i], true));
        }
        wait(0);
        var catalogueCallCount = source.calls.filter(function(call) {
            return call.operation === "cataloguePage" && call.key === "four categories";
        }).length;
        compare(catalogueCallCount, 4);

        for (var j = 0; j < kinds.length; ++j) {
            var kind = kinds[j];
            verify(world.worldData.setKind(kind));
            compare(world.worldData.searchPage.kind, kind);
            compare(world.worldData.searchPage.items.length, 1);
            compare(world.worldData.shelves[0].key, "search-" + kind);
            compare(world.worldData.shelves[0].kind,
                    kind === "playlists" ? "catalogue-playlists" : kind);
            compare(world.worldData.shelves[0].items[0].kind,
                    kind === "playlists" ? "catalogue-playlists" : kind);
        }
        compare(world.worldData.findAlbum("album-first-albums").id, "album-first-albums");
        compare(world.worldData.findAlbum("song-album-first-songs").id,
                "song-album-first-songs");
        compare(source.calls.filter(function(call) {
            return call.operation === "cataloguePage" && call.key === "four categories";
        }).length, catalogueCallCount, "Switching kinds should reuse each loaded page.");
    }

    function test_loadMoreUsesCursorAndDeduplicatesItemsAndRecords() {
        var query = "cursor query";
        startSearch(query, "albums");
        var firstRecords = [albumRecord("album-a"), albumRecord("album-b")];
        var firstItems = [itemFor("albums", "album-a"), itemFor("albums", "album-b")];
        var first = searchCall(query, "albums", 0);
        completePage(query, "albums", first,
            pageEnvelope(query, "albums", 0, firstItems, firstRecords, false,
                cursorFor(query, "albums", "cursor-albums-2", 2), 2));
        completeAllFirstPages(query, "albums");
        compare(world.worldData.searchHasMore, true);
        compare(world.worldData.searchLoading, false);

        world.worldData.loadMoreSearch();
        var more = searchCall(query, "albums", 1);
        verify(more !== null && more.id !== first.id);
        compare(more.lane, "search-albums");
        compare(more.operation, "cataloguePage");
        compare(more.key, query);
        compare(more.options.kind, "albums");
        compare(more.options.cursor.token, "cursor-albums-2");
        compare(more.options.cursor.offset, 2);
        compare(world.worldData.searchLoading, true);

        completePage(query, "albums", more,
            pageEnvelope(query, "albums", 2,
                [itemFor("albums", "album-b"), itemFor("albums", "album-c")],
                [albumRecord("album-b"), albumRecord("album-c")], true, {}, 4));
        tryVerify(function() { return world.worldData.searchPage.complete === true; });
        compare(world.worldData.searchPage.items.length, 3);
        compare(world.worldData.searchPage.items[0].id, "album-a");
        compare(world.worldData.searchPage.items[1].id, "album-b");
        compare(world.worldData.searchPage.items[2].id, "album-c");
        compare(world.worldData.searchPage.records.length, 3);
        compare(world.worldData.findAlbum("album-a").id, "album-a");
        compare(world.worldData.findAlbum("album-b").id, "album-b");
        compare(world.worldData.findAlbum("album-c").id, "album-c");
    }

    function test_failedAppendRetainsRowsAndRetriesSameCursor() {
        var query = "retry query";
        startSearch(query, "albums");
        var first = searchCall(query, "albums", 0);
        completePage(query, "albums", first,
            pageEnvelope(query, "albums", 0, [itemFor("albums", "keep-album")],
                [albumRecord("keep-album")], false, cursorFor(query, "albums", "retry-cursor", 1), 1));
        completeAllFirstPages(query, "albums");
        var oldIds = world.worldData.searchPage.items.map(function(item) { return item.id; });

        world.worldData.loadMoreSearch();
        var failed = searchCall(query, "albums", 1);
        compare(failed.options.cursor.token, "retry-cursor");
        completePage(query, "albums", failed, null, "Synthetic page timeout.");
        var retainedIds = world.worldData.searchPage.items.map(function(item) { return item.id; });
        compare(retainedIds.length, oldIds.length);
        for (var i = 0; i < oldIds.length; ++i) compare(retainedIds[i], oldIds[i]);
        verify(world.worldData.searchError.length > 0);
        compare(world.worldData.searchHasMore, true);

        world.worldData.loadMoreSearch();
        var retry = searchCall(query, "albums", 2);
        verify(retry !== null && retry.id !== failed.id);
        compare(retry.options.cursor.token, "retry-cursor");
        completePage(query, "albums", retry,
            pageEnvelope(query, "albums", 1, [itemFor("albums", "next-album")],
                [albumRecord("next-album")], true, {}, 2));
        tryVerify(function() { return world.worldData.searchPage.complete === true; });
        compare(world.worldData.searchPage.items.length, 2);
        compare(world.worldData.searchPage.items[0].id, "keep-album");
        compare(world.worldData.searchPage.items[1].id, "next-album");
        compare(world.worldData.searchError, "");
    }

    function test_staleQueryAndRetiredProfileIgnoreLatePageReplies() {
        startSearch("old query", "albums");
        var staleAlbum = searchCall("old query", "albums", 0);
        world.worldData.setQuery("new query");
        world.worldData.submitSearch();
        tryVerify(function() { return searchCalls("new query", "albums").length === 1; });
        var currentAlbum = searchCall("new query", "albums", 0);
        verify(currentAlbum.id !== staleAlbum.id);

        completePage("old query", "albums", staleAlbum,
            firstPage("old query", "albums", "stale", true));
        compare(world.session.catalogueQuery, "new query");
        verify((world.worldData.searchPage.items || []).every(function(item) {
            return item.id !== "album-stale";
        }));

        completePage("new query", "albums", currentAlbum,
            firstPage("new query", "albums", "current", true));
        var lateArtist = searchCall("new query", "artists", 0);
        source.profileLeaving();
        tryCompare(world.session, "profileActive", false, 2000);
        compare(Object.keys(world.session.cataloguePages).length, 0);
        ["search-albums", "search-artists", "search-songs", "search-playlists"].forEach(function(lane) {
            verify(source.cancellations.indexOf(lane) >= 0,
                "Profile clear must cancel " + lane + ".");
        });

        completePage("new query", "artists", lateArtist,
            firstPage("new query", "artists", "late", true));
        wait(0);
        compare(Object.keys(world.session.cataloguePages).length, 0);
        compare((world.worldData.searchPage.items || []).length, 0);
    }

    function test_tabRoundTripRestoresPagesAndClearingQueryReturnsDiscover() {
        var query = "saved tab query";
        startSearch(query, "songs");
        completeAllFirstPages(query, "");
        compare(world.worldData.searchPage.items.length, 1);
        compare(world.worldData.shelves[0].key, "search-songs");

        world.worldView.setTab("library");
        tryCompare(world.worldData, "activeTab", "library", 2000);
        world.worldView.setTab("discover");
        tryCompare(world.worldData, "activeTab", "discover", 2000);
        compare(world.worldData.query, query);
        compare(world.worldData.lens.kind, "songs");
        compare(world.worldData.searchPage.items.length, 1);
        compare(world.worldData.shelves[0].key, "search-songs");

        world.worldData.setQuery("");
        tryCompare(world.worldData, "discoveryLanding", true, 2000);
        verify(world.worldData.shelves.every(function(row) {
            return row.key.indexOf("search-") !== 0;
        }));
        compare(world.worldData.activeTab, "discover");
    }

    function test_songAndPlaylistSeeAllKeepTheirTypedNavigationRows() {
        var query = "typed navigation";
        startSearch(query, "songs");
        completeAllFirstPages(query, "");

        verify(world.worldData.setKind("songs"));
        world.worldData.openList("search-songs", false);
        tryCompare(world.worldView, "listKey", "search-songs", 2000);
        var list = findChild(world, "vinylListPage");
        verify(list !== null);
        compare(list.row.kind, "songs");
        compare(list.row.items[0].kind, "songs");
        world.activateItem(list.row.items[0]);
        tryCompare(world, "page", "song", 2000);
        compare(world.browsingSong, true);

        world.goBack();
        tryCompare(world, "page", "discover", 2000);
        tryCompare(world.worldView, "listKey", "search-songs", 2000);
        compare(world.worldData.searchPage.items.length, 1);
        compare(world.worldData.shelves[0].key, "search-songs");
        world.worldView.backFromList();
        tryCompare(world.worldView, "listKey", "", 2000);

        verify(world.worldData.setKind("playlists"));
        world.worldData.openList("search-playlists", false);
        tryCompare(world.worldView, "listKey", "search-playlists", 2000);
        list = findChild(world, "vinylListPage");
        verify(list !== null);
        compare(list.row.kind, "catalogue-playlists");
        compare(list.row.items.length, 1);
        compare(list.row.items[0].kind, "catalogue-playlists");
    }

    function test_playlistPageAppendsDuplicatePositionsAndFinalOmittedCount() {
        var id = "append-playlist";
        var partial = partialPlaylist(id, ["recording-a", "recording-b", "recording-a"],
            "playlist-cursor-2");
        world.session.shownEdition = world.session.normalize(partial);
        world.session.shownDisc = 0;

        world.session.loadMorePlaylist();
        var calls = source.calls.filter(function(call) { return call.lane === "playlist-page"; });
        compare(calls.length, 1);
        var request = calls[0];
        compare(request.operation, "playlistPage");
        compare(request.key, id);
        compare(request.options.cursor.token, "playlist-cursor-2");

        source.completed("playlist-page", request.id,
            playlistPage(id, 3, ["recording-c", "recording-a"], true, {}, 2), "");
        tryVerify(function() { return !world.session.playlistPageLoading; });

        var merged = world.session.shownEdition.sourceAlbum;
        var occurrences = merged.tracks.map(function(track) { return track.id; });
        compare(occurrences.join(","),
            "recording-a,recording-b,recording-a,recording-c,recording-a");
        compare(merged.complete, true);
        compare(merged.omittedTracks, 2);
        compare(world.session.playlistHasMore, false);
        compare(world.session.playlistPageError, "");
    }

    function test_playlistPageErrorAndWrongOffsetKeepRowsAndCursor() {
        var id = "retry-playlist";
        var partial = partialPlaylist(id, ["recording-a", "recording-b"], "retry-playlist-cursor");
        world.session.shownEdition = world.session.normalize(partial);
        world.session.shownDisc = 0;
        var priorIds = partial.tracks.map(function(track) { return track.id; }).join(",");

        world.session.loadMorePlaylist();
        var first = source.calls.filter(function(call) { return call.lane === "playlist-page"; })[0];
        source.completed("playlist-page", first.id, null, "Synthetic page timeout.");
        tryVerify(function() { return !world.session.playlistPageLoading; });
        compare(world.session.shownEdition.sourceAlbum.tracks.map(function(track) { return track.id; }).join(","), priorIds);
        verify(world.session.playlistPageError.length > 0);
        compare(world.session.shownEdition.sourceAlbum.nextCursor.token, "retry-playlist-cursor");
        compare(world.session.playlistHasMore, true);

        world.session.loadMorePlaylist();
        var calls = source.calls.filter(function(call) { return call.lane === "playlist-page"; });
        var wrongOffset = calls[1];
        compare(wrongOffset.options.cursor.token, "retry-playlist-cursor");
        source.completed("playlist-page", wrongOffset.id,
            playlistPage(id, 19, ["unexpected-recording"], true, {}, 0), "");
        tryVerify(function() { return !world.session.playlistPageLoading; });
        compare(world.session.shownEdition.sourceAlbum.tracks.map(function(track) { return track.id; }).join(","), priorIds);
        verify(world.session.playlistPageError.length > 0);
        compare(world.session.shownEdition.sourceAlbum.nextCursor.token, "retry-playlist-cursor");

        world.session.loadMorePlaylist();
        calls = source.calls.filter(function(call) { return call.lane === "playlist-page"; });
        compare(calls.length, 3);
        compare(calls[2].options.cursor.token, "retry-playlist-cursor");
    }

    function test_playingPartialPlaylistFetchesAndStartsTheFullQueue() {
        var id = "play-full-playlist";
        var partial = partialPlaylist(id, ["recording-a", "recording-selected"], "full-playlist-cursor");
        world.session.shownEdition = world.session.normalize(partial);
        world.session.shownDisc = 0;

        world.session.playAlbum(partial, 1);
        var queueCalls = source.calls.filter(function(call) { return call.lane === "queue"; });
        compare(queueCalls.length, 1);
        var request = queueCalls[0];
        compare(request.operation, "album");
        compare(request.key, partial.id);
        compare(world.session.queue.length, 0,
            "The partial playlist prefix must not replace the queue while the full fetch is pending.");

        var full = playlistPage(id, 0,
            ["recording-a", "recording-selected", "recording-c", "recording-selected"], true, {}, 1);
        source.completed("queue", request.id, full, "");
        tryVerify(function() { return world.session.queue.length === 4; });
        var queuedIds = world.session.queue.map(function(row) { return row.trackId; });
        compare(queuedIds.join(","),
            "recording-a,recording-selected,recording-c,recording-selected");
        compare(world.session.queueIndex, 1);
        compare(world.session.playingTrack.id, "recording-selected");
        compare(world.session.browseError, "");
    }

    function test_enqueuingPartialPlaylistRejectsIncompleteQueueAndAcceptsFullOccurrences() {
        var id = "enqueue-full-playlist";
        var partial = partialPlaylist(id, ["recording-a", "recording-b"], "enqueue-cursor");
        world.session.shownEdition = world.session.normalize(partial);
        world.session.shownDisc = 0;

        world.session.enqueueAlbum(partial.id, false);
        var queueCalls = source.calls.filter(function(call) { return call.lane === "queue"; });
        compare(queueCalls.length, 1);
        var request = queueCalls[0];
        compare(request.operation, "album");
        compare(request.key, partial.id);
        compare(world.session.queue.length, 0);

        source.completed("queue", request.id,
            playlistPage(id, 0, ["recording-a", "recording-b"], false,
                partial.nextCursor, 0), "");
        tryVerify(function() { return world.session.queueRequest < 0; });
        compare(world.session.queue.length, 0,
            "An incomplete playlist response must not append its loaded prefix.");
        verify(world.session.browseError.length > 0);

        world.session.enqueueAlbum(partial.id, false);
        queueCalls = source.calls.filter(function(call) { return call.lane === "queue"; });
        compare(queueCalls.length, 2);
        var fullRequest = queueCalls[1];
        var full = playlistPage(id, 0,
            ["recording-a", "recording-b", "recording-a"], true, {}, 0);
        source.completed("queue", fullRequest.id, full, "");
        tryVerify(function() { return world.session.queue.length === 3; });
        var queuedIds = world.session.queue.map(function(row) { return row.trackId; });
        compare(queuedIds.join(","), "recording-a,recording-b,recording-a");
        compare(world.session.browseError, "");
    }
}
