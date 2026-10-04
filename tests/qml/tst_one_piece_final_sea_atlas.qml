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

    TestCase {
        name: "FinalSeaNativeAtlas"
        when: windowShown
        property var atlas: null

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
