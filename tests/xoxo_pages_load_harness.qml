// Load-gate for the xoxo QML pages (lazy-Loader law: qmllint + the boot smoke are blind
// to creation-time QML errors in lazily-loaded pages — the DownloadsPage 12.5px lesson).
// Each page is loaded via a Loader; the verdict rides the EXIT CODE after a settle Timer.
// Grows per task: Task 4 gates XoxoSeries; Task 5 adds XoxoGenrePage + ComicArchiveBoard.
import QtQuick

Item {
    id: gate
    Loader { id: l1; source: "../qml/XoxoSeries.qml"; asynchronous: false }

    Timer {
        interval: 2000; running: true; repeat: false
        onTriggered: {
            // Qt.exit() does NOT halt synchronous JS — must return, or we fall through
            // to Qt.exit(1) and the last call wins (Task 1 lesson).
            if (l1.status === Loader.Ready) { console.log("XOXO LOAD PASS"); Qt.exit(0); return }
            console.log("XOXO LOAD FAIL: XoxoSeries status " + l1.status)
            Qt.exit(1)
        }
    }
}
