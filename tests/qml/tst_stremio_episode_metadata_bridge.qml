import QtQuick
import QtTest
import "../../qml/TheatreApi.js" as TheatreApi

TestCase {
    name: "StremioEpisodeMetadataBridge"

    QtObject {
        id: sync
        property var submissions: []
        signal episodeMetadataRequested(string requestId, string seriesId)
        function submitEpisodeMetadata(requestId, metadataRootId, episodes) {
            submissions.push({ requestId: requestId, metadataRootId: metadataRootId,
                               episodes: episodes })
            return true
        }
    }

    Connections {
        target: sync
        function onEpisodeMetadataRequested(requestId, seriesId) {
            TheatreApi.loadMeta("series", seriesId, function(meta) {
                var projection = TheatreApi.stremioEpisodeMetadataProjection(meta, seriesId)
                sync.submitEpisodeMetadata(requestId, projection.metadataRootId,
                                           projection.episodes)
            })
        }
    }

    function init() {
        sync.submissions = []
        TheatreApi.resetLiveCaches()
    }

    function cleanup() {
        TheatreApi.resetRequestAdapter()
        TheatreApi.resetLiveCaches()
    }

    function test_orderedEpisodeProjectionUsesOnlyIdentityFields() {
        TheatreApi.setRequestAdapter(function(url, done) {
            done({ meta: { id: "kitsu:alpha", videos: [
                { id: "kitsu:alpha:s0:e1", season: 0, episode: 1,
                  title: "special", deep: { secret: "never-forward" } },
                { id: "kitsu:alpha:s1:e2", season: 1, episode: 2,
                  name: "second" }
            ] } })
        })
        sync.episodeMetadataRequested("request-1", "kitsu:alpha")
        compare(sync.submissions.length, 1)
        compare(sync.submissions[0].requestId, "request-1")
        compare(sync.submissions[0].metadataRootId, "kitsu:alpha")
        compare(JSON.stringify(sync.submissions[0].episodes), JSON.stringify([
            { id: "kitsu:alpha:s0:e1", season: 0, episode: 1 },
            { id: "kitsu:alpha:s1:e2", season: 1, episode: 2 }
        ]))
    }

    function test_identityPivotReturnsNoUsableEpisodes() {
        TheatreApi.setRequestAdapter(function(url, done) {
            done({ meta: { id: "tt123", videos: [
                { id: "tt123:1:1", season: 1, episode: 1 }
            ] } })
        })
        sync.episodeMetadataRequested("request-2", "kitsu:alpha")
        compare(sync.submissions.length, 1)
        compare(sync.submissions[0].metadataRootId, "tt123")
        compare(sync.submissions[0].episodes.length, 0)
    }
}
