import QtQuick
import QtTest

TestCase {
    id: test
    name: "VinylPlaybackQueueNativeBridge"
    width: 960
    height: 720
    when: windowShown

    property var host: null
    property var world: null
    property var stalledSource: null
    property var rawAlbum: null
    property var queueRows: []

    Component {
        id: windowFactory
        Window { width: 960; height: 720; visible: true }
    }

    Component {
        id: stalledSourceFactory
        QtObject {
            property int serial: 0
            property var requests: []
            property var cancellations: []
            signal completed(string lane, int requestId, var data, string error)
            signal profileLeaving()
            signal profileEntered()
            signal checkpointRequested()
            signal shuttingDown()

            function request(lane: string, operation: string, key: string): int {
                serial++;
                requests = requests.concat([{lane:lane, operation:operation, key:key, id:serial}]);
                return serial;
            }
            function cancel(lane: string): void { cancellations = cancellations.concat([lane]); }
            function setAlbums(values) {}
            function setCollectionStore(value) {}
        }
    }

    function queueAlbum(path) {
        var tracks = [];
        for (var i = 0; i < 7; ++i) {
            tracks.push({id:"queue-track-" + i, title:"Queue Track " + i,
                artists:["Queue Artist"], artistIds:["queue-artist"], seconds:2,
                disc:1, number:i + 1, available:true, localPath:path});
        }
        return {id:"queue-test-album", title:"Queue Test Album", origin:"local",
            artists:["Queue Artist"], artistIds:["queue-artist"], cover:"", tracks:tracks};
    }

    function init() {
        if (typeof BridgeFixtures === "undefined" || typeof NativeSource === "undefined") {
            skip("Requires the vinyl_native_session_bridge runner.");
            return;
        }
        host = windowFactory.createObject(test);
        var component = Qt.createComponent("../../qml/vinyl/VinylLiveWorld.qml");
        compare(component.status, Component.Ready, component.errorString());
        world = component.createObject(host.contentItem, {width:960, height:720});
        verify(world !== null);

        stalledSource = stalledSourceFactory.createObject(test);
        world.session.source = stalledSource;
        rawAlbum = queueAlbum(BridgeFixtures.createIdentityAudio());
        var edition = world.session.normalize(rawAlbum);
        queueRows = world.session.editionRows(edition);
        compare(queueRows.length, 7);
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
        if (stalledSource) {
            stalledSource.destroy();
            wait(0);
            stalledSource = null;
        }
        rawAlbum = null;
        queueRows = [];
    }

    function trackIds() {
        return world.session.queue.map(function(row) { return row.trackId; });
    }

    function queueIds() {
        return world.session.queue.map(function(row) { return row.queueId; });
    }

    function startStalled(rows, index) {
        verify(world.session.appendRows(rows, false));
        world.session.playQueue(index === undefined ? 0 : index);
        tryVerify(function() { return stalledSource.requests.length > 0; }, 1500,
            "The native session should request the active row from the source.");
    }

    function test_occurrenceIdsDistinguishDuplicatesAndPlayNextPrecedesAppend() {
        var a = queueRows[0], b = queueRows[1], c = queueRows[2], d = queueRows[3];
        verify(world.session.appendRows([a, b, a], false));
        world.session.playQueue(0);
        tryVerify(function() { return stalledSource.requests.length > 0; });

        var before = world.session.queue;
        compare(before.length, 3);
        compare(before[0].trackId, a.trackId);
        compare(before[2].trackId, a.trackId);
        verify(before[0].queueId !== before[2].queueId,
            "Repeated recordings need distinct native queue occurrence IDs.");
        verify(before.every(function(row) { return !!row.queueId && row.queueId.indexOf("queue:") === 0; }));
        compare(new Set(queueIds()).size, 3);

        verify(world.session.appendRows([c], true));
        verify(world.session.appendRows([d], false));
        compare(JSON.stringify(trackIds()), JSON.stringify([a.trackId, c.trackId, b.trackId, a.trackId, d.trackId]));
        compare(new Set(queueIds()).size, 5);
    }

    function test_appendToEmptyQueueActivatesPausedFirstOccurrence() {
        verify(world.session.appendRows([queueRows[0], queueRows[1]], false));
        compare(world.session.queue.length, 2);
        compare(world.session.queueIndex, 0,
            "The first row appended to an empty queue should become the active row.");
        compare(world.session.queue[world.session.queueIndex].trackId, queueRows[0].trackId);
        verify(world.session.paused, "Appending must expose a paused queue without starting audio.");
        compare(stalledSource.requests.length, 0,
            "Queue insertion must not resolve a source until the user starts playback.");
    }

    function test_moveAndRemoveCurrentPreservePausedState() {
        var a = queueRows[0], b = queueRows[1], c = queueRows[2];
        startStalled([a, b, c], 1);
        world.session.paused = true;
        tryCompare(world.session, "paused", true);

        var currentId = world.session.queue[world.session.queueIndex].queueId;
        world.session.moveQueue(world.session.queueIndex, 2);
        compare(world.session.queue[world.session.queueIndex].queueId, currentId);
        compare(world.session.queue[world.session.queueIndex].trackId, b.trackId);
        verify(world.session.paused, "Moving the current row must not resume playback.");

        world.session.removeQueue(world.session.queueIndex);
        compare(world.session.queue.length, 2);
        compare(world.session.queue[world.session.queueIndex].trackId, c.trackId);
        verify(world.session.paused, "Removing the current row must keep the next row paused.");
    }

    function test_shuffleUnshuffleRemainsCoherentAfterInsertAndRemove() {
        var a = queueRows[0], b = queueRows[1], c = queueRows[2], d = queueRows[3], e = queueRows[4];
        var f = queueRows[5], g = queueRows[6];
        startStalled([a, b, c, d, e], 0);
        var originalIds = queueIds();
        world.session.setShuffle(true);
        verify(world.session.shuffleEnabled);
        compare(world.session.queue[world.session.queueIndex].queueId, originalIds[0]);

        verify(world.session.appendRows([f], true));
        verify(world.session.appendRows([g], false));
        var removeIndex = world.session.queue.findIndex(function(row) { return row.trackId === c.trackId; });
        verify(removeIndex >= 0);
        world.session.removeQueue(removeIndex);
        world.session.setShuffle(false);

        verify(!world.session.shuffleEnabled);
        compare(JSON.stringify(trackIds()), JSON.stringify([
            a.trackId, f.trackId, b.trackId, d.trackId, e.trackId, g.trackId
        ]));
        compare(new Set(queueIds()).size, 6);
        compare(world.session.queue[world.session.queueIndex].trackId, a.trackId);
    }

    function test_clearingShuffledQueueResetsModeForNextQueue() {
        startStalled(queueRows.slice(0, 4), 0);
        world.session.setShuffle(true);
        verify(world.session.shuffleEnabled);

        world.session.clearQueue();
        compare(world.session.queue.length, 0);
        verify(!world.session.shuffleEnabled,
            "Clearing the native queue must also clear its shuffle mode.");
        world.session.setShuffle(true);
        verify(!world.session.shuffleEnabled,"An empty queue cannot enter shuffle mode.");

        verify(world.session.appendRows(queueRows.slice(0, 4), false));
        world.session.setShuffle(true);
        verify(world.session.shuffleEnabled,
            "A fresh queue must be able to enter shuffle after clearing the old one.");
    }

    function test_repeatCyclesAndManualNavigationRespectBoundaries() {
        var a = queueRows[0], b = queueRows[1];
        startStalled([a, b], 0);
        compare(world.session.repeatMode, "off");
        world.session.next(-1);
        compare(world.session.queueIndex, 0, "Repeat off must stay at the first-row previous boundary.");

        world.session.cycleRepeat();
        compare(world.session.repeatMode, "all");
        world.session.next(-1);
        compare(world.session.queueIndex, 1, "Repeat all wraps previous navigation to the last row.");

        world.session.cycleRepeat();
        compare(world.session.repeatMode, "one");
        world.session.next(1);
        compare(world.session.queueIndex, 1, "Manual next at the final row must not wrap in repeat one.");

        world.session.cycleRepeat();
        compare(world.session.repeatMode, "off");
        world.session.next(-1);
        compare(world.session.queueIndex, 0);
    }

    function test_clearCancelsPendingNativeSourceResolution() {
        startStalled([queueRows[0]], 0);
        world.session.clearQueue();
        compare(world.session.queue.length, 0);
        compare(world.session.queueIndex, -1);
        verify(stalledSource.cancellations.indexOf("play") >= 0,
            "Clearing the queue must cancel the active track resolution lane.");
    }

    function test_manualNextRetainsPauseWhileQueuePlayStartsPlayback() {
        startStalled([queueRows[0],queueRows[1]],0);
        world.session.paused=true;
        world.session.next(1);
        compare(world.session.queueIndex,1);verify(world.session.paused);
        world.session.playQueue(0);
        compare(world.session.queueIndex,0);verify(!world.session.paused);
    }

    function test_profileRetirementCancelsPendingNativeSourceResolution() {
        startStalled([queueRows[0]], 0);
        stalledSource.profileLeaving();
        tryCompare(world.session, "profileActive", false);
        compare(world.session.queue.length, 0);
        verify(stalledSource.cancellations.indexOf("play") >= 0,
            "Leaving the profile must cancel the active track resolution lane.");
    }

    function test_shortNativeWavLoadsAndEndsAtRepeatOffBoundary() {
        NativeSource.setAlbums([]);
        var path = BridgeFixtures.createIdentityAudio();
        BridgeFixtures.changeIdentityAudio(path, 16000);
        var result = NativeSource.importFiles([path]);
        compare(result.errors.length, 0);
        compare(result.albums.length, 1);
        NativeSource.setAlbums(result.albums);
        world.session.source = NativeSource;

        var edition = world.session.normalize(result.albums[0]);
        var rows = world.session.editionRows(edition);
        compare(rows.length, 1);
        verify(world.session.appendRows(rows, false));
        world.session.playQueue(0);
        tryVerify(function() { return world.session.loaded || world.session.error.length > 0; }, 8000,
            "The native WAV should resolve and load in the real playback adapter.");
        verify(world.session.loaded, world.session.error);
        world.session.paused = true;
        tryCompare(world.session, "paused", true);
        world.session.paused = false;
        tryCompare(world.session, "paused", false);
        tryVerify(function() {
            return world.session.sourceState === "ended" || world.session.error.length > 0;
        }, 8000, "The native WAV should resolve, load, and reach natural EOF.");
        compare(world.session.error, "");
        compare(world.session.sourceState, "ended");
        compare(world.session.queueIndex, 0);
        compare(world.session.queue.length, 1);
    }
}
