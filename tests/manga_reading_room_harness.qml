// RED/GREEN contract harness for the locked Tankoban Reading Room.
// It deliberately uses only fake service seams: no network, no live app state.
import QtQuick

Item {
    id: harness
    width: 1440
    height: 820
    visible: false

    component FakeService: QtObject {
        property var volMap: ({})
        signal volumesChanged(string seriesId)
        signal progress(string volumeId, real done, real total)
        signal finished(string volumeId)
        signal failed(string volumeId, string reason)
        signal removed(string volumeId)
        signal synopsisReady(string volumeId)
        function volumesForSeries(sid) { return volMap[sid] !== undefined ? volMap[sid] : [] }
        function statusOf(vid) { return { "state": "none", "done": 0, "total": 0 } }
        function localPages(vid) { return [] }
        function cancel(vid) {}
    }

    component FakeProgress: QtObject {
        property var record: null
        function get(kind, sid) { return kind === "tankoban" ? record : null }
    }

    component FakeDownloads: QtObject {
        property var asked: []
        signal thumbReady(string chapterId, string url)
        signal progress(string chapterId, real done, real total)
        signal finished(string chapterId)
        signal failed(string chapterId, string reason)
        signal removed(string chapterId)
        function fetchThumb(seriesId, chapterId) {
            var next = asked.slice()
            next.push({ "seriesId": String(seriesId), "chapterId": String(chapterId) })
            asked = next
        }
        function statusOf(cid) { return { "state": "none", "done": 0, "total": 0 } }
        function downloadChapter(cid, sid, title, label) {}
    }

    FakeService { id: service }
    FakeProgress { id: progress }
    FakeDownloads { id: downloads }

    function volumes(count) {
        var out = []
        for (var i = 1; i <= count; i++) {
            var state = i === 2 ? "ready"
                       : i === 3 ? "resolving"
                       : i === 4 ? "ingesting"
                       : i === 5 ? "packing"
                       : i === 6 ? "downloading"
                       : i === 7 ? "failed"
                       : "none"
            out.push({
                "id": "v" + i, "seriesId": "S", "number": String(i),
                "title": "Volume " + i, "cover": "",
                "chapterStart": String((i - 1) * 10 + 1),
                "chapterEnd": String(i * 10), "state": state,
                "synopsis": "Volume synopsis " + i
            })
        }
        return out
    }

    function chapters(count) {
        var out = []
        for (var i = 1; i <= count; i++)
            out.push({ "id": "c" + i, "number": i, "name": "Chapter " + i })
        return out
    }

    property var room: null
    property var chapterOnlyRoom: null
    property var lastBatch: null

    function ck(condition, message) {
        if (!condition) throw new Error(message)
    }

    function runChecks() {
        try {
            service.volMap = ({ "S": volumes(115), "C": [] })
            progress.record = { "chapterId": "v74", "page": 93, "max": 210 }

            var roomComp = Qt.createComponent("../qml/MangaReadingRoom.qml")
            if (roomComp.status === Component.Error)
                throw new Error("room component: " + roomComp.errorString())

            room = roomComp.createObject(harness, {
                "width": 1320, "height": 720, "seriesId": "S",
                "seriesTitle": "One Piece", "author": "Eiichiro Oda",
                "status": "Ongoing", "year": 1997, "score": 9.2,
                "genres": ["Action", "Adventure", "Fantasy"],
                "synopsis": "A long synopsis used to prove the rail's three-line floor.",
                "chapters": chapters(1160), "service": service,
                "progress": progress, "downloader": downloads
            })
            if (!room) throw new Error("room createObject returned null")
            room.library.batchRequested.connect(function(numbers, label) {
                harness.lastBatch = { "numbers": numbers, "label": label }
            })

            var lib = room.library
            ck(room.contentHeight <= room.height,
               "the Reading Room root must not grow beyond the viewport")
            var rejectedBrokenHeight = false
            try { ck(room.height + 1 <= room.height, "negative fixed-height control") }
            catch (negativeHeight) { rejectedBrokenHeight = true }
            ck(rejectedBrokenHeight,
               "the fixed-height assertion must fail when the contract is inverted")
            ck(lib.renderedCount < 115,
               "the grid must virtualize the long series, rendered " + lib.renderedCount)
            ck(downloads.asked.length < 115,
               "cover fetches must stay inside the viewport, asked " + downloads.asked.length)
            ck(lib.autoLandNumber === 74,
               "the grid must auto-land on the continue volume")

            ck(lib.stateWordFor(lib.volumeRows[1]) === "On this device",
               "ready volume state must be drawn as On this device")
            ck(lib.stateWordFor(lib.volumeRows[2]) === "Finding source…",
               "resolving state must use the canon word")
            ck(lib.effectiveState(lib.volumeRows[3]) === "ingesting",
               "ingesting state must be present in the tile contract")
            ck(lib.effectiveState(lib.volumeRows[4]) === "packing",
               "packing state must be present in the tile contract")
            ck(lib.effectiveState(lib.volumeRows[5]) === "downloading",
               "downloading state must be present in the tile contract")
            ck(lib.stateWordFor(lib.volumeRows[3]).indexOf("Adding") === 0,
               "ingesting state must use the canon word")
            ck(lib.stateWordFor(lib.volumeRows[4]).indexOf("Building") === 0,
               "packing state must use the canon word")
            ck(lib.stateWordFor(lib.volumeRows[5]).indexOf("Downloading") === 0,
               "downloading state must use the canon word")
            ck(lib.stateWordFor(lib.volumeRows[6]) === "Couldn't finish",
               "failed state must use the canon word")

            lib.activeTab = "chapters"
            ck(lib.activeTab === "chapters", "the pane must switch to Chapters")
            ck(lib.chapterRows.length === 10,
               "qualified series chapters tab must carry the loose tail only")

            lib.selecting = true
            lib.selectNumber(20)
            lib.selectNumber(35)
            lib.downloadSelected()
            ck(lastBatch !== null, "select mode must emit one batch")
            ck(lastBatch.numbers.length === 2 && lastBatch.numbers[0] === 20
               && lastBatch.numbers[1] === 35,
               "selected batch must contain exactly the selected volume numbers")

            var chapterOnlyComp = Qt.createComponent("../qml/MangaReadingRoom.qml")
            chapterOnlyRoom = chapterOnlyComp.createObject(harness, {
                "width": 1000, "height": 720, "seriesId": "C",
                "seriesTitle": "Chapter Only", "chapters": chapters(42),
                "service": service, "progress": progress, "downloader": downloads
            })
            ck(chapterOnlyRoom.library.showVolumes === false,
               "chapter-only series must not expose an empty Volumes tab")
            ck(chapterOnlyRoom.library.activeTab === "chapters",
               "chapter-only series must open on Chapters")
            ck(chapterOnlyRoom.library.chapterRows.length === 42,
               "chapter-only series must show its full chapter run")

            console.log("MANGA_READING_ROOM_OK")
            Qt.exit(0)
        } catch (e) {
            console.log("MANGA_READING_ROOM_FAIL: " + e.message)
            Qt.exit(1)
        }
    }

    Component.onCompleted: Qt.callLater(runChecks)
    Timer { interval: 8000; running: true
        onTriggered: { console.log("MANGA_READING_ROOM_FAIL timeout"); Qt.exit(1) } }
}
