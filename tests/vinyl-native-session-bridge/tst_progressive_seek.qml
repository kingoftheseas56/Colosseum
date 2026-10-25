import QtQuick
import QtTest

// Real MpvItem over a loopback progressive WAV origin. The first HTTP response
// stalls after 20 seconds; a seek to 90 seconds must fetch the available target bytes.
TestCase {
    id: test
    name: "VinylProgressiveHttpSeek"
    when: windowShown
    property var session
    property var source
    property string progressiveUrl: ""
    property var observedNativeEvents: []
    property var observedSeekReplies: []
    property var loadedEntryId: null

    Connections {
        target: test.session ? test.session.player : null
        ignoreUnknownSignals: true
        function onNativePlaybackEvent(kind, entryId, position, duration, load, monotonic, observationSerial) {
            var events = test.observedNativeEvents.slice();
            events.push({kind:kind,entryId:entryId,position:position,duration:duration,
                         load:load,monotonic:monotonic,observationSerial:observationSerial});
            test.observedNativeEvents = events;
            if (kind === "loaded") test.loadedEntryId = entryId;
        }
        function onTrackedSeekReply(serial, success, load, observationSerial) {
            var replies = test.observedSeekReplies.slice();
            replies.push({serial:serial,success:success,load:load,observationSerial:observationSerial});
            test.observedSeekReplies = replies;
        }
    }

    Component {
        id: transport
        QtObject {
            property int serial: 0
            property var calls: []
            property string torrentUrl: ""
            property string fallbackUrl: ""
            signal completed(string lane, int requestId, var data, string error)
            signal profileLeaving()
            signal profileEntered()
            signal checkpointRequested()
            signal shuttingDown()
            function setAlbums(entries) {}
            function cancel(lane: string) {}
            function request(lane: string, operation: string, value: string): int {
                var id = ++serial;
                calls = calls.concat([{operation:operation,track:value}]);
                Qt.callLater(function() {
                    completed(lane, id, {
                        trackId:value,
                        url:operation === "resolve" ? torrentUrl : fallbackUrl,
                        provider:operation === "resolve" ? "torrent" : "youtube",
                        headers:{"X-Vinyl-Seek-Test":"required"},
                        qualityLabel:"Progressive HTTP contract"
                    }, "");
                });
                return id;
            }
        }
    }

    function init() {
        observedNativeEvents = [];
        observedSeekReplies = [];
        loadedEntryId = null;
        BridgeFixtures.stopProgressiveHttpFixture();
        progressiveUrl = BridgeFixtures.startProgressiveHttpFixture();
        verify(progressiveUrl.length > 0, "The loopback progressive WAV server must start.");

        source = transport.createObject(test);
        source.torrentUrl = progressiveUrl;
        source.fallbackUrl = BridgeFixtures.progressiveHttpWavUrl();
        var component = Qt.createComponent("../../qml/vinyl/VinylLiveSession.qml");
        compare(component.status, Component.Ready, component.errorString());
        session = component.createObject(test, {source:source});
        verify(session !== null);
        session.shownEdition = session.normalize({
            id:"progressive-album", browseId:"progressive-album", origin:"youtube-music",
            title:"Album", artists:["Artist"], tracks:[{
                id:"progressive-track", videoId:"abcdefghijk", title:"Song", artists:["Artist"],
                seconds:120, available:true
            }]
        });
    }

    function cleanup() {
        if (session) {
            console.log("VINYL_PROGRESSIVE_RESULT",JSON.stringify({source:session.sourceKind,position:session.position,pending:session.seekPending,error:session.error,
                offsets:BridgeFixtures.progressiveHttpRequestOffsets(),ranges:BridgeFixtures.progressiveHttpServedRanges(),
                events:observedNativeEvents,replies:observedSeekReplies}));
            session.clearProfile();
            session.destroy();
            session = null;
            wait(0);
        }
        if (source) {
            source.destroy();
            source = null;
            wait(0);
        }
        BridgeFixtures.stopProgressiveHttpFixture();
    }

    function targetBytesFetched(targetSeconds) {
        var expected = 44 + targetSeconds * 8000 * 2;
        var ranges = BridgeFixtures.progressiveHttpServedRanges();
        // Require bytes from a completed response; a held sequential response
        // is recorded only through the available twenty-second frontier.
        for (var i = 1; i < ranges.length; ++i) {
            if (!ranges[i].held && Number(ranges[i].start) <= expected
                && Number(ranges[i].end) >= expected + 1600)
                return true;
        }
        return false;
    }

    function test_seekInterruptsHeldReadAndFetchesAvailableRange() {
        session.playTrack(0);
        tryVerify(function() {
            return session.loaded && session.sourceKind === "torrent" && loadedEntryId !== null;
        }, 15000);
        compare(source.calls.length, 1);
        compare(source.calls[0].operation, "resolve");

        // Prove this is a read that actually progressed and then reached the held
        // frontier, rather than an HTTP source that never started.
        var maximumPosition = 0;
        var lastAdvanceAt = Date.now();
        tryVerify(function() {
            var position = Number(session.position);
            if (position > maximumPosition + 0.025) {
                maximumPosition = position;
                lastAdvanceAt = Date.now();
            }
            return maximumPosition > 17
                && Date.now() - lastAdvanceAt >= 1200
                && BridgeFixtures.progressiveHttpIsHolding();
        }, 30000);

        verify(BridgeFixtures.progressiveHttpIsHolding(), "The initial HTTP read must still be held at the frontier.");
        var occurrence = session.queue[0].queueId;
        var player = session.player;
        var load = player.playbackGeneration;
        var observationFence = player.nativeObservationSerial();
        var firstRestartIndex = observedNativeEvents.length;
        session.seek(90.0);
        verify(session.seekPending, "The seek must be pending before the remote range completes.");

        tryVerify(function() {
            var hasRestart = false;
            for (var i = firstRestartIndex; i < observedNativeEvents.length; ++i) {
                var event = observedNativeEvents[i];
                if (event.kind === "restart" && event.entryId === loadedEntryId
                    && event.load === load && event.observationSerial > observationFence) {
                    hasRestart = true;
                    break;
                }
            }
            var acceptedReply = false;
            for (var j = 0; j < observedSeekReplies.length; ++j) {
                var reply = observedSeekReplies[j];
                if (reply.success && reply.load === load) {
                    acceptedReply = true;
                    break;
                }
            }
            return hasRestart && acceptedReply && !session.seekPending
                && session.sourceKind === "torrent"
                && Number(session.position) > 90.1
                && targetBytesFetched(90.0);
        }, 9500);

        compare(session.sourceKind, "torrent");
        compare(session.queue.length, 1);
        compare(session.queue[0].queueId, occurrence);
        compare(session.queueIndex, 0);
        compare(session.playingTrack.id, "progressive-track");
        compare(source.calls.length, 1);
        compare(session.error, "");

        // Reopen a second uncached position while paused. The same HTTP header,
        // queue occurrence and pause intent must survive the native replacement.
        session.paused = true;
        session.seek(5);
        tryVerify(function() {
            return !session.seekPending && session.paused
                && Math.abs(Number(session.position) - 5) < 0.2;
        }, 9500);
        var pausedAt = session.position;
        wait(250);
        verify(Math.abs(session.position - pausedAt) < 0.1);
        compare(session.player, player);
        compare(player.playbackGeneration, load);
        compare(session.sourceKind, "torrent");
        compare(session.queue[0].queueId, occurrence);
        compare(source.calls.length, 1);

        session.seek(0);
        tryVerify(function() {
            return !session.seekPending && session.paused && Number(session.position) < 0.2;
        }, 9500);
        session.seek(45);
        session.seek(75);
        session.seek(90);
        tryVerify(function() {
            return !session.seekPending && session.paused
                && Math.abs(Number(session.position) - 90) < 0.2;
        }, 9500);
        compare(session.player, player);
        compare(player.playbackGeneration, load);
        compare(session.sourceKind, "torrent");
        compare(session.queue[0].queueId, occurrence);
        compare(source.calls.length, 1);
    }
}
