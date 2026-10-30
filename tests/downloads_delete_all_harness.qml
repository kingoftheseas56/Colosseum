import QtQuick

Window {
    id: window
    width: 1200
    height: 800
    visible: false
    property var calls: []
    property int phase: 0

    function check(ok, message) {
        if (!ok) {
            console.error(message)
            Qt.exit(1)
            throw new Error(message)
        }
    }
    function record(value) { calls = calls.concat([value]) }

    QtObject {
        id: downloads
        property var totals: ({ items: 4 })
        property var jobs: [
            { world: "theatre", id: "queued", state: "queued" },
            { world: "tankoban", id: "old-failure", state: "failed", canDismiss: true }
        ]
        property var files: [
            { world: "tankoban", id: "manga" },
            { world: "tankoban", id: "comic" },
            { world: "biblio", id: "book" },
            { world: "theatre", id: "video" }
        ]
        property bool denyBook: false
        function activeJobs() { return jobs.slice() }
        function series(world) { return [{ key: world }] }
        function items(world, key) { return files.filter(function(f) { return f.world === world }) }
        function cancel(world, id) {
            window.record("cancel:" + id)
            if (id === "cancel-denied") {
                jobs = [{ world: world, id: id, state: "failed", error: "partial folder is locked", canDismiss: true }]
            } else if (id === "still-stopping") {
                return
            } else if (id === "late-job") {
                // Cancellation drains asynchronously; a completion can also land meanwhile.
                cancellationDrain.start()
            } else {
                jobs = jobs.filter(function(j) { return j.id !== id })
            }
        }
        function dismissFailure(world, id) {
            window.record("dismiss:" + id)
            jobs = jobs.filter(function(j) { return j.id !== id })
        }
        function removeIncompleteDownloads() {
            window.record("purge:partials")
            return { success: true }
        }
        function remove(world, id) {
            window.record("delete:" + id)
            window.check(jobs.every(function(j) { return j.state === "failed" }),
                         "files deleted before cancellation drained")
            if (id === "book" && denyBook)
                return { success: false, message: "book is locked" }
            files = files.filter(function(f) { return f.id !== id })
            totals = { items: files.length }
            return { success: true }
        }
    }
    QtObject {
        id: audio
        property var jobs: [{ id: "audio-live", state: "downloading" },
                            { id: "audio-failed", state: "failed" }]
        property var files: [{ id: "audio-done", title: "Audiobook" }]
        function activeDownloads() { return jobs.slice() }
        function downloadedAudiobooks() { return files.slice() }
        function cancelDownload(id) {
            window.record("cancel:" + id)
            jobs = jobs.filter(function(j) { return j.id !== id })
        }
        function dismissFailure(id) {
            window.record("dismiss:" + id)
            jobs = jobs.filter(function(j) { return j.id !== id })
        }
        function deleteAudiobook(id) {
            window.record("delete:" + id)
            files = files.filter(function(f) { return f.id !== id })
            return { success: true }
        }
        function removeIncompleteDownloads() {
            window.record("purge:audio-partials")
            return { success: true }
        }
    }
    QtObject {
        id: vault
        property var rows: ["download", "manual"]
        function downloadsRootPath() { return "fixture-downloads" }
        function rescanRoot(path) {
            window.check(path === "fixture-downloads", "unrelated Vault root was changed")
            rows = ["manual"]
            window.record("refresh:vault")
        }
    }
    Timer {
        id: cancellationDrain
        interval: 100
        onTriggered: {
            downloads.jobs = []
            downloads.files = downloads.files.concat([{ world: "theatre", id: "just-landed" }])
        }
    }
    Loader {
        id: pageLoader
        anchors.fill: parent
        source: "../qml/DownloadsPage.qml"
        onStatusChanged: if (status === Loader.Error) Qt.exit(2)
        onLoaded: {
            item.downloadsApi = downloads
            item.audiobooksApi = audio
            item.vaultApi = vault
            item.refresh()
            item.requestDeleteAllDownloads()
            window.check(item.confirmationOpen, "bulk deletion must ask for confirmation")
            window.check(window.calls.length === 0, "opening confirmation deleted downloads")
            item.closeConfirmation()
            window.check(window.calls.length === 0, "dismissing confirmation deleted downloads")
            item.requestDeleteAllDownloads()
            downloads.jobs = downloads.jobs.concat([{ world: "tankoban", id: "late-job", state: "downloading" }])
            item.runConfirmedAction()
            completionCheck.start()
        }
    }
    Timer {
        id: completionCheck
        interval: 25
        repeat: true
        onTriggered: {
            var page = pageLoader.item
            if (page.deletingAllDownloads) return
            if (window.phase === 0) {
                window.check(downloads.files.length === 0 && audio.files.length === 0,
                             "bulk deletion left completed downloads behind")
                window.check(downloads.jobs.length === 0 && audio.jobs.length === 0,
                             "bulk deletion left jobs behind")
                window.check(window.calls.indexOf("cancel:late-job") >= 0
                             && window.calls.indexOf("delete:just-landed") >= 0,
                             "bulk deletion used a stale snapshot")
                window.check(page.mutationMessage === "", "successful deletion reported failure")
                window.check(vault.rows.length === 1 && vault.rows[0] === "manual",
                             "download-backed Vault rows remain or manual entries were removed")
                window.check(window.calls.indexOf("purge:partials") >= 0
                             && window.calls.indexOf("purge:audio-partials") >= 0,
                             "retained partial downloads were not purged")
                downloads.files = [{ world: "biblio", id: "book" }, { world: "theatre", id: "video" }]
                downloads.denyBook = true
                window.phase = 1
                page.requestDeleteAllDownloads()
                page.runConfirmedAction()
            } else if (window.phase === 1) {
                window.check(downloads.files.length === 1 && downloads.files[0].id === "book",
                             "failure must preserve the failed entry and continue deleting others")
                window.check(page.mutationMessage.indexOf("book is locked") >= 0,
                             "deletion failure was hidden")
                downloads.denyBook = false
                downloads.jobs = [{ world: "tankoban", id: "cancel-denied", state: "downloading" }]
                window.phase = 2
                page.requestDeleteAllDownloads()
                page.runConfirmedAction()
            } else if (window.phase === 2) {
                window.check(downloads.files.length === 0, "cancellation failure blocked unrelated file deletion")
                window.check(downloads.jobs.length === 1
                             && page.mutationMessage.indexOf("partial folder is locked") >= 0,
                             "new cancellation failure was dismissed or hidden")
                downloads.jobs = [{ world: "theatre", id: "still-stopping", state: "downloading" }]
                window.phase = 3
                page.requestDeleteAllDownloads()
                page.runConfirmedAction()
                page.downloadCancellationDeadline = 0
            } else {
                window.check(downloads.jobs.length === 1 && page.mutationMessage.length > 0,
                             "cancellation timeout must restore controls and report remaining jobs")
                console.log("DOWNLOADS DELETE ALL PASS")
                Qt.exit(0)
            }
        }
    }
    Timer { interval: 10000; running: true; onTriggered: { console.error("bulk deletion timed out"); Qt.exit(3) } }
}
