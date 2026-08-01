// hosted_player_webengine_smoke.qml — OPT-IN live smoke for the hosted-player surface.
//
// Live VidKing availability is external and non-deterministic, so this is NOT part of
// the deterministic suite. Run it against the real app engine:
//
//   native/build-msvc/colosseum.exe tests/hosted_player_webengine_smoke.qml '<json request>'
//
// With no JSON argument it defaults to Inception (keyless, TMDB 27205). It opens the
// REAL HostedPlayerPage over the real wrapper + WebChannel, logs wrapper progress, and
//   - exits 0 once the bridge delivers any usable VidKing playback event (VIDKING_SMOKE_OK)
//   - exits 3 on wrapper load failure / honest-unavailable panel (VIDKING_SMOKE_FAIL)
//   - exits 2 on a 40s timeout (VIDKING_SMOKE_TIMEOUT)
import QtQuick
import QtWebEngine

Item {
    id: root
    width: 1280; height: 720

    // Env-provided request via argv[2] (a JSON object), else a keyless movie default.
    property var request: {
        var args = Qt.application.arguments
        if (args.length > 2) {
            try { return JSON.parse(args[2]) } catch (e) { console.log("VIDKING_SMOKE_BAD_JSON " + e) }
        }
        return {
            "providerId": "vidking", "extensionId": "net.vidking.player", "type": "movie",
            "imdbId": "tt1375666", "tmdbId": 27205, "season": 0, "episode": 0,
            "mediaId": "tt1375666", "title": "Inception", "backdrop": "", "position": 0
        }
    }

    Component.onCompleted: console.log("VIDKING_SMOKE_START " + JSON.stringify(root.request))

    Loader {
        id: pageLoader
        anchors.fill: parent
        source: "../qml/HostedPlayerPage.qml"
        onLoaded: { console.log("VIDKING_SMOKE_WRAPPER_LOADED"); item.open(root.request) }
    }

    // Wrapper load failure / honest-unavailable panel → distinct nonzero exit.
    Connections {
        target: pageLoader.item
        function onErroredChanged() {
            if (pageLoader.item && pageLoader.item.errored) {
                console.log("VIDKING_SMOKE_FAIL")
                Qt.exit(3)
            }
        }
    }

    // Any usable VidKing playback event → success.
    Connections {
        target: typeof HostedPlayerBridge !== "undefined" ? HostedPlayerBridge : null
        function onPlayerEvent(event) {
            console.log("VIDKING_SMOKE_EVENT " + event.event
                        + " t=" + event.currentTime + " d=" + event.duration)
            if (event.event === "play" || event.event === "playing" || event.event === "timeupdate") {
                console.log("VIDKING_SMOKE_OK")
                Qt.exit(0)
            }
        }
    }

    Timer {
        interval: 40000; running: true; repeat: false
        onTriggered: { console.log("VIDKING_SMOKE_TIMEOUT"); Qt.exit(2) }
    }
}
