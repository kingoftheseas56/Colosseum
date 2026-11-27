import QtQuick
import "file:///app/share/colosseum/qml" as Installed

Installed.Main {
    id: app
    width: 1280
    height: 720
    property int ticks: 0
    property int stage: 0
    property real firstPosition: -1
    property real seekObserved: -1
    property var playbackEvidence: ({})
    readonly property string fixtureUrl: Qt.application.arguments.length > 2
                                         ? Qt.application.arguments[2] : ""
    readonly property string framePath: Qt.application.arguments.length > 3
                                        ? Qt.application.arguments[3] : ""

    function findItem(item, predicate) {
        if (!item) return null
        if (predicate(item)) return item
        var children = item.children || []
        for (var i = 0; i < children.length; ++i) {
            var found = findItem(children[i], predicate)
            if (found) return found
        }
        return null
    }

    function finish(code, message, evidence) {
        cycle.stop()
        console.info("INSTALLED_PLAYER_PROOF=" + JSON.stringify({
            status: code === 0 ? "PASS" : "FAIL",
            exit_code: code,
            message: message,
            ticks: ticks,
            player: evidence || playbackEvidence
        }))
        Qt.exit(code)
    }

    Timer {
        id: cycle
        interval: 200
        running: true
        repeat: true
        onTriggered: {
            app.ticks++
            if (app.ticks > 500) {
                app.finish(10, "installed Player 1 proof timed out")
                return
            }
            if (app.stage === 0) {
                var splash = app.findItem(app.contentItem,
                                          function(item) { return item.objectName === "bootSplash" })
                if (!splash || splash.visible || AccountController.busy) return
                if (!app.fixtureUrl.length || !app.framePath.length) {
                    app.finish(11, "fixture or frame path was not passed to the QML driver")
                    return
                }
                AccountController.continueWithoutAccount()
                app.showNormal()
                app.width = 1280
                app.height = 720
                var cold = { loaderActive: app.playerLayerRef.active,
                             player2Booted: app.usePlayer2 }
                console.info("INSTALLED_PLAYER_COLD=" + JSON.stringify(cold))
                if (cold.loaderActive || cold.player2Booted) {
                    app.finish(12, "candidate did not start with a cold Player 1 loader", cold)
                    return
                }
                app.openLocalVideoSession({ id: "installed-player-fixture",
                                            path: app.fixtureUrl,
                                            title: "Installed Player 1 fixture",
                                            kind: "movie", position: 0 })
                app.stage = 1
                return
            }
            if (app.stage === 1) {
                var loader = app.playerLayerRef
                if (loader.status === Loader.Error) {
                    app.finish(13, "installed player Loader.Error")
                    return
                }
                if (loader.status !== Loader.Ready || !loader.item) return
                var player = loader.item
                if (player.errored) {
                    app.finish(14, "Player 1 reported a playback error after construction")
                    return
                }
                var width = Number(player.decodedWidth || 0)
                var height = Number(player.decodedHeight || 0)
                if (!player.playerReady || !player.playbackStarted || width <= 0 || height <= 0)
                    return
                var position = Number(player.playbackPosition || 0)
                if (app.firstPosition < 0) {
                    app.firstPosition = position
                    return
                }
                if (position <= app.firstPosition + 0.2) return
                app.playbackEvidence = {
                    loaderReady: loader.status === Loader.Ready,
                    playerReady: player.playerReady === true,
                    playbackStarted: player.playbackStarted === true,
                    firstPosition: app.firstPosition,
                    position: position,
                    decodedWidth: width,
                    decodedHeight: height,
                    duration: Number(player.playbackDuration || 0)
                }
                var engine = app.findItem(player, function(item) {
                    return item.objectName === "playerMpv"
                })
                if (!engine) {
                    app.finish(17, "native Player 1 MpvItem not found")
                    return
                }
                var selected = player.firstSelectedTrack(player.audioRows)
                var channels = Number(engine.playbackStat("audioChannelCount"))
                var bitrate = Number(engine.playbackStat("audioBitrate"))
                if (!selected || !(channels > 0) || !(bitrate > 0)) return
                app.playbackEvidence.audioChannels = channels
                app.playbackEvidence.audioBitrate = bitrate
                app.playbackEvidence.seekTarget = Math.min(4.0, app.playbackEvidence.duration - 2)
                if (!(app.playbackEvidence.seekTarget > position + 1.5)) {
                    app.finish(18, "fixture too short or playback too far ahead to prove a seek")
                    return
                }
                app.stage = 2
                player.seekTo(app.playbackEvidence.seekTarget)
                return
            }
            if (app.stage === 2) {
                var player = app.playerLayerRef.item
                if (!player || player.errored) {
                    app.finish(19, "Player 1 errored during seek")
                    return
                }
                var position = Number(player.playbackPosition)
                var target = app.playbackEvidence.seekTarget
                if (app.seekObserved < 0) {
                    if (Math.abs(position - target) > 0.6) return
                    app.seekObserved = position
                    return
                }
                if (position <= app.seekObserved + 0.2) return
                app.playbackEvidence.positionAfterSeek = app.seekObserved
                app.playbackEvidence.positionAfterResume = position
                app.stage = 4
                var accepted = app.contentItem.grabToImage(function(result) {
                    if (!result.saveToFile(app.framePath)) {
                        app.finish(15, "could not save the decoded player frame")
                        return
                    }
                    app.closePlayerSession()
                    app.stage = 3
                })
                if (!accepted) app.finish(16, "could not request the player frame capture")
                return
            }
            if (app.stage === 3 && !app.playerOpen) {
                app.finish(0, "installed Player 1 decoded local audio and video and resumed after seeking",
                           app.playbackEvidence)
            }
        }
    }
}
