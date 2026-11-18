import QtQuick
import QtQuick.Controls
import "audiobook" as Audiobook
import "AudiobookShelfApi.js" as ShelfApi

FocusScope {
    id: flow

    property var world: null
    property var catalog: (typeof AudiobookCatalog !== "undefined") ? AudiobookCatalog : null
    property Item backdrop: null
    readonly property bool active: routeStack.depth > 0
    readonly property alias currentPage: routeStack.currentItem

    signal downloadRequested(string workId)
    signal listenRequested(string recordingKey)

    visible: active
    focus: active

    function openWork(workId, recordingId) {
        if (workId)
            routeStack.pushItem(bookPage, { workId: String(workId), selectedRecording: String(recordingId || "") },
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
            if (item) flow.openWork(item.workId, item.recordingId)
        }
        function onAudiobookGenreRequested(genre) { flow.browse("genre", genre, genre) }
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
    }

    Component {
        id: bookPage
        Audiobook.AudiobookBookPage {
            id: detail
            required property string workId
            property string selectedRecording
            property string catalogueStatus
            backdrop: flow.backdrop

            function refresh() {
                const catalog = flow.catalog
                const next = catalog ? catalog.work(workId) : {}
                const membership = next.series && next.series.length ? next.series[0] : null
                const series = membership && membership.id ? catalog.series(membership.id) : {}
                const records = next.recordings || []
                const selected = selectedRecording ? records.findIndex(record => record.id === selectedRecording) : -1
                const ordered = selected > 0
                    ? [records[selected]].concat(records.slice(0, selected), records.slice(selected + 1)) : records
                if (JSON.stringify(work) !== JSON.stringify(next)) work = next
                if (JSON.stringify(recordings) !== JSON.stringify(ordered)) recordings = ordered
                const works = series.works || []
                if (JSON.stringify(seriesWorks) !== JSON.stringify(works)) seriesWorks = works
                const state = catalog && typeof catalog.remoteQueryState === "function"
                    ? catalog.remoteQueryState("work", [workId]) : null
                const pending = state ? state.pending : catalog && catalog.remotePending && catalog.remotePending()
                catalogueStatus = next.id || next.workId ? ""
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
                detail.selectedRecording = ""
                detail.workId = id
                detail.refresh()
            }
            onAuthorRequested: (personId, name) => flow.browse("byPerson", personId, name)
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
            pageLoader: (pin, page, limit, options, done) => ShelfApi.loadPage(flow.catalog, pin, page, done)
            StackView.onActivated: forceActiveFocus()
            onItemRequested: (item) => flow.openWork(item.raw.workId || item.raw.id, item.raw.recordingId)
            onBackRequested: flow.back()
        }
    }
}
