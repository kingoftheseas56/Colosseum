import QtQuick
import QtQuick.Window
import QtTest
import "../qml"

Item {
    id: harness
    width: 1280
    height: 900
    property var sheet: null

    Window {
        id: hostWindow
        width: 1280
        height: 900
        visible: true
        property bool androidHost: true
        Item { id: surface; anchors.fill: parent }
    }

    Component {
        id: sourcesComponent
        SourcesSheet { anchors.fill: parent }
    }

    TestCase {
        name: "SourcesSheetCodecCompatibility"
        when: windowShown

        function init() {
            hostWindow.width = 1280
            hostWindow.androidHost = true
            harness.sheet = sourcesComponent.createObject(surface)
            verify(harness.sheet !== null, "create the production SourcesSheet")
            harness.sheet.mode = "play"
            harness.sheet.qualityFilter = "all"
            harness.sheet.rows = [
                { quality: "4K", rank: 4, seeders: 1, size: "2 GB", audio: "English",
                  release: "HEVC Main10 sample", tags: ["4K", "HEVC"], languages: [],
                  streamKind: "Torrent", addonName: "Fixture", addonId: "fixture",
                  infoHash: "unsupported-hash", fileIdx: 0, checkState: "confirmed",
                  videoCompatibility: { state: "unsupported", reason: "No HEVC Main10 decoder is available" } },
                { quality: "1080p", rank: 3, seeders: 4, size: "1 GB", audio: "English",
                  release: "Unknown codec sample", tags: ["1080p"], languages: [],
                  streamKind: "Torrent", addonName: "Fixture", addonId: "fixture",
                  infoHash: "unknown-hash", fileIdx: 0, checkState: "confirmed",
                  videoCompatibility: { state: "unknown", reason: "Codec profile is not stated" } }
            ]
            harness.sheet.open = true
            tryCompare(harness.sheet, "rowCount", 2)
            wait(50)
        }

        function cleanup() {
            if (harness.sheet) {
                harness.sheet.destroy()
                harness.sheet = null
            }
            hostWindow.width = 1280
            hostWindow.androidHost = true
        }

        function test_wideAndroidMarksOnlyUnsupportedAndRetainsRows() {
            tryCompare(harness.sheet, "phoneLayout", false)
            compare(harness.sheet.visibleRows.length, 2, "All retains every source")
            var unsupported = findChild(harness.sheet, "sourceCompatibilityWide_0")
            var unknown = findChild(harness.sheet, "sourceCompatibilityWide_1")
            verify(unsupported !== null, "wide row exposes its compatibility marker")
            verify(unsupported.visible, "known unsupported row is marked")
            compare(unsupported.text, "Unsupported on this device — No HEVC Main10 decoder is available")
            verify(unknown !== null && !unknown.visible, "unknown codec state is never presented as unsupported")
            var unsupportedRow = findChild(harness.sheet, "sourceRow_0")
            verify(unsupportedRow !== null)
            compare(unsupportedRow.checkState, "confirmed", "transport readiness remains independent")
        }

        function test_phoneAndroidMarksOnlyUnsupportedAndRetainsRows() {
            hostWindow.width = 400
            tryCompare(harness.sheet, "phoneLayout", true)
            compare(harness.sheet.visibleRows.length, 2, "phone All retains every source")
            var unsupported = findChild(harness.sheet, "sourceCompatibilityPhone_0")
            var unknown = findChild(harness.sheet, "sourceCompatibilityPhone_1")
            verify(unsupported !== null && unsupported.visible, "phone row visibly marks known unsupported")
            compare(unsupported.text, "Unsupported on this device — No HEVC Main10 decoder is available")
            verify(unknown !== null && !unknown.visible, "phone unknown state is not condemned")
            var unsupportedRow = findChild(harness.sheet, "sourceRow_0")
            compare(unsupportedRow.checkState, "confirmed", "phone status does not replace transport readiness")
        }
    }
}
