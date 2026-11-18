import QtQuick
import QtQuick.Controls
import "audiobook" as Audiobook
import "AudiobookShelfApi.js" as ShelfApi
import "AudiobookFlowData.js" as FlowData
import "AudiobookBridgeData.js" as Bridge
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
    property var ebookLibrary: []
    property string returnSessionId: ""
    property Item backdrop: null
    property real dockClearance: 0
    property var librivox: (typeof LibriVoxCatalogue !== "undefined") ? LibriVoxCatalogue : null
    property var tankorent: (typeof AudiobookTorrents !== "undefined") ? AudiobookTorrents : null
    readonly property bool abbEnabled: ExtensionCatalog.sourceEnabled(extensions, "colosseum.well.audiobookbay")
    readonly property bool librivoxEnabled: !!librivox && ExtensionCatalog.sourceEnabled(extensions, "colosseum.well.librivox")
    readonly property bool tankorentEnabled: !!tankorent && ExtensionCatalog.sourceEnabled(extensions, "colosseum.well.indexers")
    property int lastLookupToken: 0
    readonly property bool active: routeStack.depth > 0
    readonly property alias currentPage: routeStack.currentItem

    signal openExtensionsRequested()
    signal readAlongRequested(var ebook, string recordingKey)
    signal returnToReaderRequested(string id)

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

    function openListen(recordingKey, item, jump) {
        if (!session || !downloads || !recordingKey || !downloads.isDownloaded(recordingKey)) return false
        const owned = downloads.downloadedAudiobooks().find(row => row.recordingKey === recordingKey)
        const route = Object.assign({}, item || {}, owned || {}, { recordingKey: recordingKey })
        const work = !route.bundle && route.workId && catalog ? catalog.work(route.workId) : {}
        if (!session.ready || session.activePairKey !== recordingKey)
            session.openFor(recordingKey, FlowData.playerBook(route, work))
        if (!session.ready || session.activePairKey !== recordingKey) return false
        if (currentPage && currentPage.objectName === "audiobookListenPage") {
            if (currentPage.route.recordingKey !== recordingKey) currentPage.route = route
            currentPage.listeningJump = jump || null
            return true
        }
        routeStack.pushItem(playerPage, { route: route, listeningJump: jump || null }, StackView.Immediate)
        return true
    }

    function refreshPlayerRoute() {
        if (currentPage && currentPage.objectName === "audiobookListenPage" && session && session.ready
                && currentPage.route.recordingKey !== session.activePairKey)
            openListen(session.activePairKey)
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
        else {
            const reader = returnSessionId
            clear()
            if (reader) returnToReaderRequested(reader)
        }
    }

    function clear() {
        returnSessionId = ""
        routeStack.clear(StackView.Immediate)
    }

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
            property var sourceLookup: null
            property bool submitting: false
            readonly property string collectionId: workId ? "audiobook:" + workId : ""
            readonly property string pairKey: FlowData.pairFor(work)
            readonly property var ownedSources: (flow.downloadsRevision, flow.downloads && pairKey
                ? Array.prototype.slice.call(flow.downloads.recordingsForWork(pairKey))
                    .concat(flow.activeDownloads.filter(job => job.workId && job.workId === workId))
                : [])
            readonly property bool tankorentForWork: flow.tankorentEnabled && flow.tankorent.isEligible(work)
            readonly property var enabledSources: [
                flow.abbEnabled && flow.matcher ? "abb" : "",
                flow.librivoxEnabled ? "librivox" : "",
                tankorentForWork ? "tankorent" : ""
            ].filter(Boolean)
            sourceEnabled: enabledSources.length > 0
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
                const lookup = sourceLookup
                sourceLookup = null
                lookupPending = false
                if (!lookup) return
                if (lookup.abbCancel) lookup.abbCancel()
                if (lookup.pending.librivox) lookup.librivox.cancel(lookup.token)
                if (lookup.pending.tankorent) lookup.tankorent.cancel(lookup.token)
            }

            function dismissDownloadUi() {
                if (picker.opened) { picker.close(); return true }
                if (lookupPending) { cancelSourceLookup(); return true }
                return false
            }

            function submitUpload(request) {
                if (!request || !flow.downloads || submitting) return
                if (!enabledSources.includes(request.sourceKind)) { flow.openExtensionsRequested(); return }
                chosenUpload = request
                const state = flow.downloadStatus(request.recordingKey).state
                if (state === "done") { flow.openListen(request.recordingKey, request); return }
                if (state && state !== "none" && state !== "failed") return
                const failures = Object.assign({}, flow.failedKeys)
                delete failures[request.recordingKey]
                flow.failedKeys = failures
                downloadError = ""
                submitting = true
                flow.downloads.downloadRecording(request)
                submitting = false
            }

            function acceptSources(kind, token, rows, error, automatic) {
                const lookup = sourceLookup
                if (!lookup || lookup.token !== token || !lookup.pending[kind]) return
                delete lookup.pending[kind]
                lookup.rows[kind] = Array.prototype.slice.call(rows || [])
                if (error) {
                    const label = kind === "librivox" ? qsTr("LibriVox")
                        : kind === "tankorent" ? qsTr("Tankorent") : qsTr("AudioBookBay")
                    lookup.errors.push(error === "timeout" ? qsTr("%1 timed out.").arg(label)
                                                           : qsTr("%1 is unavailable right now.").arg(label))
                }
                if (Object.keys(lookup.pending).length) return
                sourceLookup = null
                lookupPending = false
                if (automatic && lookup.abbOnly) {
                    submitUpload(automatic.request)
                    return
                }
                const choices = [].concat(lookup.rows.librivox || [], lookup.rows.abb || [], lookup.rows.tankorent || [])
                if (!choices.length && lookup.errors.length) {
                    downloadError = lookup.errors.join(" ")
                    return
                }
                picker.rows = choices
                picker.notice = lookup.errors.join(" ")
                picker.open()
            }

            function librivoxRows(books) {
                return Array.prototype.map.call(books || [], function(book) {
                    return Object.assign({}, book, {
                        sourceLabel: qsTr("LibriVox"), format: "MP3", fileCount: book.chapters.length,
                        request: {
                            sourceKind: "librivox", sourceRef: book.id, chapters: book.chapters,
                            pairKey: detail.pairKey, workId: detail.workId,
                            recordingKey: detail.pairKey + "#librivox:" + book.id,
                            title: book.title, author: book.author, narrators: book.narrators,
                            format: "MP3", language: book.language
                        }
                    })
                })
            }

            function findSources() {
                if (lookupPending || picker.opened || submitting || primary.kind !== "download") return
                if (!flow.downloads || !workId || !pairKey || !enabledSources.length) return
                if (chosenUpload && enabledSources.includes(chosenUpload.sourceKind)
                    && flow.downloadStatus(chosenUpload.recordingKey).state === "failed") {
                    submitUpload(chosenUpload)
                    return
                }
                downloadError = ""
                const token = String(++flow.lastLookupToken)
                const pending = ({})
                enabledSources.forEach(kind => pending[kind] = true)
                const lookup = { token: token, pending: pending, rows: {}, errors: [],
                                 librivox: flow.librivox, tankorent: flow.tankorent,
                                 abbOnly: enabledSources.length === 1 && pending.abb, abbCancel: null }
                sourceLookup = lookup
                lookupPending = true
                if (pending.librivox) lookup.librivox.search(token, built.title, built.authors)
                if (pending.tankorent) lookup.tankorent.search(token, work, shownRecording.id, pairKey)
                if (pending.abb) {
                    lookup.abbCancel = SmartDownload.resolve(flow.abb, flow.matcher, {
                        workId: workId, title: built.title, author: built.authors, pairKey: pairKey,
                        targetRecordingId: shownRecording.id, recordings: work.recordings || [], series: work.series || []
                    }, function(result) {
                        const rows = result.rows.map(row => Object.assign({}, row, { sourceLabel: qsTr("AudioBookBay") }))
                        detail.acceptSources("abb", token, rows, result.error, result.automatic)
                    })
                }
            }

            Connections {
                target: flow.librivox
                function onSearchFinished(token, rows, error) {
                    if (detail.sourceLookup && detail.sourceLookup.token === token)
                        detail.acceptSources("librivox", token, detail.librivoxRows(rows), error)
                }
            }

            Connections {
                target: flow.tankorent
                function onSearchFinished(token, rows, error) {
                    detail.acceptSources("tankorent", token, rows, error)
                }
            }

            Component.onCompleted: refresh()
            Component.onDestruction: cancelSourceLookup()
            StackView.onActivated: forceActiveFocus()
            StackView.onDeactivating: {
                cancelSourceLookup()
                picker.close()
            }
            onEnabledSourcesChanged: {
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
            readonly property var ebook: Bridge.ownedEbook(Object.assign({}, route, book), flow.ebookLibrary)
            ebookAvailable: !!ebook
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
            onReadAlongRequested: if (ebookAvailable) {
                listeningJump = null
                flow.readAlongRequested(ebook, route.recordingKey)
            }
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
