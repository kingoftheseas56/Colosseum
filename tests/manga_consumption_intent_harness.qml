// Arc 19 deterministic contract harness for consumption-first Tankoban reading.
// No network, disk, or real downloader is used. The real MangaSeries/ReadingRoom/
// Library/Sources QML is driven through injectable service seams.
import QtQuick

Item {
    id: harness
    width: 1440
    height: 900
    visible: true

    component FakeMalCatalog: QtObject {
        function ready() { return true }
        function matchByTitle(title, year, medium) { return [{ "mal_id": 1 }] }
        function mangaById(id) {
            return {
                "mal_id": 1, "title": "One Piece", "score": 9.2,
                "status": "Publishing", "year": 1997,
                "images": { "jpg": { "large_image_url": "" } },
                "synopsis": "Pirates.", "authors": [{ "name": "Eiichiro Oda" }],
                "genres": [{ "name": "Adventure" }]
            }
        }
        signal readyChanged()
    }
    component FakeTankobanCatalog: QtObject {
        function ready() { return true }
        function seriesInfo(id) { return { "volumeCount": 5, "countBasis": "fixture" } }
    }

    component FakeExtensions: QtObject {
        function installed() { return [{ "id": "colosseum.well.nyaa", "enabled": true }] }
    }

    component FakeVolumesService: QtObject {
        property var rows: []
        property var states: ({})
        property var pages: ({})
        property int cancelCount: 0
        signal volumesChanged(string seriesId)
        signal sourcesReady(string volumeId, var results)
        signal progress(string volumeId, real done, real total)
        signal finished(string volumeId)
        signal failed(string volumeId, string reason)
        signal removed(string volumeId)
        signal synopsisReady(string volumeId)
        function volumesForSeries(seriesId) { return seriesId === "mal:1" ? rows : [] }
        function statusOf(volumeId) {
            var state = states[String(volumeId)]
            return { "state": state !== undefined ? state : "none", "done": 0, "total": 0 }
        }
        function localPages(volumeId) {
            var value = pages[String(volumeId)]
            return value !== undefined ? value : []
        }
        function searchSources(volumeId) {}
        function searchSeriesSources(volumeId, title) {}
        function downloadNyaa(volumeId, infoHash) {}
        function downloadNyaaBatch(volumeIds, infoHash) {}
        function cancel(volumeId) { cancelCount += 1 }
        function setState(volumeId, state, readyPages) {
            var nextStates = {}
            for (var key in states) nextStates[key] = states[key]
            nextStates[String(volumeId)] = String(state)
            states = nextStates
            if (readyPages !== undefined) {
                var nextPages = {}
                for (var p in pages) nextPages[p] = pages[p]
                nextPages[String(volumeId)] = readyPages
                pages = nextPages
            }
        }
    }

    FakeMalCatalog { id: malCatalog }
    FakeTankobanCatalog { id: tankobanCatalog }
    FakeVolumesService { id: service }
    FakeExtensions { id: extensions }

    property var page: null
    property var room: null
    function ck(value, message) {
        if (!value) throw new Error(message)
    }

    function findByName(item, name) {
        if (!item) return null
        if (item.objectName === name) return item
        var children = item.children || []
        for (var i = 0; i < children.length; i++) {
            var found = findByName(children[i], name)
            if (found) return found
        }
        return null
    }

    function resetReader() {
        page.openChapterId = ""
        page.openChapterLabel = ""
        page.openEntryKind = "manga"
    }

    function seed() {
        service.rows = [
            { "id": "v1", "seriesId": "mal:1", "number": "1", "title": "Romance Dawn", "cover": "", "state": "ready" },
            { "id": "v2", "seriesId": "mal:1", "number": "2", "title": "Buggy the Clown", "cover": "", "state": "none" },
            { "id": "v3", "seriesId": "mal:1", "number": "3", "title": "Don't Get Fooled", "cover": "", "state": "none" },
            { "id": "v4", "seriesId": "mal:1", "number": "4", "title": "The Black Cat Pirates", "cover": "", "state": "downloading" },
            { "id": "v5", "seriesId": "mal:1", "number": "5", "title": "For Whom the Bell Tolls", "cover": "", "state": "none" }
        ]
        service.states = ({ "v1": "ready", "v2": "none", "v3": "none", "v4": "downloading", "v5": "none" })
        service.pages = ({ "v1": [{ "url": "file:///v1/001.jpg" }] })
    }
    function run() {
        try {
            seed()
            var comp = Qt.createComponent("../qml/MangaSeries.qml")
            if (comp.status === Component.Error) throw new Error(comp.errorString())
            page = comp.createObject(harness, {
                "width": 1320, "height": 860,
                "malCatalogRef": malCatalog,
                "tankobanCatalogRef": tankobanCatalog,
                "tankobanVolumesRef": service
            })
            if (!page) throw new Error("MangaSeries did not instantiate")
            page.sourcesPage.extensionsRef = extensions
            page.malId = "1"
            page.seriesTitle = "One Piece"
            room = findByName(page, "mangaReadingRoom")
            if (!room) throw new Error("mangaReadingRoom not found")

            Qt.callLater(function() {
                try {
                    ck(room.library.volumeRows.length === 5,
                       "fixture shelf must reach the real ReadingRoom library")

                    // Ready Read: the same user intent reaches MangaReader immediately.
                    page._readVolume("v1")
                    ck(page.openChapterId === "v1", "ready Read must open the exact volume immediately")
                    ck(!page.pendingReadActive, "ready Read must not leave a pending intent")
                    resetReader()
                    // Missing Read: source choice is a dependency, not a change of verb.
                    page._readVolume("v2")
                    var v2Generation = page._readIntentGeneration
                    ck(page.pendingReadVolumeId === "v2", "missing Read must retain exact volume identity")
                    ck(page.sourcesPage.open, "missing Read must open the source picker")
                    ck(page.sourcesPage.context.intent === "consume",
                       "Read-originated source picker must carry consume intent")
                    ck(Number(page.sourcesPage.context.intentGeneration) === v2Generation,
                       "source picker must carry the exact Read generation")

                    // A newer Read makes the old completion stale.
                    page.sourcesPage.hide(true)
                    ck(!page.pendingReadActive, "backing out before a pick must clear auto-open intent")
                    page._readVolume("v3")
                    var v3Generation = page._readIntentGeneration
                    ck(v3Generation !== v2Generation, "a newer Read must advance the generation")
                    service.setState("v2", "ready", [{ "url": "file:///v2/001.jpg" }])
                    ck(page._completePendingRead("v2", v2Generation) === false,
                       "stale completion must not satisfy a newer Read")
                    ck(page.openChapterId === "", "stale completion must not open a reader")

                    // Back after acquisition starts cancels handoff, not the download.
                    service.setState("v3", "downloading")
                    page.sourcesPage.hide(true)
                    ck(!page.pendingReadActive, "backing out mid-acquisition must clear foreground Read")
                    ck(service.statusOf("v3").state === "downloading",
                       "backing out must leave the exact download running")
                    ck(service.cancelCount === 0, "source Back must never cancel native acquisition")
                    // Read on an already-arriving volume adopts the job, no second picker.
                    page._readVolume("v4")
                    var v4Generation = page._readIntentGeneration
                    ck(page.pendingReadVolumeId === "v4", "in-flight Read must retain exact identity")
                    ck(!page.sourcesPage.open, "in-flight Read must not reopen Sources")
                    ck(page._pendingReadViaSources === false,
                       "in-flight Read must be marked as adopted, not source-owned")
                    ck(room.library.pendingReadVolumeId === "v4",
                       "ReadingRoom action bar must mirror the adopted Read intent")

                    service.setState("v4", "ready", [{ "url": "file:///v4/001.jpg" }])
                    service.finished("v4")
                    ck(page.openChapterId === "v4", "adopted download completion must open MangaReader")
                    ck(!page.pendingReadActive, "successful adopted Read must clear pending state")

                    // Duplicate terminal signals cannot open the reader twice.
                    resetReader()
                    service.finished("v4")
                    ck(page.openChapterId === "", "duplicate finish after intent consumption must be ignored")

                    // Explicit Download stays acquire-only and never creates a pending Read.
                    room.library.focusAtNumber("5")
                    room.library.downloadAction(room.library.currentRow)
                    ck(page.sourcesPage.open, "Download must still reach the source picker")
                    ck(page.sourcesPage.context.intent === "acquire",
                       "explicit Download must carry acquire intent")
                    ck(!page.pendingReadActive, "explicit Download must never arm reader auto-open")
                    page.sourcesPage.hide(false)
                    // Source-owned consume completion opens only after exact readiness.
                    service.setState("v2", "none", [])
                    page._readVolume("v2")
                    var sourceGeneration = page._readIntentGeneration
                    service.setState("v2", "ready", [{ "url": "file:///v2/001.jpg" }])
                    page.sourcesPage.hide(false)
                    page.sourcesPage.consumeReady("v2", sourceGeneration)
                    ck(page.openChapterId === "v2",
                       "consume-ready signal must open the exact source-acquired volume")
                    ck(!page.pendingReadActive, "source-acquired Read must consume its intent exactly once")
                    resetReader()

                    // Changing series identity invalidates any pending auto-open generation.
                    service.setState("v3", "downloading")
                    page._readVolume("v3")
                    ck(page.pendingReadActive, "fixture must arm one pending Read before identity change")
                    var beforeChange = page._readIntentGeneration
                    page.seriesId = "mal:2"
                    ck(!page.pendingReadActive, "series identity change must cancel foreground auto-open")
                    ck(page._readIntentGeneration > beforeChange,
                       "series identity change must invalidate the old generation")

                    console.log("MANGA_CONSUMPTION_INTENT_OK")
                    Qt.exit(0)
                } catch (inner) {
                    console.log("MANGA_CONSUMPTION_INTENT_FAIL: " + inner.message)
                    Qt.exit(1)
                }
            })
        } catch (e) {
            console.log("MANGA_CONSUMPTION_INTENT_FAIL: " + e.message)
            Qt.exit(1)
        }
    }
    Timer {
        interval: 100
        running: true
        repeat: false
        onTriggered: harness.run()
    }

    Timer {
        interval: 8000
        running: true
        repeat: false
        onTriggered: {
            console.log("MANGA_CONSUMPTION_INTENT_FAIL: timeout")
            Qt.exit(1)
        }
    }
}
