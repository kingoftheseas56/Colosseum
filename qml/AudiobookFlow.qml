import QtQuick
import QtQuick.Controls
import "audiobook" as Audiobook
import "AudiobookShelfApi.js" as ShelfApi

FocusScope {
    id: flow

    property var world: null
    property var catalog: (typeof AudiobookCatalog !== "undefined") ? AudiobookCatalog : null
    property var collectionStore: (typeof Collection !== "undefined") ? Collection : null
    property Item backdrop: null
    property real dockClearance: 0
    readonly property bool active: routeStack.depth > 0
    readonly property alias currentPage: routeStack.currentItem

    signal downloadRequested(string workId)
    signal listenRequested(string recordingKey)

    visible: active
    focus: active

    function openWork(workId, recordingId, item) {
        const download = item && item.recordingKey && item.downloaded !== false && !item.missing ? item : null
        if (workId || download)
            routeStack.pushItem(bookPage, { workId: String(workId || ""), selectedRecording: String(recordingId || ""),
                                download: download },
                                StackView.Immediate)
    }

    function browse(method, key, title) {
        if (key)
            routeStack.pushItem(cataloguePage, { pin: { method: method, key: key, title: title } },
                                StackView.Immediate)
    }

    function back() {
        if (routeStack.depth > 1) routeStack.popCurrentItem(StackView.Immediate)
        else clear()
    }

    function clear() { routeStack.clear(StackView.Immediate) }

    Connections {
        target: flow.world
        function onAudiobookRequested(item) {
            if (item) flow.openWork(item.workId, item.recordingId, item)
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
            readonly property string collectionId: workId ? "audiobook:" + workId : ""
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
                const records = Array.prototype.slice.call(next.recordings || [])
                const selected = selectedRecording ? records.findIndex(record => record.id === selectedRecording) : -1
                if (download) {
                    if (selected >= 0) {
                        records[selected] = Object.assign({}, records[selected], {
                            recordingKey: download.recordingKey, local: true
                        })
                    } else {
                        records.push({
                            id: selectedRecording, recordingKey: download.recordingKey, local: true,
                            narrators: download.narrators, language: download.language,
                            abridgement: download.abridgement, coverUrl: download.coverUrl || download.cover
                        })
                    }
                }
                const ordered = selected > 0
                    ? [records[selected]].concat(records.slice(0, selected), records.slice(selected + 1)) : records
                if (JSON.stringify(work) !== JSON.stringify(next)) work = next
                if (JSON.stringify(recordings) !== JSON.stringify(ordered)) recordings = ordered
                const works = series.works || []
                if (JSON.stringify(seriesWorks) !== JSON.stringify(works)) seriesWorks = works
                const state = catalog && workId && typeof catalog.remoteQueryState === "function"
                    ? catalog.remoteQueryState("work", [workId]) : null
                const pending = state ? state.pending : catalog && catalog.remotePending && catalog.remotePending()
                catalogueStatus = next.id || next.workId || download ? ""
                    : pending ? qsTr("Loading audiobook…") : qsTr("This audiobook is unavailable right now.")
            }

            Component.onCompleted: refresh()
            StackView.onActivated: forceActiveFocus()
            Connections {
                target: flow.catalog && flow.catalog.revisionChanged !== undefined ? flow.catalog : null
                ignoreUnknownSignals: true
                function onRevisionChanged() { detail.refresh() }
                function onReadyChanged() { detail.refresh() }
                function onRequestFailed() { if (detail.catalogueStatus.length) detail.refresh() }
            }
            onSeriesBookRequested: (id) => {
                if (id === detail.workId) return
                detail.download = null
                detail.selectedRecording = ""
                detail.workId = id
                detail.refresh()
            }
            onAuthorRequested: (personId, name) => flow.browse("byPerson", personId, name)
            onSaveRequested: {
                if (!canSave) return
                if (saved) flow.collectionStore.remove("biblio", collectionId)
                else flow.collectionStore.add("biblio", {
                    id: collectionId, type: "audiobook", title: built.title, cover: coverUrl,
                    payload: { kind: "audiobook", workId: workId, recordingId: shownRecording.id,
                               author: built.authors, cover: coverUrl }
                })
            }
            onBackRequested: flow.back()
            onDownloadRequested: (id) => flow.downloadRequested(id)
            onListenRequested: (key) => flow.listenRequested(key)

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
