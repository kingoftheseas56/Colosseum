import QtQuick
import QtTest
import "../../qml" as App

TestCase {
    id: testCase
    name: "ComicDownloadCancel"
    when: windowShown

    Window {
        id: host
        visible: true
        width: 354
        height: 792
        property bool androidHost: true
    }
    App.Theme { id: theme }
    QtObject {
        id: api
        property string state: "downloading"
        property int cancels: 0
        property string cancelledId: ""
        signal progress(string issueId, double done, double total)
        signal finished(string issueId)
        signal failed(string issueId, string reason)
        signal removed(string issueId)
        function statusOf(id) { return {state: state, done: 10, total: 100} }
        function cancelDownload(id) { cancels++; cancelledId = id }
        function searchTorrentSources() {}
        function cancelTorrentSourceSearch() {}
        function downloadTorrentEdition() {}
    }
    Component { id: ledgerComponent; App.ComicDbLedger {} }
    Component { id: pickerComponent; App.ComicTorrentSourcesPage {} }

    function byName(root, name) {
        if (root.objectName === name) return root
        for (const child of root.children || []) {
            const found = byName(child, name)
            if (found) return found
        }
        return null
    }

    function init() {
        api.state = "downloading"; api.cancels = 0; api.cancelledId = ""
    }

    function makeLedger(width) {
        host.width = width
        const ledger = createTemporaryObject(ledgerComponent, host.contentItem,
            {width: width, theme: theme, androidHost: true})
        verify(ledger !== null)
        return ledger
    }

    function test_edition_cancel_data() {
        return [{tag: "320-downloading", width: 320, state: "downloading"},
            {tag: "354-resolving", width: 354, state: "resolving"},
            {tag: "354-queued", width: 354, state: "queued"},
            {tag: "354-choosing", width: 354, state: "choosing"},
            {tag: "354-extracting", width: 354, state: "extracting"},
            {tag: "wide-downloading", width: 1280, state: "downloading"}]
    }

    function test_edition_cancel(data) {
        api.state = data.state
        const ledger = makeLedger(data.width)
        verify("comicsApi" in ledger, "Ledger must use the existing Comics facade for cancellation")
        ledger.comicsApi = api
        ledger.dbSeries = {title: "Local comic", editions: [
            {locg_comic_id: "local-edition", display_title: "Local collected edition",
             format: "Compendium", available: false, sources: []}]}
        wait(30)
        const edition = byName(ledger, "comicDbEdition_local-edition")
        const cancel = byName(ledger, "comicDbCancel_local-edition")
        verify(edition !== null && cancel !== null, "Active edition exposes Cancel here")
        verify(!edition.hasSource, "Alternate-source-only editions are covered")
        verify(cancel.visible && cancel.enabled)
        verify(cancel.height >= 48 && cancel.width >= 48)
        const point = cancel.mapToItem(edition, 0, 0)
        verify(point.x >= 0 && point.x + cancel.width <= edition.width)
        verify(point.y >= 0 && point.y + cancel.height <= edition.height)
        mouseClick(cancel, cancel.width / 2, cancel.height / 2)
        compare(api.cancels, 1)
        compare(api.cancelledId, "local-edition")
        verify(!cancel.enabled, "Do not send duplicate cancellation while awaiting the facade")
        api.progress("another-edition", 90, 100)
        api.removed("another-edition")
        compare(edition.dlState, data.state)
        api.removed("local-edition")
        compare(edition.dlState, "none")
        verify(!cancel.visible)
        verify(edition.canAlternate)
    }

    function test_completed_edition_keeps_read_action() {
        api.state = "done"
        const ledger = makeLedger(354)
        verify("comicsApi" in ledger)
        ledger.comicsApi = api
        ledger.dbSeries = {title: "Local comic", editions: [{locg_comic_id: "local-edition", title: "Local", sources: []}]}
        wait(20)
        const edition = byName(ledger, "comicDbEdition_local-edition")
        verify(!byName(ledger, "comicDbCancel_local-edition").visible)
        compare(edition.dlState, "done")
        compare(api.cancels, 0)
    }

    function test_picker_cancels_inspection_without_leaving_results() {
        host.width = 354
        const page = createTemporaryObject(pickerComponent, host.contentItem, {comicsApi: api})
        page.show({issueId: "local-edition", seriesId: "local", seriesTitle: "Local", editionTitle: "Local edition"})
        page.applySources("local-edition", [{infoHash: "local", confidence: "strong"}], true)
        page.selectRow(page.rows[0])
        wait(20)
        const cancel = byName(page, "comicTorrentCancelInspection")
        verify(cancel !== null && cancel.visible, "Inspection offers an explicit Cancel")
        mouseClick(cancel, cancel.width / 2, cancel.height / 2)
        compare(api.cancels, 1)
        compare(api.cancelledId, "local-edition")
        compare(page.selectionState, "results")
        verify(page.open && !page.acquiring)
        compare(page.rows.length, 1)
        api.progress("local-edition", 10, 100)
        verify(page.open, "A stale progress tick after cancel does not close the picker")
        page.hide()
        compare(api.cancels, 1, "Closing after explicit cancel does not cancel twice")
    }
}
