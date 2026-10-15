import QtQuick

Item {
    id: harness
    width: 1280; height: 720
    property var header: null
    property real tankX: 0
    property real tankY: 0

    function fail(msg) { console.error("MANGA_SHARED_HEADER_FAIL: " + msg); Qt.exit(1) }
    function check(ok, msg) { if (!ok) fail(msg) }

    function verifyChapterGeometry() {
        check(header.modeSwitchX === tankX, "mode switch x moved between modes")
        check(header.modeSwitchY === tankY, "mode switch y moved between modes")
        check(header.height === 263, "shared header height changed")
        check(header.libraryX < header.modeSwitchX, "Library must stay spatially separate from mode switch")
        console.log("MANGA_SHARED_HEADER_OK")
        Qt.exit(0)
    }

    Component.onCompleted: {
        var comp = Qt.createComponent("../qml/MangaSeriesSharedHeader.qml")
        if (comp.status === Component.Error) fail("component: " + comp.errorString())
        header = comp.createObject(harness, { width: 1280, seriesTitle: "One Piece", synopsis: "Pirates and promises.", tankobanMode: true })
        if (!header) fail("could not create shared header")
        tankX = header.modeSwitchX; tankY = header.modeSwitchY
        header.tankobanMode = false
        Qt.callLater(verifyChapterGeometry)
    }
}