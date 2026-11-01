import QtQuick
import QtTest

TestCase {
    id: test
    name: "VinylDiscoveryNativeBridge"
    width: 960
    height: 720
    when: windowShown

    property var host: null
    property var world: null
    property var collection: null
    property var progress: null
    property var source: null

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
            function seed(values) {
                entries = values.slice();
                revision++;
                changed();
            }
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
        id: discoverySourceFactory
        QtObject {
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

    function albumRecord(id, count) {
        var tracks = [];
        for (var i = 0; i < count; ++i) {
            tracks.push({id:id + "-track-" + i, title:"Track " + i,
                artists:["Public Artist"], seconds:42, disc:1, number:i + 1,
                available:true});
        }
        return {id:id, browseId:id, title:id, origin:"youtube-music",
            artists:["Public Artist"], artistIds:["ytm:artist:public"],
            cover:"", year:"2026", tracks:tracks};
    }

    function envelope(key, count) {
        var items = [], records = [];
        if (key === "new-releases") {
            for (var a = 0; a < count; ++a) {
                var album = albumRecord("release-" + a, 2);
                records.push(album);
                items.push({id:album.id, albumId:album.id, title:album.title,
                    artist:"Public Artist", cover:"", kind:"albums"});
            }
        } else if (key === "trending") {
            var release = albumRecord("trending-release", count);
            records.push(release);
            for (var s = 0; s < count; ++s) {
                items.push({id:"trending-song-" + s, albumId:release.id,
                    title:"Trending song " + s, artist:"Public Artist", cover:"",
                    trackIndex:s, trackId:release.tracks[s].id, kind:"songs"});
            }
        } else if (key === "artists") {
            for (var p = 0; p < count; ++p) {
                items.push({id:"ytm:artist:" + p, artistId:"ytm:artist:" + p,
                    title:"Public artist " + p, artist:"Public artist " + p,
                    cover:"", round:true, kind:"artists"});
            }
        } else if (key === "charts") {
            for (var c = 0; c < count; ++c) {
                items.push({id:"ytm:playlist:" + c, title:"Chart " + c,
                    artist:"Public Artist", cover:"", kind:"catalogue-playlists"});
            }
        }
        var titles = {"new-releases":"New releases", trending:"Trending",
            artists:"Artists", charts:"Charts"};
        var kinds = {"new-releases":"albums", trending:"songs",
            artists:"artists", charts:"catalogue-playlists"};
        return {version:1, key:key, title:titles[key], kind:kinds[key],
            items:items, records:records, source:"youtube-music", region:"IN",
            fetchedAt:"2026-10-21T12:00:00Z", complete:false};
    }

    function latestCall(lane) {
        for (var i = source.calls.length - 1; i >= 0; --i)
            if (source.calls[i].lane === lane) return source.calls[i];
        return null;
    }

    function discoveryCall(key, offset) {
        var wanted = "discover-" + key;
        var matches = source.calls.filter(function(call) {
            return call.lane === wanted && call.operation === "discover";
        });
        return matches[offset === undefined ? matches.length - 1 : offset] || null;
    }

    function enterDiscover() {
        world.enterDiscoverSearch(true, false);
        tryVerify(function() {
            return source.calls.filter(function(call) { return call.operation === "discover"; }).length === 4;
        });
    }

    function completeDiscovery(key, call, data, error) {
        source.completed("discover-" + key, call.id, data, error || "");
    }

    function completeAll(count) {
        ["new-releases", "trending", "artists", "charts"].forEach(function(key) {
            completeDiscovery(key, discoveryCall(key, 0), envelope(key, count || 1), "");
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
        source = discoverySourceFactory.createObject(test);

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

    function test_discoveryFanoutPreservesTypedSourceOrderAndFortyItemBound() {
        enterDiscover();
        var keys = ["new-releases", "trending", "artists", "charts"];
        compare(source.calls.length, 4);
        for (var i = 0; i < keys.length; ++i) {
            var call = source.calls[i];
            compare(call.lane, "discover-" + keys[i]);
            compare(call.operation, "discover");
            compare(call.key, keys[i]);
            compare(call.options.refresh, false);
        }

        completeDiscovery("new-releases", discoveryCall("new-releases", 0), envelope("new-releases", 2));
        completeDiscovery("trending", discoveryCall("trending", 0), envelope("trending", 40));
        completeDiscovery("artists", discoveryCall("artists", 0), envelope("artists", 2));
        completeDiscovery("charts", discoveryCall("charts", 0), envelope("charts", 2));

        var shelves = world.worldData.shelves;
        compare(shelves.length, 4);
        for (var s = 0; s < shelves.length; ++s)
            compare(shelves[s].key, "discover-" + keys[s]);
        compare(shelves[0].kind, "albums");
        compare(shelves[0].items[0].id, "release-0");
        compare(shelves[0].items[1].id, "release-1");
        compare(shelves[1].kind, "songs");
        compare(shelves[1].items.length, 40);
        compare(shelves[1].items[0].id, "trending-song-0");
        compare(shelves[1].items[39].id, "trending-song-39");
        compare(shelves[2].kind, "artists");
        compare(shelves[2].items[0].artistId, "ytm:artist:0");
        compare(shelves[3].kind, "catalogue-playlists");
        compare(shelves[3].items[0].id, "ytm:playlist:0");
        compare(world.worldData.findAlbum("release-0").tracks.length, 2);
        compare(world.worldData.findAlbum("trending-release").tracks[39].id,
                "trending-release-track-39");
    }

    function test_catalogueQueryDoesNotReplacePublicDiscoverySections() {
        enterDiscover();
        completeAll(1);
        var publicId = world.session.discoverySections["new-releases"].items[0].id;

        world.worldData.setQuery("a catalogue query");
        world.worldData.submitSearch();
        var search = latestCall("browse");
        verify(search !== null);
        compare(search.operation, "catalogueSearch");
        source.completed("browse", search.id,
            {albums:[albumRecord("search-result", 1)], artists:[]}, "");
        tryVerify(function() {
            return world.worldData.catalogueMatches.query === "a catalogue query";
        }, 2000, "Catalogue search results should arrive without replacing public discovery sections.");
        compare(world.worldData.shelves[0].key, "local-albums");
        compare(world.session.discoverySections["new-releases"].items[0].id, publicId);

        world.worldData.setQuery("");
        tryCompare(world.worldData, "discoveryLanding", true, 2000);
        compare(world.worldData.shelves.length, 4);
        compare(world.worldData.shelves[0].items[0].id, publicId);
    }

    function test_retryRejectsStaleAndOversizedResultsButRetainsPriorSections() {
        enterDiscover();
        completeAll(1);
        var oldNewRelease = world.session.discoverySections["new-releases"].items[0].id;
        var oldTrending = world.session.discoverySections.trending.items[0].id;
        var stale = discoveryCall("new-releases", 0);

        world.worldData.retryDiscovery();
        var current = discoveryCall("new-releases");
        verify(current.id !== stale.id);
        compare(current.options.refresh, true);
        completeDiscovery("new-releases", stale, envelope("new-releases", 1));
        compare(world.session.discoverySections["new-releases"].items[0].id, oldNewRelease);
        compare(world.session.discoveryRequests["new-releases"], current.id);

        completeDiscovery("new-releases", current, envelope("new-releases", 41));
        completeDiscovery("trending", discoveryCall("trending"), null, "Catalogue is offline.");
        completeDiscovery("artists", discoveryCall("artists"), envelope("artists", 2));
        completeDiscovery("charts", discoveryCall("charts"), envelope("charts", 2));

        compare(world.session.discoverySections["new-releases"].items[0].id, oldNewRelease);
        verify(world.session.discoverySections["new-releases"].error.indexOf("invalid") >= 0);
        compare(world.session.discoverySections.trending.items[0].id, oldTrending);
        compare(world.session.discoverySections.trending.error, "Catalogue is offline.");
        compare(world.session.discoverySections.artists.items.length, 2);
        compare(world.session.discoverySections.charts.items.length, 2);
        compare(world.worldData.shelves.length, 4);
        verify(world.worldData.discoveryError.indexOf("Catalogue is offline.") >= 0);
    }

    function test_cacheRefreshActionAndExpiredReentryRequestFreshData() {
        enterDiscover();
        ["new-releases", "trending", "artists", "charts"].forEach(function(key) {
            var data = envelope(key, 2); data._cached = true;
            completeDiscovery(key, discoveryCall(key), data);
        });
        wait(0);
        var count = source.calls.length;
        world.session.requestDiscovery(false);
        compare(source.calls.length, count);
        var refresh = findChild(world, "vinylDiscoverRetry");
        verify(refresh.visible && refresh.enabled);
        refresh.activate(Qt.MouseFocusReason);
        compare(source.calls.length, count + 4);
        compare(discoveryCall("charts").options.refresh, true);
        world.session.discoveryRequestedAt = Date.now() - 900001;
        world.session.requestDiscovery(false);
        compare(source.calls.length, count + 8);
        compare(discoveryCall("charts").options.refresh, false);
    }

    function test_profileClearCancelsOutstandingDiscoveryAndIgnoresLateReplies() {
        enterDiscover();
        var stale = discoveryCall("new-releases", 0);
        source.profileLeaving();
        tryCompare(world.session, "profileActive", false, 2000);
        compare(Object.keys(world.session.discoverySections).length, 0);
        compare(Object.keys(world.session.discoveryRequests).length, 0);
        ["discover-new-releases", "discover-trending", "discover-artists", "discover-charts"].forEach(function(lane) {
            verify(source.cancellations.indexOf(lane) >= 0,
                "Profile clear must cancel the outstanding " + lane + " request.");
        });

        completeDiscovery("new-releases", stale, envelope("new-releases", 1));
        wait(0);
        compare(Object.keys(world.session.discoverySections).length, 0);
    }

    function test_seeAllKeepsDiscoveryKindCardsAndBackRoute() {
        enterDiscover();
        completeAll(2);
        compare(world.page, "discover");
        compare(world.worldData.activeTab, "discover");

        var artistCard = findChild(world, "vinylCard_discover-artists_0");
        var chartCard = findChild(world, "vinylCard_discover-charts_0");
        verify(artistCard !== null);
        verify(chartCard !== null);
        compare(artistCard.item.kind, "artists");
        compare(chartCard.item.kind, "catalogue-playlists");

        var seeAll = findChild(world, "vinylShelfSeeAll_discover-artists");
        verify(seeAll !== null);
        seeAll.activate(Qt.MouseFocusReason);
        tryCompare(world.worldView, "listKey", "discover-artists", 2000);
        compare(world.page, "discover");
        compare(world.worldData.activeTab, "discover");
        var list = findChild(world, "vinylListPage");
        verify(list !== null);
        compare(list.row.kind, "artists");
        compare(list.row.items.length, 2);
        compare(list.row.items[0].kind, "artists");

        var back = findChild(list, "vinylListBack");
        verify(back !== null && back.visible);
        back.triggered();
        tryCompare(world.worldView, "listKey", "", 2000);
        compare(world.page, "discover");
        compare(world.worldData.activeTab, "discover");
        verify(findChild(world, "vinylShelf_discover-artists") !== null);
        verify(findChild(world, "vinylShelf_discover-charts") !== null);

        var chartsSeeAll = findChild(world, "vinylShelfSeeAll_discover-charts");
        verify(chartsSeeAll !== null);
        chartsSeeAll.activate(Qt.MouseFocusReason);
        tryCompare(world.worldView, "listKey", "discover-charts", 2000);
        list = findChild(world, "vinylListPage");
        verify(list !== null);
        compare(list.row.kind, "catalogue-playlists");
        compare(list.row.items.length, 2);
        compare(list.row.items[0].kind, "catalogue-playlists");
        back = findChild(list, "vinylListBack");
        verify(back !== null && back.visible);
        back.triggered();
        tryCompare(world.worldView, "listKey", "", 2000);
        compare(world.page, "discover");
        compare(world.worldData.activeTab, "discover");
    }
}
