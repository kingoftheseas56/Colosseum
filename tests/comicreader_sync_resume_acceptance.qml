// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

import QtQuick

Item {
    id: harness
    width: 320
    height: 240
    visible: false

    property var failures: []
    property var received: []

    function ck(condition, message) {
        if (!condition)
            failures.push(message)
    }

    component FakeProgress: QtObject {
        property var saved: ({})
        signal syncedEntryApplied(string kind, string id)

        function get(kind, id) {
            if (String(kind) !== String(saved.kind)
                    || String(id) !== String(saved.id))
                return ({})
            return saved
        }
    }

    Component {
        id: progressComponent
        FakeProgress {}
    }

    property var bridgeComponent: null

    function record(seriesId, chapterId, page, fraction, updatedAt) {
        return {
            id: seriesId,
            kind: "tankoban",
            updatedAt: updatedAt,
            progress: 0.5,
            resume: {
                chapterId: chapterId,
                page: page,
                pageFraction: fraction,
                scrollFrac: 0.8
            }
        }
    }

    function runChecks() {
        bridgeComponent = Qt.createComponent(
            "../qml/comicreader/ComicReaderSyncedResumeBridge.qml")
        if (bridgeComponent.status !== Component.Ready) {
            failures.push(
                "bridge component failed: "
                + bridgeComponent.errorString())
            report()
            return
        }

        var progress = progressComponent.createObject(harness)
        var bridge = bridgeComponent.createObject(
            harness,
            {
                progress: progress,
                seriesId: "series-1"
            })
        ck(bridge !== null, "bridge create failed")
        if (!bridge) {
            report()
            return
        }

        bridge.resumeRequested.connect(function (target) {
            received.push(target)
        })
        bridge.rejected.connect(function (code) {
            received.push({ rejected: code })
        })

        // Local store writes cannot trigger this bridge because the bridge
        // listens only to the remote-only syncedEntryApplied owner signal.
        progress.saved = record(
            "series-1", "vol-1", 2, 0.25, 1000)
        ck(received.length === 0,
           "record assignment alone must not request restore")

        progress.syncedEntryApplied("manga", "series-1")
        ck(received.length === 0,
           "non-Tankoban import must be ignored")

        progress.syncedEntryApplied("tankoban", "series-2")
        ck(received.length === 0,
           "another series import must be ignored")

        progress.syncedEntryApplied("tankoban", "series-1")
        ck(received.length === 1,
           "matching remote import must request one restore")
        ck(received[0].chapterId === "vol-1",
           "chapter identity mismatch")
        ck(received[0].page === 2,
           "page mismatch")
        ck(Math.abs(received[0].pageFraction - 0.25) < 0.00001,
           "pageFraction mismatch")
        ck(Math.abs(received[0].legacyScrollFrac - 0.8) < 0.00001,
           "legacy scrollFrac compatibility mismatch")

        // Idempotent notification of the same semantic winner is coalesced.
        progress.syncedEntryApplied("tankoban", "series-1")
        ck(received.length === 1,
           "same imported winner must not request restore twice")

        // A newer record that changes only metadata/timestamp but keeps the
        // same semantic resume location also must not reposition the reader.
        progress.saved = record(
            "series-1", "vol-1", 2, 0.25, 1500)
        progress.syncedEntryApplied("tankoban", "series-1")
        ck(received.length === 1,
           "same semantic position with newer metadata must not re-jump")

        progress.saved = record(
            "series-1", "vol-2", 7, 0.42, 2000)
        progress.syncedEntryApplied("tankoban", "series-1")
        ck(received.length === 2,
           "new imported winner must request restore")
        ck(received[1].chapterId === "vol-2"
           && received[1].page === 7,
           "new imported position mismatch")

        bridge.acceptPending(received[1])
        ck(bridge.pendingTarget === null,
           "accepted restore must clear pending target")

        // Path-derived logical identity is rejected before the shell sees it.
        progress.saved = record(
            "series-1",
            "C:\\Private\\volume.cbz",
            1,
            0,
            3000)
        progress.syncedEntryApplied("tankoban", "series-1")
        ck(received.length === 3,
           "path-derived chapter must produce one rejection event")
        ck(received[2].rejected === "filesystem_identity",
           "path-derived chapter rejection code mismatch")

        // Invalid within-page anchor is also rejected rather than clamped into
        // an invented reader position.
        progress.saved = record(
            "series-1", "vol-3", 4, 1.5, 4000)
        progress.syncedEntryApplied("tankoban", "series-1")
        ck(received.length === 4,
           "invalid pageFraction must produce one rejection")
        ck(received[3].rejected === "invalid_page_fraction",
           "invalid pageFraction rejection code mismatch")

        bridge.destroy()
        progress.destroy()
        report()
    }

    function report() {
        if (failures.length === 0) {
            console.log("COMICREADER_SYNC_RESUME_OK")
            Qt.exit(0)
            return
        }

        for (var i = 0; i < failures.length; ++i)
            console.log("COMICREADER_SYNC_RESUME_FAIL: " + failures[i])
        Qt.exit(1)
    }

    Component.onCompleted: Qt.callLater(runChecks)

    Timer {
        interval: 10000
        running: true
        onTriggered: {
            console.log("COMICREADER_SYNC_RESUME_FAIL: timeout")
            Qt.exit(1)
        }
    }
}
