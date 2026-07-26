import QtQuick
import QtQuick.Window

Window {
    id: window
    visible: false
    width: 1200
    height: 800

    QtObject {
        id: fakeDownloads
        property var totals: ({ items: 2, bytes: 1000, tankoban: 1, biblio: 0, theatre: 1 })
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
        function series(world) { return [] }
        function items(world, key) { return [] }
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
            if (item.totalsMap.items !== 3 || item.totalsMap.bytes !== 1500
                    || item.totalsMap.audiobook !== 1)
                Qt.exit(14)

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
