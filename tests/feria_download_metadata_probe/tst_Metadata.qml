import QtQuick
import QtTest
import QtWebEngine
import "../../qml/feria" as Feria

Item {
    id: scene
    width: 800; height: 600
    QtObject {
        id: profile
        property bool offTheRecord: false
        signal downloadRequested(var request)
    }
    // Boundary double: Qt owns native download objects. Mirror every consumed
    // property and accept/cancel side effect; exercise the actual QML owner.
    QtObject {
        id: request
        property var view: ({accountSessionId: "space-a", accountVisitToken: "visit-a"})
        property string downloadFileName: "fixture.txt"
        property string downloadDirectory: "/fixture"
        property string url: "https://example.com/file"
        property string mimeType: "text/plain"
        property real receivedBytes: 10
        property real totalBytes: 10
        property int state: WebEngineDownloadRequest.DownloadRequested
        property bool isPaused: false
        property bool isFinished: false
        function accept() { state = WebEngineDownloadRequest.DownloadInProgress }
        function cancel() { state = WebEngineDownloadRequest.DownloadCancelled; isFinished = true }
        function pause() { isPaused = true }
        function resume() { isPaused = false }
    }
    QtObject {
        id: metadata
        property var browserDownloadHistory: [{fileName: "ordinary.txt", path: "/fixture/ordinary.txt"}]
        property bool failSave: true
        property int browserGeneration: 1
        property bool switchAccountOnSave: false
        property int attempts: 0
        property int releases: 0
        property var saved: null
        function authorizeBrowserDownload(id, token) { return id === "space-a" && token === "visit-a" ? "grant-a" : "" }
        function cancelBrowserDownload(token) { if (token === "grant-a") releases++; return true }
        function recordBrowserDownload(row) {
            attempts++
            if (switchAccountOnSave) { browserGeneration++; return false }
            if (failSave) return false
            saved = row; return true
        }
    }
    Feria.FeriaBrowserDownloads { id: downloads; profile: profile; accountStore: metadata }
    Feria.FeriaBrowserDownloads { id: privateDownloads; profile: profile; accountStore: metadata; privateBrowsing: true }
    TestCase {
        name: "FeriaDownloadMetadata"
        when: windowShown
        function init() {
            downloads.retire(); privateDownloads.retire()
            request.state = WebEngineDownloadRequest.DownloadRequested
            request.isFinished = false
            request.view = ({accountSessionId: "space-a", accountVisitToken: "visit-a"})
            downloads.accountStore = metadata
            metadata.failSave = true; metadata.attempts = 0; metadata.releases = 0; metadata.saved = null
            metadata.browserGeneration = 1; metadata.switchAccountOnSave = false
        }
        function cleanup() { downloads.retire(); privateDownloads.retire() }
        function test_privateProjectionExcludesOrdinaryHistory() {
            compare(privateDownloads.history.length, 0)
            compare(downloads.history[0].fileName, "ordinary.txt")
        }
        function test_failedCommitKeepsGrantForExplicitRetry() {
            downloads.receive(request)
            var entry = downloads.items[0]
            request.state = WebEngineDownloadRequest.DownloadCompleted
            request.isFinished = true
            compare(entry.recorded, false, "Failed save must remain retryable")
            compare(entry.downloadToken, "grant-a", "Do not cancel a retryable native grant")
            compare(entry.metadataPending, true)
            compare(metadata.releases, 0)
            metadata.failSave = false
            entry.update()
            compare(entry.recorded, true)
            compare(entry.downloadToken, "")
            compare(entry.metadataPending, false)
            compare(metadata.releases, 1)
            compare(metadata.saved.downloadToken, "grant-a")
            var count = metadata.attempts
            entry.update()
            compare(metadata.attempts, count, "A completed commit must not replay")
        }
        function test_retirementReleasesFailedCommitGrant() {
            downloads.receive(request)
            request.state = WebEngineDownloadRequest.DownloadCompleted
            request.isFinished = true
            compare(downloads.items[0].recorded, false)
            downloads.retire()
            compare(metadata.releases, 1)
            compare(downloads.items.length, 0)
        }
        function test_accountSwitchDiscardsPendingRetry() {
            downloads.receive(request)
            request.state = WebEngineDownloadRequest.DownloadCompleted
            var entry = downloads.items[0]
            compare(entry.metadataPending, true)
            var attempts = metadata.attempts
            metadata.browserGeneration++
            metadata.failSave = false
            entry.update()
            compare(metadata.attempts, attempts, "A stale retry must not call the new account's store")
            compare(metadata.saved, null)
            compare(entry.downloadToken, "")
            compare(entry.metadataPending, false)
            compare(entry.recorded, true)
            compare(metadata.releases, 1)
        }
        function test_accountSwitchDuringSaveDiscardsGrant() {
            downloads.receive(request)
            metadata.switchAccountOnSave = true
            request.state = WebEngineDownloadRequest.DownloadCompleted
            var entry = downloads.items[0]
            compare(metadata.attempts, 1)
            compare(entry.downloadToken, "")
            compare(entry.metadataPending, false)
            compare(entry.recorded, true)
            compare(metadata.releases, 1)
        }
        function test_nativeAccountSwitchRevokesCompletedDownloadGrant() {
            Fixture.switchProfile("download-old-account")
            downloads.accountStore = FeriaAccount
            var token = FeriaAccount.beginSessionVisit("space-a", {pk: "youtube"})
            verify(token.length > 0)
            request.view = ({accountSessionId: "space-a", accountVisitToken: token})
            downloads.receive(request)
            var entry = downloads.items[0], generation = FeriaAccount.browserGeneration
            verify(entry.downloadToken.length > 0)
            Fixture.switchProfile("download-new-account")
            verify(FeriaAccount.browserGeneration !== generation)
            request.state = WebEngineDownloadRequest.DownloadCompleted
            compare(FeriaAccount.browserDownloadHistory.length, 0)
            compare(entry.downloadToken, "")
            compare(entry.metadataPending, false)
            compare(entry.recorded, true)
            entry.update()
            compare(FeriaAccount.browserDownloadHistory.length, 0)
        }
    }
}
