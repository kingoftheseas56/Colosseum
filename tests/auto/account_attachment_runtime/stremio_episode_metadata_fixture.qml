import QtQuick
import "../../../qml/TheatreApi.js" as TheatreApi

// Test-only host for the same nonvisual request/reply path Main.qml owns.
// The fixture exercises the real JS metadata reader and the real native
// stremioSyncState invokable; it exposes no provider envelope or credential.
Item {
    visible: false
    width: 0
    height: 0
    Component.onCompleted: {
        TheatreApi.setRequestAdapter(function(url, done) {
            done({ meta: {
                id: "kitsu:qml-fixture",
                videos: [
                    { id: "kitsu:qml-fixture:s0:e1", season: 0, episode: 1,
                      privateDescription: "not-forwarded" },
                    { id: "kitsu:qml-fixture:s1:e1", season: 1, episode: 1,
                      providerOnly: { nested: true } }
                ]
            } })
        })
        stremioSyncState.setEpisodeMetadataBridgeReady(true)
    }

    Component.onDestruction: TheatreApi.resetRequestAdapter()

    Connections {
        target: stremioSyncState
        function onEpisodeMetadataRequested(requestId, seriesId) {
            TheatreApi.loadMeta("series", seriesId, function(meta) {
                var projection = TheatreApi.stremioEpisodeMetadataProjection(meta, seriesId)
                stremioSyncState.submitEpisodeMetadata(
                    requestId, projection.metadataRootId, projection.episodes)
            })
        }
    }
}
