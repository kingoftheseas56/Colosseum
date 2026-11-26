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
    property bool androidHost: Qt.platform.os === "android"
    property real dockClearance: 0
    readonly property real androidDockInset: androidHost ? dockClearance : 0
    property bool dockExpanded: true
    readonly property bool abbEnabled: ExtensionCatalog.sourceEnabled(extensions, "colosseum.well.audiobookbay")
    readonly property bool active: routeStack.depth > 0
    readonly property alias currentPage: routeStack.currentItem
    readonly property bool replacingDownload: !!currentPage && currentPage.replacesStalledDownload === true

    signal openExtensionsRequested()
    signal readAlongRequested(var ebook, string recordingKey)
    signal returnToReaderRequested(string id)
    signal minimizeRequested()
    signal fullscreenRequested()
    signal quitRequested()

    property int downloadsRevision: 0
    property var failedKeys: ({})
    property var smartDownloads: ({})
    property int smartDownloadSequence: 0
    property int smartDownloadRevision: 0
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

    function openReplacement(key, attempt, bookDetail) {
        if (!downloads || replacingDownload || !attempt) return false
        const row = downloads.activeDownloads().find(job => job.state === "failed"
            && job.id === key && job.attempt === attempt)
        if (!row || !row.pairKey || !row.title || (!row.author && !row.workId)) return false
        const intent = smartDownloads[key]
        if (intent && row.smartIntent === intent.token) stopSmartRecovery(intent)
        routeStack.pushItem(replacementPage, { stalled: row, bookDetail: bookDetail || null }, StackView.Immediate)
        return true
    }

    function startSmartDownload(request, context) {
        const key = request.recordingKey
        const token = String(++smartDownloadSequence)
        if (downloads.downloadRecording(request, token)) {
            smartDownloads[key] = { token: token, context: context, original: request,
                                    triedHashes: [request.infoHash], attempt: "",
                                    needsChoice: false, cancel: null }
            ++smartDownloadRevision
            Qt.callLater(reconcileSmartDownloads)
        }
    }

    function stopSmartRecovery(intent) {
        intent.needsChoice = true
        const cancel = intent.cancel
        intent.cancel = null
        if (cancel) cancel()
        ++smartDownloadRevision
    }

    function reconcileSmartDownloads() {
        const keys = Object.keys(smartDownloads)
        if (!keys.length) return
        const rows = downloads ? downloads.activeDownloads() : []
        for (const key of keys) {
            const intent = smartDownloads[key]
            const row = rows.find(job => job.id === key && job.smartIntent === intent.token)
            if (!row) {
                delete smartDownloads[key]
                if (intent.cancel) intent.cancel()
                ++smartDownloadRevision
            } else if (row.state === "failed" && row.attempt && row.attempt !== intent.attempt
                       && !intent.needsChoice) {
                retrySmartDownload(key, intent, row)
            }
        }
    }

    function retrySmartDownload(key, intent, stalled) {
        intent.attempt = stalled.attempt
        ++smartDownloadRevision
        if (!abbEnabled || !abb || !matcher || intent.triedHashes.length >= 3) {
            stopSmartRecovery(intent)
            return
        }

        const context = Object.assign({}, intent.context, {
            alternateTo: stalled, fallback: { original: intent.original, triedHashes: intent.triedHashes }
        })
        let completed = false
        const cancel = SmartDownload.resolve(abb, matcher, context, function(result) {
            completed = true
            intent.cancel = null
            if (smartDownloads[key] !== intent || intent.needsChoice || !abbEnabled || !downloads) return
            const stillStalled = downloads.activeDownloads().some(job => job.id === key
                && job.attempt === stalled.attempt && job.smartIntent === intent.token)
            if (!stillStalled) return
            const request = result.automatic && result.automatic.request
            if (!request || request.recordingKey !== key || intent.triedHashes.includes(request.infoHash)) {
                stopSmartRecovery(intent)
                return
            }

            intent.triedHashes.push(request.infoHash)
            if (!downloads.replaceStalledDownload(key, stalled.attempt, request, intent.token))
                stopSmartRecovery(intent)
        })
        if (!completed) intent.cancel = cancel
    }

    onAbbEnabledChanged: {
        if (!abbEnabled) Object.values(smartDownloads).forEach(intent => stopSmartRecovery(intent))
    }
    onDownloadsChanged: Qt.callLater(reconcileSmartDownloads)
    Component.onDestruction: Object.values(smartDownloads).forEach(intent => {
        if (intent.cancel) intent.cancel()
    })

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
        if (currentPage && typeof currentPage.cancelSourceLookup === "function")
            currentPage.cancelSourceLookup()
        returnSessionId = ""
        routeStack.clear(StackView.Immediate)
    }

    Connections {
        target: flow.downloads
        ignoreUnknownSignals: true
        function onActiveCountChanged() {
            ++flow.downloadsRevision
            Qt.callLater(flow.reconcileSmartDownloads)
        }
        function onDownloadPathsChanged() { ++flow.downloadsRevision }
        function onFailuresChanged() {
            ++flow.downloadsRevision
            Qt.callLater(flow.reconcileSmartDownloads)
        }
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
        // Android pages reserve the dock inside their content. On desktop the
        // collapsed dock fits beside the wide book page's series shelf.
        anchors.bottomMargin: flow.androidHost || (!flow.dockExpanded && flow.width >= 760
            && flow.height >= 700 && currentItem && currentItem.objectName === "audiobookBookPage")
                ? 0 : flow.dockClearance
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
            selectedDownloadKey: chosenUpload ? chosenUpload.recordingKey : ""
            sourceEnabled: flow.abbEnabled
            readonly property var stalledSmartDownload: {
                flow.smartDownloadRevision
                const key = selectedDownloadKey || shownRecording.key
                const intent = flow.smartDownloads[key]
                const row = intent && intent.context.workId === workId && flow.activeDownloads.find(job =>
                    job.state === "failed" && job.id === key && job.smartIntent === intent.token)
                return row ? { key: key, attempt: row.attempt, needsChoice: intent.needsChoice } : null
            }
            smartRecoveryPending: !!stalledSmartDownload && !stalledSmartDownload.needsChoice
            smartRecoveryRequired: !!stalledSmartDownload && stalledSmartDownload.needsChoice
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
            backdropBottomInset: routeStack.anchors.bottomMargin
            contentBottomInset: flow.androidDockInset
            androidHost: flow.androidHost
            onMinimizeRequested: flow.minimizeRequested()
            onFullscreenRequested: flow.fullscreenRequested()
            onQuitRequested: flow.quitRequested()

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

            function submitUpload(request, smartContext) {
                if (!request || request.sourceKind !== "abb" || !flow.abbEnabled || !flow.downloads || submitting) return
                chosenUpload = request
                const state = flow.downloadStatus(request.recordingKey).state
                if (state === "done") { flow.openListen(request.recordingKey, request); return }
                if (state && state !== "none" && state !== "failed") return
                const failures = Object.assign({}, flow.failedKeys)
                delete failures[request.recordingKey]
                flow.failedKeys = failures
                lookupError = ""
                lookupMessage = ""
                submitting = true
                if (smartContext) flow.startSmartDownload(request, smartContext)
                else flow.downloads.downloadRecording(request)
                submitting = false
            }

            function cancelSourceLookup() {
                ++lookupGeneration
                const cancel = cancelLookup
                cancelLookup = null
                lookupPending = false
                if (cancel) cancel()
            }

            function findSources() {
                if (flow.currentPage !== detail || lookupPending || submitting || primary.kind !== "download"
                        || !primary.enabled || !pairKey) return
                if (stalledSmartDownload) {
                    flow.openReplacement(stalledSmartDownload.key, stalledSmartDownload.attempt, detail)
                    return
                }
                if (!flow.downloads || !flow.abb || !flow.matcher) {
                    lookupError = qsTr("AudioBookBay is unavailable right now.")
                    return
                }
                if (chosenUpload && flow.downloadStatus(chosenUpload.recordingKey).state === "failed") {
                    submitUpload(chosenUpload)
                    return
                }

                lookupError = ""
                lookupMessage = ""
                lookupPending = true
                const generation = ++lookupGeneration
                const context = {
                    workId: workId, title: built.title, author: built.authors, pairKey: pairKey,
                    targetRecordingId: shownRecording.id, recordings: work.recordings || [], series: work.series || []
                }
                const cancel = SmartDownload.resolve(flow.abb, flow.matcher, context, function(result) {
                    if (generation !== detail.lookupGeneration || flow.currentPage !== detail || !flow.abbEnabled) return
                    detail.cancelLookup = null
                    detail.lookupPending = false
                    if (result.automatic) {
                        detail.submitUpload(result.automatic.request, context)
                    } else if (result.rows.length) {
                        routeStack.pushItem(sourcesPage, { detail: detail, options: result.rows }, StackView.Immediate)
                    } else if (result.error) {
                        detail.lookupError = result.error === "timeout" ? qsTr("AudioBookBay took too long to respond.")
                            : qsTr("AudioBookBay is unavailable right now.")
                    } else {
                        detail.lookupMessage = qsTr("No AudioBookBay matches found.")
                    }
                })
                if (generation === lookupGeneration && lookupPending) cancelLookup = cancel
            }

            Component.onCompleted: refresh()
            Component.onDestruction: cancelSourceLookup()
            StackView.onActivated: forceActiveFocus()
            StackView.onDeactivating: cancelSourceLookup()
            onSourceEnabledChanged: if (!sourceEnabled) cancelSourceLookup()
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
                detail.chosenUpload = null
                detail.lookupError = ""
                detail.lookupMessage = ""
                detail.download = null
                detail.selectedRecording = ""
                detail.workId = id
                detail.refresh()
            }
            onAuthorRequested: (personId, name) => flow.browse("byPerson", personId, name)
            onSaveRequested: if (canSave) flow.toggleSaved(workId, shownRecording.id, built.title, built.authors, coverUrl)
            onBackRequested: flow.back()
            onDownloadRequested: findSources()
            onEnableSourceRequested: flow.openExtensionsRequested()
            onCancelLookupRequested: cancelSourceLookup()
            onListenRequested: (key) => flow.openListen(key, detail.chosenUpload || {
                workId: detail.workId, recordingId: detail.shownRecording.id, title: detail.built.title,
                author: detail.built.authors, coverUrl: detail.coverUrl
            })

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
        id: sourcesPage
        Audiobook.AudiobookSourcesPage {
            id: sources
            contentBottomInset: flow.androidDockInset
            required property Item detail
            required property var options
            workTitle: detail.built.title
            workAuthor: detail.built.authors
            artwork: detail.coverUrl
            backdrop: flow.backdrop
            providerStates: ({ abb: flow.abbEnabled ? { state: "results", rows: options }
                                                   : { state: "disabled", rows: [] } })
            recordingStatus: key => { flow.downloadsRevision; return flow.downloadStatus(key).state }
            onBackRequested: flow.back()
            onExtensionsRequested: flow.openExtensionsRequested()
            onSourcePicked: row => {
                const request = row.request
                if (flow.currentPage !== sources || !request || request.sourceKind !== "abb" || !flow.abbEnabled) return
                const book = detail
                flow.back()
                book.submitUpload(request)
            }
        }
    }

    Component {
        id: replacementPage
        Audiobook.AudiobookSourcesPage {
            id: replacement
            contentBottomInset: flow.androidDockInset
            required property var stalled
            property Item bookDetail: null
            readonly property bool replacesStalledDownload: !bookDetail
            readonly property bool currentFailure: flow.activeDownloads.some(job => job.state === "failed"
                && job.id === stalled.id && job.attempt === stalled.attempt)
            readonly property bool canSearch: currentFailure && flow.abbEnabled
            readonly property var work: {
                if (!flow.catalog) return {}
                flow.catalog.revision
                return SmartDownload.recoveryWork(flow.catalog, stalled)
            }
            readonly property string lookupIdentity: JSON.stringify([work.id, workTitle, workAuthor, work.series])
            property string searchedIdentity: ""
            property var lookup: null
            property int generation: 0
            property var result: ({ state: "searching", rows: [] })
            workTitle: work.title || stalled.title
            workAuthor: FlowData.authorFor(work) || stalled.author
            artwork: work.coverUrl || work.cover || ""
            backTip: bookDetail ? qsTr("Back to audiobook details") : qsTr("Back to Downloads")
            backdrop: flow.backdrop
            providerStates: ({ abb: !currentFailure
                ? { state: "empty", message: qsTr("This stalled download is no longer in Downloads."), rows: [] }
                : flow.abbEnabled ? result : { state: "disabled", rows: [] } })
            recordingStatus: (key) => {
                flow.downloadsRevision
                const state = FlowData.status(flow.downloads, key, false).state
                return state === "done" ? "downloaded" : state === "failed" ? "none" : state
            }

            function cancelSourceLookup() {
                ++generation
                const cancel = lookup
                lookup = null
                if (cancel) cancel()
            }

            function search() {
                if (!canSearch || lookup) return
                searchedIdentity = lookupIdentity
                selectionError = ""
                if (!flow.abb) {
                    result = { state: "error", rows: [] }
                    return
                }
                if (!workAuthor) {
                    result = { state: "empty", message: qsTr("The author is needed to find another release."), rows: [] }
                    return
                }
                result = { state: "searching", rows: [] }
                const requestGeneration = ++generation
                const cancel = SmartDownload.resolve(flow.abb, flow.matcher, {
                    workId: stalled.workId || "", title: workTitle, author: workAuthor, pairKey: stalled.pairKey,
                    lookupTitle: work.title || SmartDownload.legacyTitle(stalled.title).title,
                    targetRecordingId: stalled.audiosiloRecordingId, recordings: work.recordings || [],
                    series: work.series || [], alternateTo: stalled
                }, function(response) {
                    if (requestGeneration !== replacement.generation || !replacement.canSearch) return
                    replacement.lookup = null
                    replacement.result = response.rows.length ? { state: "results", rows: response.rows }
                        : response.error ? { state: "error", error: response.error, rows: [] }
                        : { state: "empty", message: qsTr("No other releases found for this audiobook."), rows: [] }
                })
                if (requestGeneration === generation && result.state === "searching") lookup = cancel
            }

            StackView.onActivated: search()
            StackView.onDeactivating: cancelSourceLookup()
            Component.onDestruction: cancelSourceLookup()
            onCanSearchChanged: {
                if (!canSearch) cancelSourceLookup()
                else if (StackView.status === StackView.Active) search()
            }
            onLookupIdentityChanged: Qt.callLater(function() {
                if (replacement.StackView.status !== StackView.Active || !replacement.canSearch
                        || replacement.searchedIdentity === replacement.lookupIdentity) return
                replacement.cancelSourceLookup()
                replacement.search()
            })
            onBackRequested: flow.back()
            onRetryRequested: search()
            onExtensionsRequested: flow.openExtensionsRequested()
            onSourcePicked: row => {
                if (flow.currentPage !== replacement || !canSearch || !row.request) return
                if (flow.downloads.replaceStalledDownload(stalled.id, stalled.attempt, row.request)) {
                    if (bookDetail && !row.request.bundle) bookDetail.chosenUpload = row.request
                    flow.back()
                } else selectionError = qsTr("This release could not be queued.")
            }
        }
    }

    Component {
        id: playerPage
        Audiobook.AudiobookListenPage {
            id: player
            required property var route
            session: flow.session
            contentTop: 75
            contentBottomInset: flow.androidDockInset
            androidHost: flow.androidHost
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
            onMinimizeRequested: flow.minimizeRequested()
            onFullscreenRequested: flow.fullscreenRequested()
            onQuitRequested: flow.quitRequested()
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
            contentBottomInset: flow.androidDockInset
            emptyMessage: qsTr("No audiobooks found.")
            fitArtwork: true
            pageLoader: (pin, page, limit, options, done) => ShelfApi.loadPage(flow.catalog, pin, page, done)
            StackView.onActivated: forceActiveFocus()
            onItemRequested: (item) => flow.openWork(item.raw.workId || item.raw.id, item.raw.recordingId)
            onBackRequested: flow.back()
        }
    }
}
