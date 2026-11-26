import QtQuick
import QtTest
import "../../qml/vinyl" as Vinyl

TestCase {
    id: test
    name: "VinylUiThreadBudget"
    when: windowShown
    // 50 ms target; allow 2x on the offscreen software runner.
    readonly property int budgetMs: 100
    property var session
    property var world
    property var pool
    property var checkpoint
    property var resumeEntries
    property list<string> qtArtists: ["Artist"]
    property list<var> qtTracks: [{id:"qt-track", title:"Qt track", artists:["Artist"]}]

    QtObject {
        id: recommendations
        property bool profileActive: true
        property string profileScope: "test"
        property bool canEnterVinyl: true
        property string onboardingState: "answered"
        property int homeRevision: 0
        property string homeStatus: "ready"
        property var selectedArtists: [1, 2, 3, 4, 5]
        property var homeSourcePool: ({})
        property var homeSections: []
        property var homeListeningCards: []
        property var homeQuickPicks: []
        property var homeSnapshot: ({historyEnabled: true})
        function beginHomeVisit() {}
        function leaveHome() {}
        function setCratePin(item, pinned, generation) {
            homeSourcePool = Object.assign({}, homeSourcePool, {crate: homeSourcePool.crate.map(row =>
                row.id === item.id ? Object.assign({}, row, {pinned: pinned}) : row)});
            ++homeRevision;
            return true;
        }
    }
    QtObject {
        id: preferences
        function setVinylTasteSelections() { return true; }
    }
    QtObject {
        id: progress
        function get(kind, key) { return kind === "music-session" ? test.resumeEntries[0] : null; }
        function recent(kind, limit) { return test.resumeEntries; }
    }
    QtObject {
        id: nativeSummaryStub
        // No persistence/audio on this runner. The production QML adapter still
        // receives the entire checkpoint and projects the active row's edition.
        property var queue: []
        function replaceRows(rows, index, shuffled) { queue = rows; return true; }
        function checkpointSummary(value) {
            if (!value || !value.rows) return {};
            var ref = value.rows[value.queueIndex];
            var edition = value.editionTable[ref.editionKey];
            var track = edition.discs[0][ref.trackIndex];
            return {valid:true, empty:false, row:Object.assign({}, ref, {edition:edition, track:track,
                title:track.title, artist:track.artist}), duration:120, position:15};
        }
    }
    Component {
        id: worldFactory
        Vinyl.VinylLiveWorldData {
            session: test.session
            recommendationService: recommendations
            preferencesStore: preferences
            searchHistoryStore: null
            legacyCarouselEnabled: false
        }
    }
    function makeSession() {
        // The shared runner does not link the native Vinyl/audio module. Use the
        // production methods verbatim, like other source-extract QML tests.
        var request = new XMLHttpRequest();
        request.open("GET", Qt.resolvedUrl("../../qml/vinyl/VinylLiveSession.qml"), false);
        request.send();
        var code = "";
        var helpers = ["albumData", "albumEdition", "trackRow"]; // Not present in the negative control.
        var methods = ["plainData", "checkpointSummary", "validAlbum", "cacheCrateRecord", "normalizeEditions",
            "normalize", "creditGroups", "formatTime", "playAlbum", "albumTrackRow", "editionRows", "playableRows", "rowFor"];
        for (var name of methods.concat(helpers)) {
            var prefix = "^    function " + name + "\\(";
            var method = new RegExp(prefix + "[^\\r\\n]*\\}\\r?$|" + prefix + "[^]*?\\n    \\}", "m").exec(request.responseText);
            if (!method && helpers.includes(name)) continue;
            verify(method, "Find production session method: " + name);
            code += method[0] + "\n";
        }
        return Qt.createQmlObject(`import QtQuick; Item {
            property var backend: nativeSummaryStub
            property var progressStore: progress
            property var library: []
            property var collectionStore: null
            property var discoverySections: ({})
            property bool profileActive: true
            property bool shuttingDown: false
            property int profileGeneration: 0
            property bool browsing: false
            property string browseError: ""
            property string sourceState: "playing"
            property real position: 15
            property real duration: 120
            function personalRowAvailable(row) { return true; }
            function manualPlaybackStarted() {}
            function snapshot() {}
            function refreshSource(rows) {}
            function cancelQueueRequest() {}
            ${code}
        }`, test, "VinylBudgetSession");
    }
    function album(id) {
        // Same canonical album/track shape as tst_vinyl_live_world_state, with
        // full albums to model records carried by Home and a populated queue.
        var tracks = [];
        for (var i = 0; i < 16; ++i)
            tracks.push({id:id+"-song-"+i, title:"Fixture song "+i, artists:["Artist"], artist:"Artist",
                artistIds:["artist"], disc:1, number:i+1, seconds:180+i, videoId:"aBcDeFgH123"});
        return {id:id, title:"Fixture release "+id, origin:"local", artists:["Artist"],
            artistIds:["artist"], year:2000, genre:"Jazz", tracks:tracks};
    }
    function initTestCase() {
        session = makeSession();
        var cards = [], editions = [], refs = [];
        for (var i = 0; i < 72; ++i) {
            var record = album("release-"+i);
            cards.push({id:record.id, albumId:record.id, kind:"albums", title:record.title,
                artist:"Artist", cover:"", record:record});
            editions.push({id:record.id, sourceAlbum:record, discs:[record.tracks]});
            refs.push({editionKey:i, discIndex:0, trackIndex:i%16, editionId:record.id, albumId:record.id,
                trackId:record.tracks[i%16].id});
        }
        pool = {crate:cards.slice(0,16), albums:cards.slice(0,48), artists:[], songs:[], rows:[],
            charts:{albums:cards.slice(0,20), songs:[], artists:[]}};
        for (var j = 0; j < 8; ++j)
            pool.rows.push({key:"shelf-"+j, title:"Shelf "+j, items:cards.slice(j*6,j*6+24)});
        checkpoint = {version:3, queueIndex:0, rows:refs, editionTable:editions};
        resumeEntries = [];
        for (var k = 0; k < 8; ++k)
            resumeEntries.push({kind:"music", id:cards[k].id, resume:Object.assign({},checkpoint,{queueIndex:k})});
    }
    function init() {
        recommendations.homeSourcePool = pool;
        world = createTemporaryObject(worldFactory, test);
        verify(world);
        compare(world.crateItems.length, 16);
        compare(world.v1HomeRows.length, 8);
    }
    function timed(label, action) {
        var start = Date.now();
        action();
        var elapsed = Date.now()-start;
        console.info("UI budget: " + label + " = " + elapsed + " ms (limit " + budgetMs + " ms)");
        verify(elapsed < budgetMs, label + " took " + elapsed + " ms");
    }
    function test_selectTab() {
        timed("selectTab Spin", function() { world.selectTab("albums"); });
        compare(world.activeTab, "albums");
        timed("selectTab home", function() { world.selectTab("home"); });
        compare(world.activeTab, "home");
        tryVerify(function() { return world.recentContinue.length === 6; });
        timed("refreshHome (deferred work)", function() { world.refreshHome(); });
        compare(world.recentContinue.length, 6);
    }
    function test_playAlbum() {
        timed("playAlbum", function() { session.playAlbum(pool.albums[0].record, 8); });
        compare(nativeSummaryStub.queue.length, 16);
        compare(nativeSummaryStub.queue[8].trackId, pool.albums[0].record.tracks[8].id);
    }
    function test_albumAdapterPreservesSequenceAndRejectsArrayLikeObject() {
        verify(!Array.isArray(qtArtists) || !Array.isArray(qtTracks), "Exercise a Qt sequence wrapper");
        var record = {id:"qt-album", artists:qtArtists, tracks:qtTracks};
        var adapt = typeof session.albumData === "function" ? session.albumData : session.plainData;
        var data = adapt(record);
        verify(Array.isArray(data.artists));
        verify(Array.isArray(data.tracks));
        compare(data, JSON.parse(JSON.stringify(record)));
        verify(session.validAlbum(data));
        var malformed = adapt({id:"bad", tracks:{0:{id:"track"},length:1}});
        verify(!Array.isArray(malformed.tracks));
        verify(!session.validAlbum(malformed));
    }
    function test_pinCrateItem() {
        var item = world.crateItems[0], pinned;
        timed("pinCrateItem", function() { pinned = world.pinCrateItem(item, true); });
        verify(pinned);
        compare(world.crateItems[0].pinned, true);
    }
    function test_checkpointSummary50() {
        var summary;
        timed("checkpointSummary x50", function() {
            for (var i = 0; i < 50; ++i) summary = session.checkpointSummary(resumeEntries[i%8].resume);
        });
        verify(summary.valid);
        compare(summary.row.trackId, checkpoint.rows[1].trackId);
    }
    function test_homeSnapshotRefresh() {
        var updated = Object.assign({}, pool, {rows: pool.rows.map(row => Object.assign({}, row, {title:row.title+" updated"}))});
        var projected;
        timed("home snapshot refresh", function() {
            recommendations.homeSourcePool = updated;
            ++recommendations.homeRevision;
            projected = [world.v1HomeRows.length, world.spinAlbums.length, world.crateItems.length];
        });
        compare(projected, [8, 48, 16]);
        compare(world.v1HomeRows[0].title, "Shelf 0 updated");
        var before = world.v1HomeRows, spin = world.spinAlbums, crate = world.crateItems;
        recommendations.homeSourcePool = Object.assign({}, updated, {pendingCount:1});
        ++recommendations.homeRevision;
        verify(world.v1HomeRows === before);
        verify(world.spinAlbums === spin);
        verify(world.crateItems === crate);
    }
}
