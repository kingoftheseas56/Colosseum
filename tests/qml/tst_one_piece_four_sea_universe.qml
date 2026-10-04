import QtQuick
import QtQuick.Window
import QtTest
import "../../qml" as Colosseum
import "../../qml/OnePieceAtlasBadgeLayout.js" as BadgeLayout

TestCase {
    id: tests
    name: "OnePieceFourSeaUniverse"
    when: windowShown

    Window { id: testWindow; width: 1280; height: 720; visible: true }
    Component { id: pageComponent; Colosseum.OnePieceUniversePage { reducedMotion: true } }
    Component {
        id: roundBadgeComponent
        Colosseum.OnePieceRoundBadge {
            width: 66; height: 66
            source: Qt.resolvedUrl("../../assets/universes/one-piece/paradise/badges/water-7.png")
        }
    }
    property var page: null
    SignalSpy { id: arcSpy; target: tests.page; signalName: "arcRequested" }

    function init() {
        testWindow.width = 1280
        testWindow.height = 720
        page = pageComponent.createObject(testWindow.contentItem)
        verify(page !== null)
        testWindow.requestActivate()
        tryVerify(function() { return page.activeAtlas !== null }, 4000)
        arcSpy.clear()
    }
    function cleanup() { page.destroy(); page = null }
    function sea(id) {
        compare(page.showSea(id), true)
        tryCompare(page, "activeSeaId", id)
        tryVerify(function() { return page.activeAtlas !== null }, 4000)
    }
    function activate(name) {
        wait(20) // Complete the loaded atlas's deferred initial focus.
        var button = findChild(page, name)
        verify(button !== null, name)
        verify(button.enabled && button.visible, name + " is interactive")
        button.forceActiveFocus()
        keyClick(name === "eastBlueToParadise" ? Qt.Key_Space : Qt.Key_Return)
    }

    function test_keyboard_sea_gates_connect_the_whole_voyage() {
        compare(page.activeSeaId, "east-blue")
        activate("eastBlueToParadise")
        tryCompare(page, "activeSeaId", "paradise")
        tryVerify(function() { return page.activeAtlas !== null }, 4000)
        activate("paradiseNewWorld")
        tryCompare(page, "activeSeaId", "new-world")
        tryVerify(function() { return page.activeAtlas !== null }, 4000)
        activate("newWorldBeyondWano")
        tryCompare(page, "activeSeaId", "final-sea")
        tryVerify(function() { return page.activeAtlas !== null }, 4000)
        activate("finalSeaToNewWorld")
        tryCompare(page, "activeSeaId", "new-world")
        activate("newWorldToParadise")
        tryCompare(page, "activeSeaId", "paradise")
        activate("paradiseEastBlue")
        tryCompare(page, "activeSeaId", "east-blue")
    }

    function test_east_blue_starts_left_and_exits_right() {
        sea("east-blue")
        var atlas = page.activeAtlas
        var start = atlas.markerForTest("romance")
        var end = atlas.markerForTest("loguetown")
        verify(start !== null && end !== null)
        verify(start.x < end.x, "Romance Dawn is left of Loguetown")
        var gate = findChild(atlas, "eastBlueToParadise")
        verify(gate !== null)
        var gateCenter = gate.mapToItem(atlas, gate.width / 2, gate.height / 2)
        verify(gateCenter.x > atlas.width / 2, "Paradise gate is on the right")
        verify(gateCenter.x < atlas.width, "Paradise gate remains visible")
    }

    function test_badge_sizes_follow_each_maps_actual_scale() {
        var viewports = [[1280, 720], [900, 360]]
        for (var v = 0; v < viewports.length; ++v) {
            testWindow.width = viewports[v][0]
            testWindow.height = viewports[v][1]
            wait(20)
            for (var zoom = 1; zoom <= 2; ++zoom) {
                sea("east-blue")
                page.activeAtlas.zoom = zoom
                var seas = ["paradise", "new-world", "final-sea"]
                for (var i = 0; i < seas.length; ++i) {
                    sea(seas[i])
                    page.activeAtlas.zoom = zoom
                    var atlas = page.activeAtlas
                    var designDiameter = seas[i] === "paradise" ? 56 : 66
                    compare(atlas.sealDiameter, designDiameter * atlas.stageGeometry.scale,
                            seas[i] + " badge follows chart scale at zoom " + zoom)
                }
            }
        }
    }

    function test_round_badge_keeps_corners_transparent_at_every_size() {
        var badge = roundBadgeComponent.createObject(testWindow.contentItem)
        verify(badge !== null)
        tryCompare(badge, "status", Image.Ready, 5000)
        var sizes = [44, 66, 132]
        for (var i = 0; i < sizes.length; ++i) {
            badge.width = sizes[i]; badge.height = sizes[i]
            tryCompare(badge, "status", Image.Ready, 5000)
            wait(50)
            var canvas = findChild(badge, "onePieceRoundBadgeCanvas")
            var ctx = canvas.getContext("2d")
            var w = canvas.canvasSize.width, h = canvas.canvasSize.height
            compare(ctx.getImageData(0, 0, 1, 1).data[3], 0)
            compare(ctx.getImageData(w - 1, 0, 1, 1).data[3], 0)
            compare(ctx.getImageData(0, h - 1, 1, 1).data[3], 0)
            compare(ctx.getImageData(w - 1, h - 1, 1, 1).data[3], 0)
            var decoded = findChild(badge, "onePieceRoundBadgeDecoded")
            verify(ctx.getImageData(Math.floor(w / 2), Math.floor(h / 2), 1, 1).data[3] > 0,
                   "Painted center at size " + sizes[i] + "; decoded " + decoded.implicitWidth + "x" + decoded.implicitHeight)
        }
        badge.source = ""
        tryCompare(badge, "status", Image.Null)
        wait(50)
        var empty = canvas.getContext("2d").getImageData(Math.floor(w / 2), Math.floor(h / 2), 1, 1)
        compare(empty.data[3], 0)
        badge.destroy()
    }

    function test_badges_clear_island_drawings_and_labels_at_every_zoom() {
        var seas = ["paradise", "new-world", "final-sea"]
        var viewports = [[1280, 720], [900, 600]]
        for (var v = 0; v < viewports.length; ++v) {
            testWindow.width = viewports[v][0]; testWindow.height = viewports[v][1]
            for (var i = 0; i < seas.length; ++i) {
                sea(seas[i])
                var atlas = page.activeAtlas
                tryCompare(atlas, "captureReady", true, 5000)
                for (var zoom = 1; zoom <= 2; zoom += 0.5) {
                    atlas.zoom = zoom
                    wait(30)
                    var areas = atlas.badgeProtectedAreas
                    verify(areas.length > 0)
                    for (var m = 0; m < atlas.markerEntries.length; ++m) {
                        var marker = atlas.markerItem(atlas.markerEntries[m].id)
                        verify(marker.visible, marker.markerId + " remains available at " + viewports[v] + " zoom " + zoom)
                        var cx = (marker.x + marker.width / 2) / atlas.stageGeometry.scale
                        var cy = (marker.y + marker.height / 2) / atlas.stageGeometry.scale
                        var radius = (atlas.sealDiameter + 9) / (2 * atlas.stageGeometry.scale)
                        for (var a = 0; a < areas.length; ++a)
                            verify(!BadgeLayout.circleOverlapsRect(cx, cy, radius, areas[a]),
                                   marker.markerId + " clears drawing " + a + " at zoom " + zoom)
                    }
                }
            }
        }
    }

    function test_return_preserves_map_and_only_active_sea_consumes_escape() {
        sea("paradise")
        var paradise = page.activeAtlas
        paradise.setZoom(1.4)
        var zoom = paradise.zoom
        paradise.openPreview("whisky-peak", "whisky-peak")
        compare(page.atlasTransientOpen, true)
        compare(page.requestEscape(), true)
        compare(page.atlasTransientOpen, false)
        compare(page.requestEscape(), false)
        sea("new-world")
        verify(!paradise.visible && !paradise.enabled)
        sea("paradise")
        compare(page.activeAtlas, paradise)
        compare(paradise.zoom, zoom)
        verify(paradise.visible && paradise.enabled)
        compare(page.showSea("unknown-sea"), false)
        compare(page.activeSeaId, "paradise")
    }

    function test_each_sea_opens_the_shared_arc_contract() {
        var rows = [
            { sea: "east-blue", marker: "arlong", arc: "arlong" },
            { sea: "paradise", marker: "whisky-peak", arc: "whisky-peak" },
            { sea: "new-world", marker: "wano-country", arc: "wano-country" },
            { sea: "final-sea", marker: "elbaf", arc: "elbaf" }
        ]
        for (var i = 0; i < rows.length; ++i) {
            var row = rows[i]
            sea(row.sea)
            page.activeAtlas.openPreview(row.marker, row.arc)
            compare(page.activeAtlas.previewVisible, true, row.arc + " preview opens")
            page.activeAtlas.openSelectedArc()
            compare(arcSpy.count, i + 1)
            var arc = arcSpy.signalArguments[i][0]
            compare(arc.id, row.arc)
            compare(arc.seaId, row.sea)
            if (row.sea !== "east-blue") {
                verify(arc.heroSource.indexOf("/" + row.sea + "/banners/") >= 0)
                verify(arc.relatedMedia !== undefined)
            }
            if (row.arc === "elbaf") {
                compare(arc.anime, "1156-1180")
                compare(arc.storyRanges.anime, "1156+")
            }
        }
    }

    function test_arc_overlay_disables_map_then_restores_its_focus() {
        sea("final-sea")
        var atlas = page.activeAtlas
        wait(20)
        var marker = atlas.markerItem("egghead")
        marker.forceActiveFocus()
        atlas.openSelectedArc()
        verify(marker.activeFocus)
        page.arcPageOpen = true
        verify(!atlas.enabled)
        page.arcPageOpen = false
        page.takeKeyboardFocus()
        tryVerify(function() { return atlas.activeFocus && marker.activeFocus }, 2000)
    }
}
