import QtQuick
import QtTest
import "../../qml" as Paradise

Item {
    id: host
    width: 1280
    height: 720

    Component {
        id: atlasComponent
        Paradise.OnePieceParadiseAtlas { reducedMotion: true }
    }

    TestCase {
        id: suite
        name: "ParadiseNativeAtlas"
        when: windowShown
        property var atlas: null

        SignalSpy { id: arcSpy; signalName: "arcRequested" }
        SignalSpy { id: nextSpy; signalName: "newWorldRequested" }
        SignalSpy { id: previousSpy; signalName: "eastBlueRequested" }

        function init() {
            failOnWarning(/ReferenceError|TypeError|Binding loop detected/)
            host.width = 1280
            host.height = 720
            atlas = atlasComponent.createObject(host, { width: host.width, height: host.height })
            verify(atlas !== null)
            arcSpy.target = atlas
            nextSpy.target = atlas
            previousSpy.target = atlas
            arcSpy.clear()
            nextSpy.clear()
            previousSpy.clear()
            tryCompare(atlas, "captureReady", true, 5000)
            mouseMove(host, 2, 2)
        }

        function cleanup() {
            arcSpy.target = null
            nextSpy.target = null
            previousSpy.target = null
            if (atlas) atlas.destroy()
            atlas = null
            wait(0)
        }

        function control(name) {
            var item = findChild(atlas, name)
            verify(item !== null, "Missing native control: " + name)
            return item
        }

        function rect(item) {
            var point = item.mapToItem(atlas, 0, 0)
            return { x: point.x, y: point.y, width: item.width, height: item.height }
        }

        function overlaps(a, b) {
            return a.x < b.x + b.width && a.x + a.width > b.x
                    && a.y < b.y + b.height && a.y + a.height > b.y
        }

        function test_unknown_ids_do_not_open_an_arc() {
            compare(atlas.focusArc("not-an-arc"), false)
            compare(atlas.openPreview("not-a-marker"), false)
            compare(atlas.previewVisible, false)
            compare(arcSpy.count, 0)
        }

        function test_focus_previews_but_only_open_arc_navigates() {
            verify(atlas.focusArc("arabasta"))
            tryCompare(atlas, "previewVisible", true)
            compare(atlas.selectedArcId, "arabasta")
            compare(arcSpy.count, 0)
            var open = control("paradiseOpenArc")
            open.forceActiveFocus()
            keyClick(Qt.Key_Return)
            compare(arcSpy.count, 1)
            compare(arcSpy.signalArguments[0][0].id, "arabasta")
            var payload = arcSpy.signalArguments[0][0]
            compare(payload.seaId, "paradise")
            compare(payload.seaLabel, "Paradise")
            verify(String(payload.markerSource).endsWith("/badges/arabasta.png"))
            verify(String(payload.heroSource).endsWith("/banners/arabasta.png"))
            compare(payload.onePacePrefixes[0], "AL_")
        }

        function test_local_art_loads_and_empty_art_uses_native_fallback() {
            for (var i = 0; i < atlas.markerEntries.length; ++i)
                tryCompare(control("paradiseBadge-" + atlas.markerEntries[i].id), "status", Image.Ready, 5000)
            verify(atlas.focusArc("post-enies-lobby"))
            var badge = control("paradiseBadge-water-7")
            var poster = control("paradiseArcPoster")
            tryCompare(badge, "status", Image.Ready, 5000)
            tryCompare(poster, "status", Image.Ready, 5000)
            verify(String(badge.source).endsWith("/badges/post-enies-lobby.png"))
            verify(String(poster.source).endsWith("/banners/post-enies-lobby.png"))
            compare(control("paradiseSealFallback-water-7").visible, false)
            compare(control("paradisePosterFallback").visible, false)
            badge.source = ""
            poster.source = ""
            tryCompare(badge, "status", Image.Null)
            tryCompare(poster, "status", Image.Null)
            compare(control("paradiseSealFallback-water-7").visible, true)
            compare(control("paradisePosterFallback").visible, true)
            compare(arcSpy.count, 0)
        }

        function test_badge_pointer_click_is_not_navigation() {
            var marker = atlas.markerItem("arabasta")
            verify(marker !== null)
            mouseMove(marker, marker.width / 2, marker.height / 2)
            tryCompare(atlas, "previewVisible", true)
            mouseClick(marker, marker.width / 2, marker.height / 2, Qt.LeftButton)
            compare(arcSpy.count, 0)
        }

        function test_hover_handoff_and_focus_loss_keep_the_explicit_open_contract() {
            var marker = atlas.markerItem("arabasta")
            mouseMove(marker, marker.width / 2, marker.height / 2)
            tryCompare(atlas, "previewVisible", true)
            // Allow scheduled placement to reach the frame before pointer handoff.
            wait(20)
            var card = control("paradiseArcPreview")
            mouseMove(card, card.width / 2, card.height / 2)
            wait(220)
            compare(atlas.previewVisible, true)
            compare(arcSpy.count, 0)
            mouseMove(host, 2, 2)
            control("paradiseIndexButton").forceActiveFocus()
            tryCompare(atlas, "previewVisible", false)
        }

        function test_return_visits_share_marker_but_keep_story_identity() {
            atlas.focusArc("water-7")
            var first = atlas.selectedMarkerId
            var badge = control("paradiseBadge-water-7")
            tryCompare(badge, "status", Image.Ready, 5000)
            verify(String(badge.source).endsWith("/badges/water-7.png"))
            atlas.focusArc("post-enies-lobby")
            compare(atlas.selectedMarkerId, first)
            compare(atlas.selectedArcId, "post-enies-lobby")
            tryCompare(badge, "status", Image.Ready, 5000)
            verify(String(badge.source).endsWith("/badges/post-enies-lobby.png"))
            atlas.focusArc("sabaody")
            first = atlas.selectedMarkerId
            badge = control("paradiseBadge-sabaody")
            tryCompare(badge, "status", Image.Ready, 5000)
            verify(String(badge.source).endsWith("/badges/sabaody.png"))
            atlas.focusArc("return-to-sabaody")
            compare(atlas.selectedMarkerId, first)
            compare(atlas.selectedArcId, "return-to-sabaody")
            tryCompare(badge, "status", Image.Ready, 5000)
            verify(String(badge.source).endsWith("/badges/return-to-sabaody.png"))
        }

        function test_keyboard_visit_switch_preserves_preview_focus() {
            verify(atlas.focusArc("water-7"))
            var visit = control("paradiseVisit-post-enies-lobby")
            visit.forceActiveFocus()
            keyClick(Qt.Key_Return)
            tryCompare(atlas, "selectedArcId", "post-enies-lobby")
            var open = control("paradiseOpenArc")
            tryCompare(open, "activeFocus", true)
            wait(220)
            compare(atlas.previewVisible, true)
            compare(arcSpy.count, 0)
            keyClick(Qt.Key_Return)
            compare(arcSpy.count, 1)
            compare(arcSpy.signalArguments[0][0].id, "post-enies-lobby")
        }

        function test_preview_text_scrolls_by_keyboard_when_constrained() {
            atlas.focusArc("arabasta")
            var card = control("paradiseArcPreview")
            card.height = 220
            var flick = control("paradisePreviewContent")
            var open = control("paradiseOpenArc")
            open.forceActiveFocus()
            tryVerify(function() { return flick.contentHeight > flick.height })
            keyClick(Qt.Key_PageDown)
            verify(flick.contentY > 0)
            keyClick(Qt.Key_PageUp)
            compare(flick.contentY, 0)
            compare(arcSpy.count, 0)
        }

        function test_index_entry_focuses_location_without_navigating() {
            mouseClick(control("paradiseIndexButton"))
            compare(atlas.indexVisible, true)
            tryVerify(function() { return findChild(atlas, "paradiseIndexRow-reverse-mountain") !== null })
            var row = control("paradiseIndexRow-reverse-mountain")
            row.forceActiveFocus()
            keyClick(Qt.Key_Return)
            tryCompare(atlas, "indexVisible", false)
            tryCompare(atlas, "selectedArcId", "reverse-mountain")
            compare(arcSpy.count, 0)
        }

        function test_escape_dismisses_preview_without_focus_reopening_it() {
            atlas.focusArc("drum-island")
            tryCompare(atlas, "previewVisible", true)
            control("paradiseOpenArc").forceActiveFocus()
            keyClick(Qt.Key_Escape)
            tryCompare(atlas, "previewVisible", false)
            wait(220)
            compare(atlas.previewVisible, false)
            compare(atlas.requestEscape(), false)
            mouseClick(control("paradiseIndexButton"))
            compare(atlas.requestEscape(), true)
            compare(atlas.indexVisible, false)
            compare(atlas.requestEscape(), false)
        }

        function test_directional_marker_focus_and_zoom_are_keyboard_reachable() {
            atlas.focusArc("reverse-mountain")
            var initial = atlas.selectedMarkerId
            keyClick(Qt.Key_Right)
            verify(atlas.selectedMarkerId !== initial)
            var zoomIn = control("paradiseZoomIn")
            zoomIn.forceActiveFocus()
            keyClick(Qt.Key_Return)
            compare(atlas.zoom, 1.25)
            atlas.setZoom(200)
            compare(atlas.zoom, 2)
            atlas.setZoom(-4)
            compare(atlas.zoom, 1)
        }

        function test_hiding_atlas_closes_transient_state() {
            atlas.focusArc("skypiea")
            atlas.visible = false
            compare(atlas.previewVisible, false)
            compare(atlas.indexVisible, false)
            atlas.visible = true
            compare(atlas.transientOpen, false)
        }

        function test_transition_buttons_emit_only_their_own_signal() {
            mouseClick(control("paradiseNewWorld"))
            compare(nextSpy.count, 1)
            compare(arcSpy.count, 0)
            mouseClick(control("paradiseEastBlue"))
            compare(previousSpy.count, 1)
            compare(arcSpy.count, 0)
        }

        function test_all_previews_remain_inside_viewport_data() {
            return [
                { tag: "1280x720", w: 1280, h: 720 },
                { tag: "1600x900", w: 1600, h: 900 },
                { tag: "1920x1080", w: 1920, h: 1080 }
            ]
        }

        function test_all_previews_remain_inside_viewport(data) {
            host.width = data.w
            host.height = data.h
            atlas.width = data.w
            atlas.height = data.h
            for (var i = 0; i < atlas.arcEntries.length; ++i) {
                verify(atlas.focusArc(atlas.arcEntries[i].id))
                wait(30)
                var card = control("paradiseArcPreview")
                verify(card.x >= 0 && card.y >= 0)
                verify(card.x + card.width <= atlas.width + 1)
                verify(card.y + card.height <= atlas.height + 1)
                var marker = atlas.markerItem(atlas.selectedMarkerId)
                verify(!overlaps(rect(card), rect(marker)), "Preview covers " + marker.markerId)
                var controls = ["paradiseIndexButton", "paradiseZoomTools", "paradiseEastBlue", "paradiseNewWorld"]
                for (var c = 0; c < controls.length; ++c)
                    verify(!overlaps(rect(card), rect(control(controls[c]))), "Preview covers " + controls[c])
            }
        }
    }
}
