import QtQuick
import QtTest
import "../../qml" as FinalSea

Item {
    id: host
    width: 1280
    height: 720

    Component {
        id: atlasComponent
        FinalSea.OnePieceFinalSeaAtlas { reducedMotion: true }
    }
    Component { id: taskbarComponent; FinalSea.Taskbar { z: 900 } }

    TestCase {
        name: "FinalSeaNativeAtlas"
        when: windowShown
        property var atlas: null
        property var taskbar: null

        SignalSpy { id: arcSpy; signalName: "arcRequested" }
        SignalSpy { id: backSpy; signalName: "newWorldRequested" }

        function init() {
            host.width = 1280
            host.height = 720
            atlas = atlasComponent.createObject(host, {width:host.width,height:host.height})
            verify(atlas !== null)
            arcSpy.target = atlas
            backSpy.target = atlas
            arcSpy.clear(); backSpy.clear()
            tryCompare(atlas, "captureReady", true, 5000)
            mouseMove(host, 2, 2)
        }
        function cleanup() {
            if (taskbar) taskbar.destroy()
            taskbar = null
            arcSpy.target = null
            backSpy.target = null
            if (atlas) atlas.destroy()
            atlas = null
            wait(0)
        }
        function control(name) {
            var item = findChild(atlas, name)
            verify(item !== null, "Missing control: " + name)
            return item
        }

        function test_generated_map_keeps_exact_aspect_and_locked_badges() {
            var artwork = control("finalSeaMapArtwork")
            compare(artwork.designWidth, 1672)
            compare(artwork.designHeight, 941)
            verify(artwork.visible)
            verify(atlas.captureReady, "native artwork has presented a frame")
            fuzzyCompare(atlas.stageGeometry.width / atlas.stageGeometry.height, 1672 / 941, 0.00001)
            var centers = {egghead: [0.38215, 0.47510], elbaf: [0.86965, 0.37725]}
            for (var zoom of [1, 1.5, 2]) {
                atlas.setZoom(zoom)
                wait(30)
                for (var id of ["egghead", "elbaf"]) {
                    var marker = atlas.markerItem(id)
                    verify(marker.visible)
                    fuzzyCompare((marker.x + marker.width / 2) / atlas.stageGeometry.width, centers[id][0], 0.00001)
                    fuzzyCompare((marker.y + marker.height / 2) / atlas.stageGeometry.height, centers[id][1], 0.00001)
                    verify(marker.diameter / atlas.stageGeometry.scale >= 84)
                }
            }
        }

        function test_native_controls_follow_painted_map_controls() {
            // Painted stations keep their chart positions; interactive sizes follow
            // the painted plate but never drop below the 44 px hit-target floor.
            var controls = [
                ["finalSeaIndexButton", 0.8439, 0.0372, 0.1340, 0.0574],
                ["finalSeaToNewWorld", 0.0299, 0.8055, 0.1441, 0.0616]
            ]
            for (var row of controls) {
                var button = control(row[0])
                var origin = button.mapToItem(atlas, 0, 0)
                var painted = atlas.mapPoint(row[1], row[2])
                fuzzyCompare(origin.x, painted.x, 0.2)
                fuzzyCompare(origin.y, painted.y, 0.2)
                fuzzyCompare(button.width, Math.max(44, row[3] * atlas.stageGeometry.width), 0.2)
                fuzzyCompare(button.height, Math.max(44, row[4] * atlas.stageGeometry.height), 0.2)
                compare(button.mapArtwork, false)
                compare(button.contentItem.visible, true)
            }
            // Zoom station: visible controls clamped right of the shell taskbar dock's
            // closed footprint (leftEdge<=80 + 130 wide).
            var tools = control("finalSeaZoomTools")
            var paintedOrigin = atlas.mapPoint(0.0299, 0.8831)
            var dockRight = Math.max(18, Math.min(80, atlas.width * 0.045)) + 130
            fuzzyCompare(tools.x, Math.max(paintedOrigin.x, 218), 0.2)
            fuzzyCompare(tools.y, paintedOrigin.y, 0.2)
            verify(tools.x >= dockRight, "zoom station must sit clear of the shell taskbar dock")
            var zoomWidths = [["finalSeaZoomOut", 0.0353], ["finalSeaZoomReset", 0.0604], ["finalSeaZoomIn", 0.0359]]
            for (var z of zoomWidths) {
                var zoomButton = control(z[0])
                fuzzyCompare(zoomButton.width, Math.max(44, z[1] * atlas.stageGeometry.width), 0.2)
                fuzzyCompare(zoomButton.height, Math.max(44, 0.0584 * atlas.stageGeometry.height), 0.2)
                compare(zoomButton.mapArtwork, false)
                compare(zoomButton.contentItem.visible, true)
            }
            mouseClick(control("finalSeaZoomIn"))
            compare(atlas.zoom, 1.25)
            atlas.takeKeyboardFocus()
            keyClick(Qt.Key_0)
            compare(atlas.zoom, 1)
        }

        // Regression for the 1.1.9 acceptance finding: finalSeaToNewWorld measured
        // 39.5472 px high at 1280x720 against the 44 px minimum, and the painted
        // zoom station sat under the shell taskbar dock.
        function test_return_gate_keeps_44px_target_and_zoom_station_clears_dock() {
            var sizes = [[1280, 720], [1364, 1024]]
            for (var s of sizes) {
                host.width = s[0]; host.height = s[1]
                atlas.width = s[0]; atlas.height = s[1]
                wait(30)
                var gate = control("finalSeaToNewWorld")
                verify(gate.width >= 44 && gate.height >= 44,
                       "TO NEW WORLD keeps the 44 px target at " + s[0] + "x" + s[1])
                var gatePoint = gate.mapToItem(atlas, 0, 0)
                verify(gatePoint.x >= 0 && gatePoint.y >= 0
                       && gatePoint.x + gate.width <= atlas.width
                       && gatePoint.y + gate.height <= atlas.height,
                       "TO NEW WORLD stays fully inside the window at " + s[0] + "x" + s[1])
                var index = control("finalSeaIndexButton")
                verify(index.width >= 44 && index.height >= 44,
                       "painted Index keeps the 44 px target at " + s[0] + "x" + s[1])
                var dock = {
                    x: Math.max(18, Math.min(80, atlas.width * 0.045)),
                    y: atlas.height - 80, width: 130, height: 64
                }
                var names = ["finalSeaZoomOut", "finalSeaZoomReset", "finalSeaZoomIn"]
                for (var n = 0; n < names.length; ++n) {
                    var zoomButton = control(names[n])
                    verify(zoomButton.width >= 44 && zoomButton.height >= 44)
                    var point = zoomButton.mapToItem(atlas, 0, 0)
                    var overlapsDock = point.x < dock.x + dock.width
                            && point.x + zoomButton.width > dock.x
                            && point.y < dock.y + dock.height
                            && point.y + zoomButton.height > dock.y
                    verify(!overlapsDock,
                           names[n] + " must not sit under the shell taskbar dock at " + s[0] + "x" + s[1])
                }
            }
        }

        function test_keyboard_index_stays_on_screen_when_map_is_zoomed() {
            atlas.setZoom(2)
            wait(30)
            atlas.takeKeyboardFocus()
            keyClick(Qt.Key_I)
            compare(atlas.indexVisible, true)
            var panel = control("finalSeaIndexPanel")
            verify(panel.x >= 0 && panel.y >= 0)
            verify(panel.x + panel.width <= atlas.width)
            verify(panel.y + panel.height <= atlas.height)
        }

        function test_visible_zoom_symbols_drive_their_own_action() {
            taskbar = taskbarComponent.createObject(host)
            verify(taskbar !== null)
            for (var size of [[1280, 720], [1364, 1024], [1920, 1080]]) {
                host.width = size[0]; host.height = size[1]
                atlas.width = size[0]; atlas.height = size[1]
                atlas.setZoom(1)
                wait(30)
                compare(findChild(atlas, "finalSeaPaintedZoomCover"), null,
                        "native artwork has no obsolete painted zoom station to mask")
                for (var name of ["finalSeaZoomIn", "finalSeaZoomOut", "finalSeaZoomReset"]) {
                    wait(30) // settle the viewport's deferred zoom scroll before the next pointer event
                    var button = control(name)
                    verify(button.contentItem.visible, name + " must paint its actionable symbol")
                    var symbol = button.contentItem.mapToItem(atlas,
                                    button.contentItem.width / 2, button.contentItem.height / 2)
                    var before = atlas.zoom
                    verify(symbol.x >= 0 && symbol.x < atlas.width
                           && symbol.y >= 0 && symbol.y < atlas.height)
                    mouseClick(atlas, symbol.x, symbol.y)
                    compare(atlas.zoom, name === "finalSeaZoomIn" ? before + 0.25
                            : name === "finalSeaZoomOut" ? before - 0.25 : 1)
                    compare(taskbar.open, false, "The shell dock must not intercept a zoom symbol")
                }
            }
            var home = findChild(taskbar, "colosseumTaskbarHomeButton")
            verify(home !== null)
            mouseClick(home, home.width / 2, home.height / 2)
            compare(taskbar.open, true, "The production shell dock must itself remain clickable")
        }

        function test_unknown_ids_fail_closed() {
            compare(atlas.focusArc("future-island-that-does-not-exist"), false)
            compare(atlas.focusLocation("laugh-tale"), false)
            compare(atlas.previewVisible, false)
            compare(arcSpy.count, 0)
        }

        function test_focus_then_explicit_open_is_only_arc_navigation() {
            verify(atlas.focusArc("egghead"))
            tryCompare(atlas, "selectedArcId", "egghead")
            compare(arcSpy.count, 0)
            var open = control("finalSeaOpenArc")
            open.forceActiveFocus()
            keyClick(Qt.Key_Return)
            compare(arcSpy.count, 1)
            compare(arcSpy.signalArguments[0][0].id, "egghead")
        }

        function test_local_art_and_bounded_payload_preserve_open_story_ranges() {
            for (var i = 0; i < atlas.arcEntries.length; ++i) {
                var entry = atlas.arcEntries[i]
                verify(atlas.focusArc(entry.id))
                tryCompare(control("finalSeaBadge-" + entry.id), "status", Image.Ready, 5000)
                tryCompare(control("finalSeaPreviewHero"), "status", Image.Ready, 5000)
                compare(arcSpy.count, i)
                atlas.openSelectedArc()
                compare(arcSpy.count, i + 1)
                var payload = arcSpy.signalArguments[i][0]
                compare(payload.seaId, "final-sea")
                compare(payload.anime, entry.providerAnimeSpec)
                compare(payload.volumes, entry.providerVolumeSpec)
                compare(payload.storyRanges.anime, entry.anime)
                compare(payload.storyRanges.chapters, entry.chapters)
                compare(payload.storyRanges.volumes, entry.volumes)
                if (entry.id === "elbaf") {
                    compare(entry.anime, "1156+")
                    compare(entry.chapters, "1127+")
                    compare(entry.volumes, "111+")
                    compare(payload.anime, "1156-1180")
                    compare(payload.volumes, "111-115")
                    compare(payload.storyRanges.status, "ongoing")
                }
                payload.onePacePrefixes.push("test-only")
                compare(entry.onePacePrefixes.length, entry.id === "egghead" ? 1 : 0)
            }
        }

        function test_pointer_click_on_seal_does_not_navigate() {
            var marker = atlas.markerItem("elbaf")
            verify(marker !== null)
            mouseMove(marker, marker.width/2, marker.height/2)
            tryCompare(atlas, "previewVisible", true)
            mouseClick(marker, marker.width/2, marker.height/2, Qt.LeftButton)
            compare(arcSpy.count, 0)
        }

        function test_supporting_locations_resolve_to_parent_arc() {
            verify(atlas.focusLocation("labophase"))
            compare(atlas.selectedArcId, "egghead")
            verify(atlas.focusLocation("owl-library"))
            compare(atlas.selectedArcId, "elbaf")
            verify(atlas.focusLocation("heaven-world"))
            compare(atlas.selectedArcId, "elbaf")
        }

        function test_index_has_only_two_voyage_arcs() {
            mouseClick(control("finalSeaIndexButton"))
            compare(atlas.indexVisible, true)
            verify(findChild(atlas, "finalSeaIndexRow-egghead") !== null)
            verify(findChild(atlas, "finalSeaIndexRow-elbaf") !== null)
            compare(findChild(atlas, "finalSeaIndexRow-laugh-tale"), null)
        }

        function test_escape_closes_preview_then_index() {
            atlas.focusArc("elbaf")
            compare(atlas.requestEscape(), true)
            compare(atlas.previewVisible, false)
            mouseClick(control("finalSeaIndexButton"))
            compare(atlas.requestEscape(), true)
            compare(atlas.indexVisible, false)
            compare(atlas.requestEscape(), false)
        }

        function test_zoom_bounds_and_back_seam() {
            atlas.setZoom(99)
            compare(atlas.zoom, 2)
            atlas.setZoom(-2)
            compare(atlas.zoom, 1)
            mouseClick(control("finalSeaToNewWorld"))
            compare(backSpy.count, 1)
            compare(arcSpy.count, 0)
        }

        function test_directional_focus_moves_between_the_two_story_seals() {
            atlas.focusArc("egghead")
            var first = atlas.selectedMarkerId
            keyClick(Qt.Key_Right)
            verify(atlas.selectedMarkerId !== first)
            compare(atlas.selectedArcId, "elbaf")
        }

        function test_preview_text_is_keyboard_scrollable_when_constrained() {
            atlas.focusArc("elbaf")
            var card = control("finalSeaArcPreview")
            card.height = 220
            var flick = control("finalSeaPreviewContent")
            var open = control("finalSeaOpenArc")
            open.forceActiveFocus()
            tryVerify(function() { return flick.contentHeight > flick.height })
            keyClick(Qt.Key_PageDown)
            verify(flick.contentY > 0)
            keyClick(Qt.Key_PageUp)
            compare(flick.contentY, 0)
        }

        function test_target_viewports_keep_preview_on_screen_data() {
            return [
                {tag:"1280x720",w:1280,h:720},
                {tag:"1600x900",w:1600,h:900},
                {tag:"1920x1080",w:1920,h:1080}
            ]
        }
        function test_target_viewports_keep_preview_on_screen(data) {
            host.width=data.w; host.height=data.h
            atlas.width=data.w; atlas.height=data.h
            for (var i=0;i<atlas.arcEntries.length;++i) {
                verify(atlas.focusArc(atlas.arcEntries[i].id))
                wait(30)
                var card=control("finalSeaArcPreview")
                verify(card.x>=0 && card.y>=0)
                verify(card.x+card.width<=atlas.width+1)
                verify(card.y+card.height<=atlas.height+1)
            }
        }
    }
}
