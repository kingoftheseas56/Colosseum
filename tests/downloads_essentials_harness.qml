import QtQuick
import QtQuick.Window

Window {
    id: window
    visible: false
    width: 1200
    height: 800

    QtObject {
        id: fakeDownloads
        property var totals: ({ items: 3, bytes: 5096, tankoban: 1, biblio: 1, theatre: 1 })
        function activeJobs() {
            return [
                { id: "one", world: "theatre", groupKey: "show",
                  state: "downloading", received: 100, total: 100, ratio: 1,
                  canPlay: false, canRetry: false, canPause: true,
                  canResume: false, canCancel: true, canDismiss: false },
                { id: "two", world: "theatre", groupKey: "show",
                  state: "downloading", received: 0, total: 900, ratio: 0,
                  canPlay: false, canRetry: false, canPause: true,
                  canResume: false, canCancel: true, canDismiss: false },
                { id: "unknown", world: "theatre", groupKey: "show",
                  state: "downloading", received: 500, total: 0, ratio: 0,
                  canPlay: false, canRetry: false, canPause: true,
                  canResume: false, canCancel: true, canDismiss: false },
                { id: "failed-theatre", world: "theatre", state: "failed",
                  error: "origin disconnected",
                  canPlay: false, canRetry: true, canPause: false,
                  canResume: false, canCancel: false, canDismiss: false },
                { id: "failed-manga", world: "tankoban", state: "failed",
                  error: "source refused the request",
                  canPlay: false, canRetry: false, canPause: false,
                  canResume: false, canCancel: false, canDismiss: true }
            ]
        }
        function series(world) {
            if (world === "biblio")
                return [{ key: "author:test author", world: "biblio", title: "Test Author",
                          kind: "book", itemCount: 1, bytes: 4096, updatedAt: 42 }]
            return []
        }
        function items(world, key) {
            if (world === "biblio" && key === "author:test author")
                return [{ id: "cccccccccccccccccccccccccccccccc", world: "biblio", kind: "book",
                          title: "Arc 19 Book", author: "Test Author", subtitle: "EPUB",
                          path: "C:/arc19/persisted.epub", bytes: 4096, addedAt: 42, missing: false }]
            return []
        }
        function remove(world, id) {
            return { success: false, message: "fixture deletion was denied" }
        }
    }

    QtObject {
        id: fakeAudiobooks
        function downloadedAudiobooks() {
            return [{ id: "audio", title: "Audio", bytes: 500, bookPath: "C:/book.epub" }]
        }
        function activeDownloads() {
            return [{ id: "audio-live", title: "Live", author: "Author",
                      state: "downloading", received: 25, total: 100, error: "" }]
        }
    }

    Loader {
        id: pageLoader
        anchors.fill: parent
        source: "../qml/DownloadsPage.qml"
        onLoaded: {
            item.downloadsApi = fakeDownloads
            item.audiobooksApi = fakeAudiobooks
            item.refresh()

            var grouped = item.groupJobs(fakeDownloads.activeJobs().slice(0, 3))
            if (grouped.length !== 1 || Math.abs(grouped[0].ratio - 0.1) > 0.0001)
                Qt.exit(11)
            if (item.liveJobCount !== 4)
                Qt.exit(12)
            if (item.attentionCount !== 2)
                Qt.exit(13)
            if (item.totalsMap.items !== 4 || item.totalsMap.bytes !== 5596
                    || item.totalsMap.biblio !== 1 || item.totalsMap.audiobook !== 1)
                Qt.exit(14)
            if (item.laneSeries.biblio.length !== 1
                    || item.laneSeries.biblio[0].key !== "author:test author")
                Qt.exit(17)
            item.toggleLedger("biblio", "author:test author")
            if (item.ledgerItems.length !== 1
                    || item.ledgerItems[0].path !== "C:/arc19/persisted.epub"
                    || item.ledgerItems[0].title !== "Arc 19 Book"
                    || item.ledgerItems[0].author !== "Test Author"
                    || item.ledgerItems[0].subtitle !== "EPUB")
                Qt.exit(18)

            item.confirmAction("Delete local copy?", "This deletes the file.", "Delete", function() {})
            if (!item.confirmationOpen)
                Qt.exit(15)
            item.closeConfirmation()

            item.finishMutation(fakeDownloads.remove("tankoban", "failed-manga"),
                                "fallback deletion error")
            if (item.mutationMessage !== "fixture deletion was denied")
                Qt.exit(16)

            console.log("DOWNLOADS ESSENTIALS PASS")
            Qt.exit(0)
        }
        onStatusChanged: if (status === Loader.Error) Qt.exit(10)
    }
}
