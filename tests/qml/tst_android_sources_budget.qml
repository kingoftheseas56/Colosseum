import QtQuick
import QtQuick.Window
import QtTest 1.3
import "../../qml" as App

// PF-02: production SourcesSheet height budget at the supported Android desktop-presentation
// viewports. The logical viewport is derived from DesktopPresentationScale's 1024x640 reference
// divided by the launch visual zoom (1.15 default, 1.20 override); these are calculated
// logical sizes, not measured device rectangles (device acceptance stays pending).
TestCase {
    id: testCase
    name: "AndroidSourcesBudget"
    when: windowShown

    property var sheet: null
    property int playCount: 0
    property string playedHash: ""

    Window {
        id: hostWindow
        width: 1280
        height: 900
        visible: true
        property bool androidHost: false
        property bool desktopPresentation: true
        Item { id: surface; anchors.fill: parent }
    }

    Component {
        id: sourcesComponent
        App.SourcesSheet { anchors.fill: parent }
    }

    Connections {
        target: testCase.sheet
        ignoreUnknownSignals: true
        function onPlayRequested(infoHash) {
            testCase.playCount += 1
            testCase.playedHash = infoHash
        }
    }

    function makeRows(n, qualities, addons) {
        var out = []
        for (var i = 0; i < n; ++i) {
            out.push({
                infoHash: "hash" + i, fileIdx: 0,
                quality: qualities[i % qualities.length],
                addonName: addons[i % addons.length],
                release: "Example.Release." + i + ".mkv",
                seeders: 10 + i, streamKind: "Torrent", audio: "AAC", languages: []
            })
        }
        return out
    }

    function findChild(root, name) {
        if (!root)
            return null
        if (root.objectName === name)
            return root
        var children = root.children || []
        for (var i = 0; i < children.length; ++i) {
            var found = findChild(children[i], name)
            if (found)
                return found
        }
        return null
    }

    function openSheet(width, height, android, rows) {
        hostWindow.width = width
        hostWindow.height = height
        hostWindow.androidHost = android
        sheet = sourcesComponent.createObject(surface)
        verify(sheet !== null)
        sheet.title = "Example film"
        sheet.rows = rows
        sheet.open = true
        sheet.opacity = 1
        waitForRendering(sheet)
        return sheet
    }

    function cleanup() {
        if (sheet) {
            sheet.destroy()
            sheet = null
        }
        playCount = 0
        playedHash = ""
        hostWindow.androidHost = false
    }

    function listRect() {
        var list = findChild(sheet, "sourcesSheetList")
        verify(list !== null)
        return { list: list, rect: list.mapToItem(sheet, 0, 0, list.width, list.height) }
    }

    function supportedViewports() {
        // 1024x640 reference / zoom; 20:9 phones are height-limited so width exceeds 1024/zoom.
        return [
            { zoom: 1.0, width: 1422, height: 640 },
            { zoom: 1.15, width: 1237, height: Math.floor(640 / 1.15) },
            { zoom: 1.20, width: 1185, height: Math.floor(640 / 1.20) }
        ]
    }

    function test_desktop_composition_unchanged() {
        openSheet(1280, 900, false, makeRows(6, ["1080p", "720p"], ["Torrentio"]))
        verify(!sheet.compactHeight)
        compare(findChild(sheet, "sourcesSheetBanner").height, 300)
        var r = listRect()
        // H - 544 with one quality-pill row (desktop budget, Windows behavior unchanged).
        compare(Math.round(r.rect.height), 900 - 544)
    }

    function test_desktop_short_window_not_rebudgeted() {
        // Windows/desktop geometry stays exactly as before even when short.
        openSheet(1280, 600, false, makeRows(6, ["1080p"], ["Torrentio"]))
        verify(!sheet.compactHeight)
        compare(findChild(sheet, "sourcesSheetBanner").height, 300)
    }

    function test_android_tall_viewport_keeps_desktop_banner() {
        openSheet(1422, 900, true, makeRows(6, ["1080p", "720p"], ["Torrentio"]))
        verify(!sheet.compactHeight)
        compare(findChild(sheet, "sourcesSheetBanner").height, 300)
    }

    function test_android_supported_viewports_show_complete_row() {
        var vps = supportedViewports()
        for (var i = 0; i < vps.length; ++i) {
            var vp = vps[i]
            openSheet(vp.width, vp.height, true, makeRows(8, ["4K", "1080p", "720p", "SD"], ["Torrentio", "Comet"]))
            var r = listRect()
            verify(r.rect.height >= 150, "zoom " + vp.zoom + " list height " + r.rect.height)
            verify(r.rect.y + r.rect.height <= sheet.height, "list stays inside the sheet")
            var banner = findChild(sheet, "sourcesSheetBanner")
            verify(banner.height >= sheet.compactBannerMin)
            // Title block never overlaps Back and stays inside the banner.
            var title = findChild(sheet, "sourcesSheetTitleBlock")
            verify(title.y + title.height <= banner.height + 0.5, "title inside banner at zoom " + vp.zoom)
            cleanup()
        }
    }

    function test_wrapped_filters_keep_row_budget() {
        // Narrow-wide window forces quality pills to wrap; still one complete row.
        openSheet(360, Math.floor(640 / 1.20), true, makeRows(8, ["4K", "1080p", "720p", "SD"], ["Torrentio"]))
        var pills = null
        var r = listRect()
        verify(r.rect.height >= 150, "wrapped list height " + r.rect.height)
    }

    function test_final_row_reachable_and_selectable() {
        openSheet(1185, Math.floor(640 / 1.20), true, makeRows(12, ["1080p"], ["Torrentio"]))
        var r = listRect()
        r.list.positionViewAtEnd()
        waitForRendering(sheet)
        var last = r.list.itemAtIndex(11)
        verify(last !== null, "final source row instantiated")
        var p = last.mapToItem(r.list, 0, 0)
        verify(p.y >= -0.5 && p.y + last.height <= r.list.height + 0.5, "final row fully visible")
        // Explicit choice of that exact row only (no auto selection).
        compare(playCount, 0)
        sheet.activateSource(11)
        compare(playCount, 1)
        compare(playedHash, "hash11")
    }

    function test_empty_and_error_states_fit() {
        var vp = supportedViewports()[2]
        openSheet(vp.width, vp.height, true, [])
        sheet.timedOut = true
        var empty = findChild(sheet, "sourcesSheetEmptyText")
        verify(empty.visible)
        var p = empty.mapToItem(sheet, 0, 0)
        verify(p.y > findChild(sheet, "sourcesSheetBanner").height - 1)
        verify(p.y + empty.height <= sheet.height)
    }

    function test_extension_menu_bounded_and_scrollable() {
        var addons = []
        for (var i = 0; i < 12; ++i) addons.push("Ext" + i)
        openSheet(1185, Math.floor(640 / 1.20), true, makeRows(12, ["1080p"], addons))
        sheet.openExtMenu(null)
        waitForRendering(sheet)
        var menu = findChild(sheet, "extPickerMenu")
        verify(menu.visible)
        var p = menu.mapToItem(sheet, 0, 0)
        verify(p.y + menu.height <= sheet.height, "menu inside sheet")
        var flick = findChild(sheet, "extPickerScroll")
        verify(flick.interactive)
        // Keyboard to the final extension scrolls it into view.
        sheet.extMenuIndex = 12
        var top = 8 + 12 * 52
        verify(top >= flick.contentY && top + 52 <= flick.contentY + flick.height + 0.5)
    }
}
