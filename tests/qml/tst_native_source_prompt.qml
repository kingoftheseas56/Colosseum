import QtQuick
import QtQuick.Window
import QtTest
import "../../qml" as App
import "../../qml/ComicsApi.js" as ComicsApi

TestCase {
    id: suite
    name: "NativeSourcePrompt"
    when: windowShown
    Window { id: host; width: 1280; height: 800; visible: true }
    App.Theme { id: theme }
    QtObject {
        id: extensions
        property int revision: 0
        property var rows: []
        function installed() { return rows }
        function setRows(value) { rows = value; revision++ }
    }
    property var originalPoster
    QtObject {
        id: comics
        property int downloads: 0
        signal progress(string id, double done, double total)
        signal finished(string id)
        signal failed(string id, string reason)
        signal removed(string id)
        function statusOf(id) { return {state: "none", done: 0, total: 0} }
        function localPages(id) { return [] }
        function downloadIssue(id, url, seriesId, title, label, bytes) { downloads++ }
    }
    Component { id: liveComponent; App.ComicSeries {} }
    Component { id: ledgerComponent; App.ComicDbLedger {} }
    SignalSpy { id: route; signalName: "openExtensionsRequested" }
    function initTestCase() {
        originalPoster = ComicsApi.posterFor
        ComicsApi.posterFor = function(query, done) { done(null) }
    }
    function cleanupTestCase() { ComicsApi.posterFor = originalPoster }
    function init() { extensions.setRows([]); route.clear(); comics.downloads = 0 }
    function test_catalogue_remains_when_download_extension_is_missing_data() {
        return [{tag: "live-shelf", live: true}, {tag: "catalogue-ledger", live: false}]
    }
    function test_catalogue_remains_when_download_extension_is_missing(data) {
        const item = data.live
            ? createTemporaryObject(liveComponent, host.contentItem, {
                width: host.width, height: host.height, extensionsRef: extensions,
                seriesTitle: "Catalogue fixture", bakedReleases: [{id: "issue", name: "Issue #1", sizeMB: 1, url: "https://example.test/post",
                    cover: Qt.resolvedUrl("../../assets/addon-logos/getcomics.png").toString()}],
                comicsRef: comics, progressRef: null,
                poster: Qt.resolvedUrl("../../assets/addon-logos/getcomics.png").toString()
            })
            : createTemporaryObject(ledgerComponent, host.contentItem, {
                width: host.width, theme: theme, extensionsRef: extensions,
                dbSeries: {title: "Catalogue fixture", editions: [{id: "issue", title: "Issue #1", format: "Other"}]},
                comicsApi: null
            })
        verify(item !== null)
        route.target = item
        const prefix = data.live ? "comicSource" : "comicLedgerSource"
        const action = findChild(item, prefix + "ExtensionsAction")
        verify(action !== null)
        verify(action.visible)
        action.triggered()
        compare(route.count, 1)
        if (data.live) {
            item.downloadRelease(item.releases[0])
            compare(comics.downloads, 0)
        }
        extensions.setRows([{id: "colosseum.well.getcomics.issues", enabled: true}])
        compare(item.getComicsEnabled, true)
        verify(!action.visible)
        if (data.live) {
            item.downloadRelease(item.releases[0])
            compare(comics.downloads, 1)
        }
        extensions.setRows([])
        verify(action.visible)
        if (data.live) compare(item.releases.length, 1)
        else compare(item.formatGroups.length, 1)
    }
}
