// Copyright (C) 2019 The Qt Company Ltd.
// SPDX-License-Identifier: BSD-3-Clause
// Qt WebEngine download APIs follow Qt Quick Nano Browser; see NanoBrowser-LICENSE.txt.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtWebEngine
import ".."
import "../account" as Account

Item {
    id: root
    // Headless inspection of the existing Downloads panel, without changing its behaviour.
    objectName: privateBrowsing ? "feriaPrivateDownloads" : "feriaRegularDownloads"
    // One instance per shared profile, owned by the session deck, never by a tab.
    required property var profile
    property var accountStore: null
    property bool privateBrowsing: false
    // Native lifecycle owner, supplied by the session deck. Each download of a
    // registered profile holds a work ticket against that profile, so the
    // profile is not destroyed under it and a late completion cannot record
    // into another account. Component doubles stay unmanaged (-1).
    property var runtime: null
    property bool active: profile !== null
    property bool panelOpen: false
    property var items: []
    property var opening: null
    property var openingStore: null
    readonly property var history: !privateBrowsing && accountStore ? accountStore.browserDownloadHistory || [] : []
    signal message(string text)
    Theme { id: theme }

    function openPanel() { panelOpen = true }
    function syncOpeningAuthority() {
        if (accountStore && runtime && typeof accountStore.watchBrowserDownloads === "function")
            accountStore.watchBrowserDownloads(profile, runtime)
    }
    function openingMessage(reason) {
        if (reason === "canceled" || reason === "os-canceled") return qsTr("Opening canceled.")
        if (reason === "os-blocked") return qsTr("Windows prevented opening this download. Check Windows Security or open its folder to inspect it.")
        if (reason === "os-unavailable") return qsTr("Windows attachment checks are unavailable. The file was not opened.")
        if (reason === "file-changed" || reason === "provenance-changed") return qsTr("The file changed while you were confirming. Select Open again to inspect the current file.")
        return qsTr("This download could not be verified or is no longer available. Open its folder to inspect it, or download it again.")
    }
    function finishOpening(confirmed) {
        const selected = opening, owner = openingStore
        opening = null; openingStore = null
        openPrompt.close()
        if (!selected || !owner) return
        const result = owner.finishBrowserDownloadOpen(selected.ticket, confirmed)
        if (!result.ok && result.reason !== "canceled") message(openingMessage(result.reason))
    }
    function openDownload(id, current) {
        if (opening) finishOpening(false)
        if (!accountStore || typeof accountStore.prepareBrowserDownloadOpen !== "function") {
            message(openingMessage("unowned-download")); return
        }
        const selected = accountStore.prepareBrowserDownloadOpen(id, accountStore.browserGeneration, !!current)
        if (!selected.ok) { message(openingMessage(selected.reason)); return }
        opening = selected; openingStore = accountStore
        if (selected.requiresConfirmation) openPrompt.open()
        else finishOpening(true)
    }
    function cancelActive() { retire() }
    function fileUrl(path) {
        var normalized = String(path || "").replace(/\\/g, "/")
        var encoded = normalized.split("/").map(function(part) { return encodeURIComponent(part) }).join("/")
        encoded = encoded.replace(/^([a-z])%3A\//i, "$1:/")
        return normalized.indexOf("//") === 0 ? "file:" + encoded
             : normalized[0] === "/" ? "file://" + encoded : "file:///" + encoded
    }
    function receive(request) {
        if (!active) { request.cancel(); return }
        // Qt deletes unaccepted requests at the end of this signal. The default
        // Downloads path/name are resolved by Qt, including existing-name collisions.
        // Do not defer acceptance to a QML dialog or retain rejected requests.
        var view = request.view
        var privateDownload = privateBrowsing || !!profile.offTheRecord
        var sessionId = view ? String(view.accountSessionId || "") : ""
        var visitToken = view ? String(view.accountVisitToken || "") : ""
        var work = runtime && runtime.hasProfile(profile) ? runtime.admitProfileWork(profile, "download") : -1
        if (work === 0) { request.cancel(); return } // the profile is leaving
        var token = !privateDownload && sessionId.length && visitToken.length && accountStore
            && typeof accountStore.authorizeBrowserDownload === "function"
            ? accountStore.authorizeBrowserDownload(sessionId, visitToken) : ""
        var entry = downloadEntry.createObject(root, {
            request: request, privateDownload: privateDownload,
            metadataStore: accountStore, owningProfile: profile, downloadToken: token,
            accountGeneration: accountStore ? accountStore.browserGeneration : 0,
            sessionId: sessionId, visitToken: visitToken, work: work
        })
        if (!entry) {
            if (work > 0) runtime.releaseWork(work)
            if (token.length && typeof accountStore.cancelBrowserDownload === "function") accountStore.cancelBrowserDownload(token)
            request.cancel(); message(qsTr("Could not start this download.")); return
        }
        items = items.concat([entry])
        request.accept()
        Qt.callLater(function() { if (root.items.indexOf(entry) >= 0) entry.update() })
        panelOpen = true
    }
    function retire() {
        if (opening) finishOpening(false)
        var previous = items
        items = []
        panelOpen = false
        for (var i = 0; i < previous.length; ++i) {
            var entry = previous[i]
            if (entry.request && !entry.request.isFinished) entry.request.cancel()
            entry.releaseAuthorization()
            entry.releaseWork()
            if (entry.openingAuthority && entry.metadataStore && typeof entry.metadataStore.forgetBrowserDownloadAuthority === "function")
                entry.metadataStore.forgetBrowserDownloadAuthority(entry.openingAuthority)
            entry.request = null
            entry.destroy()
        }
    }
    onProfileChanged: { retire(); syncOpeningAuthority() }
    onAccountStoreChanged: { retire(); syncOpeningAuthority() }
    onRuntimeChanged: syncOpeningAuthority()
    Component.onCompleted: syncOpeningAuthority()
    onActiveChanged: if (!active) retire()
    Component.onDestruction: retire()
    Connections {
        target: root.runtime; ignoreUnknownSignals: true
        function onProfileCountChanged() { root.syncOpeningAuthority() }
    }
    Connections {
        target: root.accountStore; ignoreUnknownSignals: true
        function onProfileChanged() { if (root.opening) root.finishOpening(false); root.syncOpeningAuthority() }
    }
    Connections {
        target: root.profile
        ignoreUnknownSignals: true
        function onDownloadRequested(request) { root.receive(request) }
    }
    Component {
        id: downloadEntry
        QtObject {
            id: entry
            required property var request
            property var metadataStore: null
            property var owningProfile: null
            property string downloadToken: ""
            property int accountGeneration: 0
            property bool privateDownload: false
            property string sessionId: ""
            property string visitToken: ""
            property real work: -1
            function workLive() { return work < 0 || (root.runtime && root.runtime.workValid(work)) }
            function metadataLive() {
                return workLive() && root.active && owningProfile === root.profile
                    && metadataStore && metadataStore === root.accountStore
                    && accountGeneration === metadataStore.browserGeneration
            }
            function releaseWork() {
                if (work > 0 && root.runtime) root.runtime.releaseWork(work)
                if (work > 0) work = 0
            }
            property bool recorded: false
            property bool metadataPending: false
            property bool notified: false
            property string openingAuthority: ""
            property string name: request ? request.downloadFileName : ""
            property string directory: request ? request.downloadDirectory : ""
            readonly property string path: directory.replace(/[\\/]$/, "") + "/" + name
            property string sourceUrl: request ? String(request.url) : ""
            property real received: request ? request.receivedBytes : 0
            property real total: request ? request.totalBytes : -1
            property int state: request ? request.state : WebEngineDownloadRequest.DownloadCancelled
            property bool paused: request ? request.isPaused : false
            property bool finished: request ? request.isFinished : true
            readonly property bool completed: state === WebEngineDownloadRequest.DownloadCompleted
            readonly property string status: completed ? qsTr("Completed")
                : state === WebEngineDownloadRequest.DownloadCancelled ? qsTr("Canceled")
                : state === WebEngineDownloadRequest.DownloadInterrupted ? qsTr("Interrupted")
                : paused ? qsTr("Paused") : qsTr("Downloading")
            function update() {
                if (!request) return
                if (request.state === WebEngineDownloadRequest.DownloadCancelled) { releaseAuthorization(); releaseWork() }
                if (request.state === WebEngineDownloadRequest.DownloadCompleted && !recorded) {
                    if (!metadataLive()) {
                        recorded = true; metadataPending = false
                        releaseAuthorization(); releaseWork(); return
                    }
                    const nativeOpening = metadataStore && typeof metadataStore.browserDownloadAuthority === "function"
                    if (nativeOpening) openingAuthority = metadataStore.browserDownloadAuthority(request)
                    if (nativeOpening && !openingAuthority) { metadataPending = true; return }
                    if (!notified) { notified = true; root.message(qsTr("Downloaded %1.").arg(name)) }
                    if (!privateDownload && downloadToken.length && metadataLive()
                            && typeof metadataStore.recordBrowserDownload === "function") {
                        var descriptor = {url: sourceUrl, path: path, fileName: name,
                            mimeType: request.mimeType, receivedBytes: request.receivedBytes,
                            totalBytes: Math.max(0, request.totalBytes), state: "completed",
                            sessionId: sessionId, visitToken: visitToken, downloadToken: downloadToken}
                        if (nativeOpening) descriptor.openingAuthority = openingAuthority
                        recorded = metadataStore.recordBrowserDownload(descriptor) === true
                        // An account switch revokes the grant, including a
                        // switch during the store's synchronous save callback.
                        metadataPending = !recorded && metadataLive()
                        if (metadataPending) {
                            root.message(qsTr("The file downloaded, but its history could not be saved. Retry saving history."))
                            return // Native retains failed grants until retry or retirement.
                        }
                    }
                    recorded = true // Private/unowned/stale downloads do not enter history.
                    metadataPending = false
                    releaseAuthorization()
                    releaseWork()
                }
            }
            function releaseAuthorization() {
                if (downloadToken.length && metadataStore && typeof metadataStore.cancelBrowserDownload === "function")
                    metadataStore.cancelBrowserDownload(downloadToken)
                downloadToken = ""
            }
            function togglePause() {
                if (!request || completed || state === WebEngineDownloadRequest.DownloadCancelled) return
                if (paused || state === WebEngineDownloadRequest.DownloadInterrupted) request.resume()
                else request.pause()
            }
            function cancel() { if (request && !completed) request.cancel() }
            function engineChanged() {
                if (root.runtime && root.runtime.hasProfile(owningProfile)
                        && metadataStore && typeof metadataStore.browserDownloadAuthority === "function")
                    Qt.callLater(function() { if (root.items.indexOf(entry) >= 0) entry.update() })
                else update()
            }
            property var observer: Connections {
                target: entry.request
                ignoreUnknownSignals: true
                // Native completion capture shares these engine signals. Wait
                // until all direct handlers finish before consuming its row.
                function onStateChanged() { entry.engineChanged() }
                function onIsFinishedChanged() { entry.engineChanged() }
            }
        }
    }
    Dialog {
        id: openPrompt
        objectName: "feriaDownloadOpenConfirmation"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(480, parent ? parent.width - 32 : 480)
        modal: true
        title: qsTr("Open downloaded file?")
        closePolicy: Popup.CloseOnEscape
        onRejected: root.finishOpening(false)
        contentItem: ColumnLayout {
            Text { Layout.fillWidth: true; textFormat: Text.PlainText; wrapMode: Text.Wrap; text: root.opening ? root.opening.fileName : ""; color: theme.ink }
            Text { Layout.fillWidth: true; textFormat: Text.PlainText; wrapMode: Text.Wrap; text: root.opening ? root.opening.warning : ""; color: theme.inkDim }
            Text { Layout.fillWidth: true; textFormat: Text.PlainText; wrapMode: Text.Wrap; text: root.opening && root.opening.source ? qsTr("From %1").arg(FeriaBrowserPolicy.displayOrigin(root.opening.source)) : qsTr("Source website unknown"); color: theme.inkDim }
            RowLayout {
                Account.AccountButton { Layout.fillWidth: true; text: qsTr("Cancel"); onClicked: root.finishOpening(false) }
                Account.AccountButton { Layout.fillWidth: true; text: qsTr("Open"); onClicked: root.finishOpening(true) }
            }
        }
    }
    Dialog {
        id: panel
        objectName: "feriaBrowserDownloadsPanel"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(650, parent ? parent.width - 32 : 650)
        height: Math.min(560, parent ? parent.height - 32 : 560)
        visible: root.panelOpen
        modal: false
        title: qsTr("Downloads")
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        onClosed: root.panelOpen = false
        background: Rectangle { color: "#202027"; radius: 16; border.color: theme.edge }
        contentItem: ColumnLayout {
            Text {
                visible: root.items.length === 0 && root.history.length === 0
                text: qsTr("Your downloads will appear here.")
                color: theme.inkDim; font.family: theme.ui
            }
            ListView {
                Layout.fillWidth: true; Layout.fillHeight: true
                clip: true; spacing: 16
                model: root.items
                delegate: ColumnLayout {
                    required property var modelData
                    width: ListView.view.width
                    Text { Layout.fillWidth: true; text: modelData.name; textFormat: Text.PlainText; elide: Text.ElideMiddle; color: theme.ink; font.family: theme.ui }
                    Text { text: modelData.status; color: theme.inkDim; font.family: theme.ui }
                    ProgressBar { Layout.fillWidth: true; from: 0; to: Math.max(1, modelData.total); value: modelData.received; indeterminate: modelData.total <= 0 && !modelData.finished }
                    RowLayout {
                        Account.AccountButton {
                            implicitWidth: 150; implicitHeight: 36
                            visible: modelData.metadataPending
                            text: qsTr("Retry saving history"); onClicked: modelData.update()
                        }
                        Account.AccountButton {
                            implicitWidth: 110; implicitHeight: 36
                            visible: !modelData.completed && modelData.state !== WebEngineDownloadRequest.DownloadCancelled
                            text: modelData.paused || modelData.state === WebEngineDownloadRequest.DownloadInterrupted ? qsTr("Resume") : qsTr("Pause")
                            onClicked: modelData.togglePause()
                        }
                        Account.AccountButton {
                            implicitWidth: 110; implicitHeight: 36
                            visible: !modelData.finished
                            text: qsTr("Cancel"); onClicked: modelData.cancel()
                        }
                        Account.AccountButton {
                            implicitWidth: 110; implicitHeight: 36; visible: modelData.completed
                            text: qsTr("Open"); onClicked: root.openDownload(modelData.openingAuthority, true)
                        }
                        Account.AccountButton {
                            implicitWidth: 130; implicitHeight: 36; visible: modelData.completed
                            text: qsTr("Open folder"); onClicked: Qt.openUrlExternally(root.fileUrl(modelData.directory))
                        }
                    }
                }
                footer: Column {
                    width: parent.width; spacing: 8
                    Repeater {
                        model: root.history
                        delegate: RowLayout {
                            required property var modelData
                            width: parent.width
                            Text { Layout.fillWidth: true; text: modelData.fileName || qsTr("Downloaded file"); textFormat: Text.PlainText; elide: Text.ElideMiddle; color: theme.inkDim; font.family: theme.ui }
                            Account.AccountButton {
                                implicitWidth: 90; implicitHeight: 36
                                text: qsTr("Open"); onClicked: root.openDownload(modelData.id, false)
                            }
                            Account.AccountButton {
                                implicitWidth: 120; implicitHeight: 36
                                text: qsTr("Open folder")
                                onClicked: Qt.openUrlExternally(root.fileUrl(String(modelData.path).replace(/[\\/][^\\/]*$/, "")))
                            }
                        }
                    }
                }
            }
            Account.AccountButton {
                Layout.alignment: Qt.AlignRight
                text: qsTr("Close"); onClicked: root.panelOpen = false
            }
        }
    }
}
