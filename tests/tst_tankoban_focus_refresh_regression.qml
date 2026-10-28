// Actual production-component unit tests, not native catalogue qualification.
// In-memory service rows only; no network, acquisition, or media input.
import QtQuick
import QtTest

Item {
    width: 1600
    height: 900
    QtObject {
        id: service
        property var rows: []
        signal volumesChanged(string seriesId)
        function volumesForSeries(seriesId) { return rows }
    }
    QtObject {
        id: progress
        function get(kind, id) { return null }
        function all() { return [] }
    }
    TestCase {
        name: "TankobanFocusRefresh"
        when: windowShown
        property var library: null
        property var flow: null
        function model(count) {
            var rows = []
            for (var i = 1; i <= count; ++i)
                rows.push({id: "unit:" + i, number: String(i), name: "Volume " + i,
                           cover: "", state: "none"})
            return rows
        }
        function init() {
            service.rows = model(113)
            var component = Qt.createComponent("../qml/MangaTankobanLibrary.qml")
            compare(component.status, Component.Ready, component.errorString())
            library = component.createObject(parent, {width: 1600, height: 800,
                seriesId: "unit-series", seriesTitle: "Unit series", service: service, progress: progress})
            verify(library !== null)
            tryCompare(library, "renderedCount", 113)
            flow = findChild(library, "volumeFlow")
            verify(flow !== null)
            flow.forceActiveFocus() // Unit focus setup; never used by native UI driver.
            tryCompare(flow, "activeFocus", true)
            keyClick(Qt.Key_Home)
            tryCompare(library, "focusIndex", 0)
        }
        function cleanup() {
            if (library) library.destroy()
            library = null
            flow = null
            wait(1)
        }
        function advanceSix() {
            for (var i = 0; i < 6; ++i) keyClick(Qt.Key_Right)
            tryCompare(library, "focusIndex", 6)
            tryCompare(flow, "currentIndex", 6)
        }
        function publish(rows) {
            service.rows = rows
            service.volumesChanged(library.seriesId)
            wait(100)
        }
        function test_metadata_refresh_preserves_user_focus() {
            advanceSix()
            console.log("FOCUS_REFRESH_BEFORE " + JSON.stringify({focusIndex: library.focusIndex,
                currentIndex: flow.currentIndex, rows: library.renderedCount}))
            publish(service.rows.map(function(row) {
                return {id: row.id, number: row.number, name: row.name,
                        cover: row.cover, state: row.state, synopsis: "New metadata only"}
            }))
            console.log("FOCUS_REFRESH_AFTER " + JSON.stringify({focusIndex: library.focusIndex,
                currentIndex: flow.currentIndex, rows: library.renderedCount}))
            compare(library.focusIndex, 6)
            compare(flow.currentIndex, 6)
        }
        function test_insert_before_selected_preserves_identity() {
            advanceSix()
            var rows = service.rows.slice()
            rows.unshift({id: "unit:extra", number: "0", name: "Extra", cover: "", state: "none"})
            publish(rows)
            compare(library.focusToken, "7")
            compare(library.focusIndex, 7)
            compare(flow.currentIndex, 7)
        }
        function test_removed_selection_rehomes() {
            advanceSix()
            publish(service.rows.filter(function(row) { return row.number !== "7" }))
            compare(library.focusToken, "1")
            compare(library.focusIndex, 0)
            compare(flow.currentIndex, 0)
        }
        function test_new_series_rehomes() {
            advanceSix()
            library.seriesId = "second-unit-series"
            tryCompare(library, "focusIndex", 0)
            tryCompare(flow, "currentIndex", 0)
        }
        function test_empty_then_repopulate_rehomes() {
            advanceSix()
            publish([])
            compare(library.renderedCount, 0)
            compare(library.autoLandIndex, -1)
            publish(model(113))
            compare(library.focusIndex, 0)
            compare(flow.currentIndex, 0)
        }
        function test_deferred_centre_uses_latest_selection() {
            advanceSix()
            service.rows = model(113)
            service.volumesChanged(library.seriesId)
            keyClick(Qt.Key_Right)
            tryCompare(library, "focusIndex", 7)
            tryCompare(flow, "currentIndex", 7)
        }
    }
}
