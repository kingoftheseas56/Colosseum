import QtQuick
import QtQuick.Controls
import "audiobook" as Audiobook
import "AudiobookShelfApi.js" as ShelfApi
import "AudiobookFlowData.js" as FlowData
import "AudiobookSmartDownload.js" as SmartDownload
import "AbbApi.js" as Abb
import "ExtensionsCatalog.js" as ExtensionCatalog

FocusScope {
    id: flow

    property var world: null
    property var catalog: (typeof AudiobookCatalog !== "undefined") ? AudiobookCatalog : null
    property var collectionStore: (typeof Collection !== "undefined") ? Collection : null
    property var extensions: (typeof Extensions !== "undefined") ? Extensions : null
    property var downloads: (typeof Audiobooks !== "undefined") ? Audiobooks : null
    property var matcher: (typeof AudiobookRecordingMatcher !== "undefined") ? AudiobookRecordingMatcher : null
    property var abb: ({ search: Abb.search, detail: Abb.fetchDetail, searchTerm: Abb.searchTerm, relevantRows: Abb.relevantRows })
    property var session: null
    property Item backdrop: null
    property real dockClearance: 0
    readonly property bool sourceEnabled: ExtensionCatalog.sourceEnabled(extensions, "colosseum.well.audiobookbay")
    readonly property bool active: routeStack.depth > 0
    readonly property alias currentPage: routeStack.currentItem

    signal openExtensionsRequested()

    property int downloadsRevision: 0
    property var failedKeys: ({})
    readonly property var activeDownloads: (downloadsRevision, downloads ? downloads.activeDownloads() : [])

    visible: active
    focus: active

    function openWork(workId, recordingId, item) {
        const download = item && item.recordingKey && item.downloaded !== false && !item.missing ? item : null
        if (workId || download)
            routeStack.pushItem(bookPage, { workId: String(workId || ""), selectedRecording: String(recordingId || ""),
                                download: download },
                                StackView.Immediate)
    }

    function openListen(recordingKey, item) {
        if (!session || !downloads || !recordingKey || !downloads.isDownloaded(recordingKey)) return false
        const owned = downloads.downloadedAudiobooks().find(row => row.recordingKey === recordingKey)
        const route = Object.assign({}, item || {}, owned || {}, { recordingKey: recordingKey })
        const work = !route.bundle && route.workId && catalog ? catalog.work(route.workId) : {}
        session.openFor(recordingKey, FlowData.playerBook(route, work))
        if (!session.ready || session.activePairKey !== recordingKey) return false
        routeStack.pushItem(playerPage, { route: route }, StackView.Immediate)
        return true
    }

    function downloadStatus(key) {
        const failed = failedKeys[key] || activeDownloads.some(job =>
            job.state === "failed" && (job.recordingKey || job.id) === key)
        return FlowData.status(downloads, key, failed)
    }

    function toggleSaved(workId, recordingId, title, author, cover) {
        if (!collectionStore || !workId) return
        const id = "audiobook:" + workId
        if (collectionStore.has("biblio", id)) collectionStore.remove("biblio", id)
        else collectionStore.add("biblio", {
            id: id, type: "audiobook", title: title, cover: cover,
            payload: { kind: "audiobook", workId: workId, recordingId: recordingId || "",
                       author: author, cover: cover }
        })
    }

    function browse(method, key, title) {
        if (key)
            routeStack.pushItem(cataloguePage, { pin: { method: method, key: key, title: title } },
                                StackView.Immediate)
    }

    function back() {
        const page = currentPage
        if (page && page.objectName === "audiobookBookPage" && page.dismissDownloadUi()) return
        if (page && page.objectName === "audiobookListenPage" && page.chaptersOpen) {
            page.chaptersOpen = false
            return
        }
        if (routeStack.depth > 1) routeStack.popCurrentItem(StackView.Immediate)
        else clear()
    }

    function clear() { routeStack.clear(StackView.Immediate) }

    Connections {
        target: flow.downloads
        ignoreUnknownSignals: true
        function onActiveCountChanged() { ++flow.downloadsRevision }
        function onFailuresChanged() { ++flow.downloadsRevision }
        function onResolving(key) { ++flow.downloadsRevision }
        function onProgress(key, received, total) { ++flow.downloadsRevision }
        function onRemoved(key) { ++flow.downloadsRevision }
        function onFinished(key, path) {
            const failures = Object.assign({}, flow.failedKeys)
            delete failures[key]
            flow.failedKeys = failures
            ++flow.downloadsRevision
        }
        function onFailed(key, reason) {
            const failures = Object.assign({}, flow.failedKeys)
            failures[key] = reason || true
            flow.failedKeys = failures
            ++flow.downloadsRevision
        }
    }

    Connections {
        target: flow.world
        function onAudiobookRequested(item) {
            if (!item) return
            if ((item.downloaded || item.bundle) && flow.openListen(item.recordingKey, item)) return
            if (!item.bundle) flow.openWork(item.workId, item.recordingId, item)
        }
        function onAudiobookGenreRequested(genre) { flow.browse("genre", genre, ShelfApi.genreTitle(genre)) }
        function onAudiobookShelfRequested(key) {
            const shelf = (flow.catalog ? flow.catalog.shelves() : []).find(row => row.key === key)
            if (shelf) flow.browse("shelf", key, shelf.title)
        }
    }

    MouseArea {
        anchors.fill: parent
        onWheel: (wheel) => wheel.accepted = true
    }
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0; color: "#0c0f18" }
            GradientStop { position: 1; color: "#06070b" }
        }
    }

    StackView {
        id: routeStack
        anchors.fill: parent
        anchors.bottomMargin: flow.dockClearance
    }

    Component {
        id: bookPage
        Audiobook.AudiobookBookPage {
            id: detail
            required property string workId
            property string selectedRecording
            property var download: null
            property string catalogueStatus
            property var chosenUpload: null
            property var cancelLookup: null
            property int lookupGeneration: 0
            property bool submitting: false
            readonly property string collectionId: workId ? "audiobook:" + workId : ""
            readonly property string pairKey: FlowData.pairFor(work)
            readonly property var ownedSources: (flow.downloadsRevision, flow.downloads && pairKey
                ? Array.prototype.slice.call(flow.downloads.recordingsForWork(pairKey))
                    .concat(flow.activeDownloads.filter(job => job.workId && job.workId === workId))
                : [])
            sourceEnabled: flow.sourceEnabled
            selectedDownloadKey: chosenUpload ? chosenUpload.recordingKey : ""
            recordings: FlowData.recordingsFor(work, selectedRecording, chosenUpload || download, ownedSources, !flow.downloads)
            downloadState: {
                flow.downloadsRevision
                const states = ({})
                recordings.forEach(record => {
                    if (record.recordingKey) states[record.recordingKey] = flow.downloadStatus(record.recordingKey)
                })
                if (selectedDownloadKey) states[selectedDownloadKey] = flow.downloadStatus(selectedDownloadKey)
                return states
            }
            canSave: !!flow.collectionStore && collectionId.length > 0 && built.workId === workId
            saved: canSave && (flow.collectionStore.revision, flow.collectionStore.has("biblio", collectionId))
            backdrop: flow.backdrop

            function refresh() {
                const catalog = flow.catalog
                const catalogueWork = catalog && workId ? catalog.work(workId) : {}
                const next = catalogueWork.id || catalogueWork.workId || !download ? catalogueWork : {
                    id: workId, title: download.title, authors: download.author ? [download.author] : [],
                    coverUrl: download.coverUrl || download.cover
                }
                const membership = next.series && next.series.length ? next.series[0] : null
                const series = membership && membership.id ? catalog.series(membership.id) : {}
                if (JSON.stringify(work) !== JSON.stringify(next)) work = next
                const works = series.works || []
                if (JSON.stringify(seriesWorks) !== JSON.stringify(works)) seriesWorks = works
                const state = catalog && workId && typeof catalog.remoteQueryState === "function"
                    ? catalog.remoteQueryState("work", [workId]) : null
                const pending = state ? state.pending : catalog && catalog.remotePending && catalog.remotePending()
                catalogueStatus = next.id || next.workId || download ? ""
                    : pending ? qsTr("Loading audiobook…") : qsTr("This audiobook is unavailable right now.")
            }

            function cancelSourceLookup() {
                ++lookupGeneration
                const cancel = cancelLookup
                cancelLookup = null
                lookupPending = false
                if (cancel) cancel()
            }

            function dismissDownloadUi() {
                if (picker.opened) { picker.close(); return true }
                if (lookupPending) { cancelSourceLookup(); return true }
                return false
            }

            function submitUpload(request) {
                if (!request || !flow.downloads || submitting) return
                if (!flow.sourceEnabled) { flow.openExtensionsRequested(); return }
                chosenUpload = request
                const state = flow.downloadStatus(request.recordingKey).state
                if (state && state !== "none" && state !== "failed") return
                const failures = Object.assign({}, flow.failedKeys)
                delete failures[request.recordingKey]
                flow.failedKeys = failures
                downloadError = ""
                submitting = true
                flow.downloads.downloadRecording(request)
                submitting = false
            }

            function findSources() {
                if (lookupPending || picker.opened || submitting || primary.kind !== "download") return
                if (!flow.downloads || !flow.matcher || !workId) return
                if (chosenUpload && flow.downloadStatus(chosenUpload.recordingKey).state === "failed") {
                    submitUpload(chosenUpload)
                    return
                }
                downloadError = ""
                lookupPending = true
                const generation = ++lookupGeneration
                const cancel = SmartDownload.resolve(flow.abb, flow.matcher, {
                    workId: workId, title: built.title, author: built.authors, pairKey: pairKey,
                    targetRecordingId: shownRecording.id, recordings: work.recordings || [], series: work.series || []
                }, function(result) {
                    if (generation !== detail.lookupGeneration) return
                    detail.cancelLookup = null
                    detail.lookupPending = false
                    if (result.error) {
                        detail.downloadError = result.error === "timeout"
                            ? qsTr("AudioBookBay took too long to respond.") : qsTr("AudioBookBay is unavailable right now.")
                    } else if (result.automatic && flow.sourceEnabled) {
                        detail.submitUpload(result.automatic.request)
                    } else if (flow.sourceEnabled) {
                        picker.rows = result.rows
                        picker.open()
                    }
                })
                if (generation === lookupGeneration && lookupPending) cancelLookup = cancel
            }

            Component.onCompleted: refresh()
            Component.onDestruction: cancelSourceLookup()
            StackView.onActivated: forceActiveFocus()
            StackView.onDeactivating: {
                cancelSourceLookup()
                picker.close()
            }
            onSourceEnabledChanged: if (!sourceEnabled) {
                cancelSourceLookup()
                picker.close()
            }
            Connections {
                target: flow.catalog && flow.catalog.revisionChanged !== undefined ? flow.catalog : null
                ignoreUnknownSignals: true
                function onRevisionChanged() { detail.refresh() }
                function onReadyChanged() { detail.refresh() }
                function onRequestFailed() { if (detail.catalogueStatus.length) detail.refresh() }
            }
            onSeriesBookRequested: (id) => {
                if (id === detail.workId) return
                detail.cancelSourceLookup()
                picker.close()
                detail.chosenUpload = null
                detail.downloadError = ""
                detail.download = null
                detail.selectedRecording = ""
                detail.workId = id
                detail.refresh()
            }
            onAuthorRequested: (personId, name) => flow.browse("byPerson", personId, name)
            onSaveRequested: if (canSave) flow.toggleSaved(workId, shownRecording.id, built.title, built.authors, coverUrl)
            onBackRequested: flow.back()
            onDownloadRequested: (id) => detail.findSources()
            onEnableSourceRequested: flow.openExtensionsRequested()
            onCancelLookupRequested: detail.cancelSourceLookup()
            onListenRequested: (key) => flow.openListen(key, detail.chosenUpload || {
                workId: detail.workId, recordingId: detail.shownRecording.id, title: detail.built.title,
                author: detail.built.authors, coverUrl: detail.coverUrl
            })

            Audiobook.AudiobookSourcePicker {
                id: picker
                parent: detail
                workTitle: detail.built.title
                onSourcePicked: (row) => detail.submitUpload(row.request)
            }

            Text {
                anchors.centerIn: parent
                visible: detail.catalogueStatus.length > 0
                text: detail.catalogueStatus
                color: "#a5a7b0"
                font.pixelSize: 15
            }
        }
    }

    Component {
        id: playerPage
        Audiobook.AudiobookListenPage {
            id: player
            required property var route
            session: flow.session
            downloaded: true
            contentTop: 75
            saveAvailable: !!flow.collectionStore && !!route.workId && !route.bundle
            saved: saveAvailable && (flow.collectionStore.revision,
                flow.collectionStore.has("biblio", "audiobook:" + route.workId))
            book: {
                if (route.bundle || !route.workId || !flow.catalog) return FlowData.playerBook(route, {})
                flow.catalog.revision
                return FlowData.playerBook(route, flow.catalog.work(route.workId))
            }
            StackView.onActivated: forceActiveFocus()
            onBookChanged: if (session && route && session.activePairKey === route.recordingKey) session.book = book
            onBackRequested: flow.back()
            onListenRequested: if (session.paused) session.togglePlay()
            onSaveToggled: flow.toggleSaved(route.workId, route.audiosiloRecordingId || route.recordingId,
                                            bookTitle, book.author, book.cover)
        }
    }

    Component {
        id: cataloguePage
        TheatreSeeAllPage {
            emptyMessage: qsTr("No audiobooks found.")
            squareArtwork: true
            pageLoader: (pin, page, limit, options, done) => ShelfApi.loadPage(flow.catalog, pin, page, done)
            StackView.onActivated: forceActiveFocus()
            onItemRequested: (item) => flow.openWork(item.raw.workId || item.raw.id, item.raw.recordingId)
            onBackRequested: flow.back()
        }
    }
}
