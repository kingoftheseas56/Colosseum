import QtQuick
import QtTest

// Exercise the production routing functions with recording services. This avoids
// loading the video renderer while checking the context that reaches its entry points.
TestCase {
    id: testCase
    name: "SmartPlayContext"
    property var sources: ({})
    property var host: null

    function initTestCase() {
        for (const file of ["Main", "PlayerPage", "TheatreSeries", "TheatreSeriesView"]) {
            const request = new XMLHttpRequest()
            request.open("GET", Qt.resolvedUrl("../../qml/" + file + ".qml"), false)
            request.send()
            verify(request.responseText.length > 0, "Read production " + file + "; enable QML_XHR_ALLOW_FILE_READ")
            sources[file] = request.responseText
        }
    }
    function extract(file, name) {
        const source = sources[file]
        const start = source.indexOf("function " + name + "(")
        verify(start >= 0, "Production function exists: " + name)
        let depth = 1
        const opening = source.indexOf("{", start)
        for (let end = opening + 1; end < source.length; ++end) {
            if (source[end] === "{") ++depth
            else if (source[end] === "}" && --depth === 0) return source.slice(start, end + 1)
        }
        fail("Unterminated production function " + name)
        return ""
    }
    function createHost(body) {
        host = Qt.createQmlObject('import QtQuick\nimport "'
            + Qt.resolvedUrl("../../qml/EpisodeBrowser.js") + '" as EpisodeBrowser\n'
            + 'Item { id: root; property var page: root; property var win: root\n'
            + body + '\n}', testCase)
        return host
    }
    function cleanup() { if (host) { host.destroy(); host = null } }

    function test_titleEntry_data() {
        return [{ tag: "Series", file: "TheatreSeries" }, { tag: "SeriesView", file: "TheatreSeriesView" }]
    }
    function test_titleEntry(data) {
        const entry = createHost([
            'property var sourceMetadata: ({ runtime: "20 min", sourceId: "kitsu:123" })',
            'property string year: "2020"; property string logo: "logo"; property string cover: "cover"',
            'property var emitted: null',
            'function playLocalRequested(payload) { emitted = payload }',
            'function playArrivingRequested(payload) { emitted = payload }',
            'QtObject { id: downloads',
            '  function hasVideo(id) { return true }',
            '  function downloadedVideos() { return [{ id: "tt123:1:2", path: "episode.mkv" }] }',
            '  function jobs() { return [{ id: "tt123:1:2", url: "https://example.org/episode", received: 123 }] }',
            '}',
            'QtObject { id: progress; function get(kind, id) { return ({}) } }',
            extract(data.file, "tryPlayLocal"), extract(data.file, "tryPlayArriving")
        ].join("\n").replace(/\bDownload\b/g, "downloads").replace(/\bProgress\b/g, "progress"))
        verify(entry.tryPlayLocal("tt123:1:2", "Fixture", "episode"))
        compare((entry.emitted.playbackContext || {}).metadata, entry.sourceMetadata)
        verify(entry.tryPlayArriving("tt123:1:2"))
        compare(entry.emitted.received, 123)
        compare((entry.emitted.playbackContext || {}).metadata, entry.sourceMetadata)
    }

    function test_sessionRouting_data() { return [{ tag: "Local", local: true }, { tag: "Arriving", local: false }] }
    function test_sessionRouting(data) {
        const router = createHost([
            'property var opened: null',
            'function setGuiStallContext(action, surface) {}',
            'function closeDownloadsPage() {}',
            'QtObject { id: sessionStore; function openOrSwitch(value) { root.opened = value } }',
            'QtObject { id: progress; function get(kind, id) { return ({}) } }',
            extract("Main", "openLocalVideoSession"), extract("Main", "routeArrivingPlay")
        ].join("\n").replace(/\bSessions\b/g, "sessionStore").replace(/\bProgress\b/g, "progress"))
        const target = { id: "tt123:1:2", path: "episode.mkv", url: "https://example.org/episode",
            playbackContext: { metadata: { runtime: "20 min", sourceId: "kitsu:123" }, year: "2020" } }
        if (data.local) router.openLocalVideoSession(target)
        else router.routeArrivingPlay(target)
        compare((router.opened.target.playbackContext || {}).metadata, target.playbackContext.metadata)
    }

    function test_arrivingHandover_keepsMetadata() {
        const player = createHost([
            'property string arrivingStreamUrl: "https://example.org/episode"',
            'property var arrivingStreamHeaders: ({})',
            'property string mediaId: "tt123:1:2"; property string mediaTitle: "Fixture"; property string mediaArt: ""',
            'property string subStreamType: "series"; property bool resumePromptConsumed: false',
            'property var mediaMetadata: ({ runtime: "20 min", sourceId: "kitsu:123" })',
            'property var opened: null',
            'function playRemoteUrl(target) { opened = target }',
            'QtObject { id: mpv; property real position: 30 }',
            extract("PlayerPage", "switchArrivingToStream")
        ].join("\n"))
        player.switchArrivingToStream()
        compare((player.opened.playbackContext || {}).metadata, player.mediaMetadata)
        compare(player.opened.position, 28)
        verify(player.resumePromptConsumed)
    }

    function test_hydration_retainsOriginalAnimeIdentity() {
        const player = createHost([
            'property int hydrateGen: 0; property string mediaId: "tt123:1:2"',
            'property string mediaTitle: "Fixture"; property string mediaArt: ""; property string mediaYear: "2020"',
            'property string mediaLocalPath: "episode.mkv"; property var streamCandidates: []',
            'property var mediaMetadata: ({ runtime: "20 min", sourceId: "kitsu:123" })',
            'property var playbackQueue: []; property int playbackQueueIndex: -1',
            'property var adjacentEpisodes: ({}); property string playbackQueueOrderingMode: ""',
            'property var callback: null',
            'function hasAdjacentEpisode(direction) { return false }',
            'function resolveAdjacentContext(context) { return ({}) }',
            'QtObject { id: metaApi; function loadMeta(type, id, cb) { root.callback = cb } }',
            'QtObject { id: browser',
            '  function isEpisodeId(id) { return true }',
            '  function seriesRootId(id) { return id.split(":")[0] }',
            '  function showTitleFrom(title) { return title }',
            '  function queueContextFromOrder(order, id, title, art, year) { return { episodeQueue: [], episodeIndex: 0 } }',
            '}',
            extract("PlayerPage", "maybeHydrateContext")
        ].join("\n").replace(/\bTheatreApi\b/g, "metaApi").replace(/\bEpisodeBrowser\b/g, "browser"))
        player.maybeHydrateContext()
        player.callback({ runtime: "23 min", aliases: ["Alternate"] })
        compare(player.mediaMetadata.sourceId, "kitsu:123")
        compare(player.mediaMetadata.runtime, "23 min")
    }
}
