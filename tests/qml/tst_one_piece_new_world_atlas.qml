import QtQuick
import QtQuick.Window
import QtTest
import "../../qml" as NewWorld
import "../../qml/OnePieceNewWorldAtlasData.js" as ChartData

TestCase {
    id: suite
    name: "NewWorldNativeAtlas"
    when: windowShown
    property var atlas: null

    Window { id: testWindow; width: 1280; height: 720; visible: true; color: "#151b18" }
    Component { id: atlasComponent; NewWorld.OnePieceNewWorldAtlas { reducedMotion: true } }
    SignalSpy { id: arcSpy; signalName: "arcRequested" }
    SignalSpy { id: previousSpy; signalName: "paradiseRequested" }
    SignalSpy { id: nextSpy; signalName: "beyondWanoRequested" }

    function init() {
        failOnWarning(/ReferenceError|TypeError|Binding loop detected/)
        testWindow.requestActivate()
        atlas = atlasComponent.createObject(testWindow.contentItem, { width: 1280, height: 720 })
        verify(atlas !== null)
        arcSpy.target = atlas
        previousSpy.target = atlas
        nextSpy.target = atlas
        arcSpy.clear(); previousSpy.clear(); nextSpy.clear()
        tryCompare(atlas, "captureReady", true, 5000)
        mouseMove(testWindow.contentItem, 2, 2)
        atlas.closePreview(false)
        wait(20)
    }
    function cleanup() {
        arcSpy.target = null; previousSpy.target = null; nextSpy.target = null
        if (atlas) atlas.destroy()
        atlas = null
        wait(0)
    }
    function control(name) {
        var result = findChild(atlas, name)
        verify(result !== null, "Missing control: " + name)
        return result
    }
    function rect(item) {
        var p = item.mapToItem(atlas, 0, 0)
        return { x: p.x, y: p.y, width: item.width, height: item.height }
    }
    function overlaps(a, b) {
        return a.x < b.x + b.width && a.x + a.width > b.x
                && a.y < b.y + b.height && a.y + a.height > b.y
    }
    function moveTo(item) {
        var p = item.mapToItem(testWindow.contentItem, item.width / 2, item.height / 2)
        mouseMove(testWindow.contentItem, p.x, p.y)
    }
    function click(item) {
        var p = item.mapToItem(testWindow.contentItem, item.width / 2, item.height / 2)
        mouseClick(testWindow.contentItem, p.x, p.y, Qt.LeftButton)
    }

    function test_unknown_and_contextual_ids_cannot_open_an_arc() {
        compare(atlas.focusArc("missing"), false)
        compare(atlas.focusArc("levely"), false)
        compare(atlas.focusLocation("mary-geoise"), false)
        compare(atlas.openPreview("missing"), false)
        compare(atlas.previewVisible, false)
        compare(arcSpy.count, 0)
        var contextBadge = control("newWorldReverieContextBadge")
        tryCompare(contextBadge, "status", Image.Ready, 5000)
        verify(String(contextBadge.source).endsWith("/badges/reverie.png"))
        compare(contextBadge.activeFocusOnTab, false)
        compare(atlas.markerEntries.length, 6)
    }
    function test_wrong_arc_for_a_real_marker_does_not_replace_selection() {
        verify(atlas.focusArc("dressrosa"))
        compare(atlas.openPreview("dressrosa", "wano-country"), false)
        compare(atlas.selectedArcId, "dressrosa")
        compare(arcSpy.count, 0)
    }
    function test_pointer_seal_is_inert_and_hover_handoff_retains_preview() {
        var seal = atlas.markerItem("punk-hazard")
        verify(seal !== null)
        moveTo(seal)
        tryCompare(atlas, "previewVisible", true)
        click(seal)
        compare(arcSpy.count, 0)
        // Preview placement is deferred until its selected marker has been laid out.
        wait(20)
        var preview = control("newWorldArcPreview")
        moveTo(preview)
        wait(220)
        compare(atlas.previewVisible, true)
        click(control("newWorldOpenArc"))
        compare(arcSpy.count, 1)
        compare(arcSpy.signalArguments[0][0].id, "punk-hazard")
    }
    function test_keyboard_seal_previews_then_explicit_open_emits_once() {
        var seal = atlas.markerItem("zou")
        seal.forceActiveFocus()
        tryCompare(atlas, "selectedArcId", "zou")
        compare(arcSpy.count, 0)
        keyClick(Qt.Key_Return)
        var open = control("newWorldOpenArc")
        tryCompare(open, "activeFocus", true)
        compare(arcSpy.count, 0)
        keyClick(Qt.Key_Return)
        compare(arcSpy.count, 1)
        compare(arcSpy.signalArguments[0][0].id, "zou")
    }
    function test_local_art_and_catalogue_payload_preserve_story_records() {
        for (var i = 0; i < atlas.arcEntries.length; ++i) {
            var entry = atlas.arcEntries[i]
            verify(atlas.focusArc(entry.id))
            tryCompare(control("newWorldBadge-" + entry.id), "status", Image.Ready, 5000)
            tryCompare(control("newWorldPreviewHero"), "status", Image.Ready, 5000)
            compare(arcSpy.count, i)
            atlas.openSelectedArc()
            compare(arcSpy.count, i + 1)
            var payload = arcSpy.signalArguments[i][0]
            compare(payload.id, entry.id)
            compare(payload.seaId, "new-world")
            compare(payload.anime, entry.anime)
            compare(payload.onePacePrefixes.length, 1)
            verify(payload.heroSource.indexOf("/new-world/banners/") >= 0)
            payload.onePacePrefixes.push("test-only")
            compare(entry.onePacePrefixes.length, 1)
        }
    }
    function test_arrow_keys_follow_geographic_neighbors() {
        verify(atlas.focusArc("fish-man-island"))
        var before = atlas.selectedMarkerId
        keyClick(Qt.Key_Right)
        verify(atlas.selectedMarkerId !== before)
        verify(atlas.markerItem(atlas.selectedMarkerId) !== null)
        compare(arcSpy.count, 0)
    }
    function test_primary_labels_clear_landmarks_data() {
        return [{tag: "980", width: 980, height: 720},
                {tag: "1280", width: 1280, height: 720},
                {tag: "1920", width: 1920, height: 1080}]
    }
    function test_primary_labels_clear_landmarks(data) {
        atlas.width = data.width; atlas.height = data.height
        wait(30)
        var places = ChartData.canonMarkers.map(function(marker) { return marker.locationId })
        places.push("onigashima")
        for (var id of places) {
            var label = control("newWorldChartLabel-" + id)
            verify(label.visible, id + " remains labelled in the full-chart view")
            var top = label.mapToItem(atlas, (label.width - label.paintedWidth) / 2, 0)
            var bottom = label.mapToItem(atlas, (label.width + label.paintedWidth) / 2, label.paintedHeight)
            var ink = {x: top.x, y: top.y, width: bottom.x - top.x, height: bottom.y - top.y}
            verify(ink.x >= 0 && ink.y >= 0 && bottom.x <= atlas.width && bottom.y <= atlas.height)
            for (var place of ChartData.locations) {
                var landmark = control("newWorldLandform-" + place.id)
                if (!landmark.visible) continue
                var a = landmark.mapToItem(atlas, 0, 0)
                var b = landmark.mapToItem(atlas, landmark.width, landmark.height)
                verify(!overlaps(ink, {x: a.x, y: a.y, width: b.x - a.x, height: b.y - a.y}),
                       id + " label stays clear of " + place.id)
            }
        }
    }
    function test_supporting_locations_resolve_to_one_story_seal() {
        verify(atlas.focusLocation("wano"))
        var same = atlas.selectedMarkerId
        verify(atlas.focusLocation("onigashima"))
        compare(atlas.selectedMarkerId, same)
        compare(atlas.selectedArcId, "wano-country")
        verify(atlas.focusLocation("cacao"))
        compare(atlas.selectedArcId, "whole-cake-island")
        compare(arcSpy.count, 0)
    }
    function test_escape_restores_focus_without_reopening_preview() {
        atlas.focusArc("dressrosa")
        control("newWorldOpenArc").forceActiveFocus()
        keyClick(Qt.Key_Escape)
        tryCompare(atlas, "previewVisible", false)
        wait(220)
        compare(atlas.previewVisible, false)
        compare(atlas.requestEscape(), false)
        click(control("newWorldIndexButton"))
        compare(atlas.indexVisible, true)
        compare(atlas.requestEscape(), true)
        compare(atlas.indexVisible, false)
        compare(atlas.requestEscape(), false)
    }
    function test_index_keeps_six_voyage_entries_and_keyboard_selection_is_not_navigation() {
        click(control("newWorldIndexButton"))
        compare(atlas.indexVisible, true)
        var list = control("newWorldIndexList")
        compare(list.count, 6)
        tryVerify(function() { return findChild(atlas, "newWorldIndexRow-fish-man-island") !== null })
        var first = control("newWorldIndexRow-fish-man-island")
        first.forceActiveFocus()
        keyClick(Qt.Key_End)
        tryCompare(list, "currentIndex", 5)
        keyClick(Qt.Key_Return)
        tryCompare(atlas, "selectedArcId", "wano-country")
        compare(atlas.indexVisible, false)
        compare(arcSpy.count, 0)
    }
    function test_preview_text_is_keyboard_scrollable_at_constrained_height() {
        atlas.focusArc("wano-country")
        var card = control("newWorldArcPreview")
        card.height = 220
        var textArea = control("newWorldPreviewContent")
        control("newWorldOpenArc").forceActiveFocus()
        tryVerify(function() { return textArea.contentHeight > textArea.height })
        keyClick(Qt.Key_PageDown)
        verify(textArea.contentY > 0)
        keyClick(Qt.Key_PageUp)
        fuzzyCompare(textArea.contentY, 0, 1)
    }
    function test_zoom_is_bounded_and_offscreen_keyboard_targets_are_revealed() {
        control("newWorldZoomIn").forceActiveFocus()
        keyClick(Qt.Key_Return)
        compare(atlas.zoom, 1.25)
        atlas.setZoom(9)
        compare(atlas.zoom, 2)
        wait(20)
        atlas.focusArc("wano-country")
        var viewport = control("newWorldMapViewport")
        var seal = rect(atlas.markerItem("wano-country"))
        verify(seal.x >= viewport.x - 1 && seal.x + seal.width <= viewport.x + viewport.width + 1)
        verify(seal.y >= viewport.y - 1 && seal.y + seal.height <= viewport.y + viewport.height + 1)
        atlas.setZoom(-2)
        compare(atlas.zoom, 1)
        tryCompare(viewport, "contentX", 0)
        tryCompare(viewport, "contentY", 0)
    }
    function test_hiding_atlas_closes_transient_state() {
        atlas.focusArc("whole-cake-island")
        atlas.visible = false
        compare(atlas.previewVisible, false)
        compare(atlas.indexVisible, false)
        atlas.visible = true
        wait(30)
        compare(atlas.transientOpen, false)
    }
    function test_transition_requests_do_not_open_fake_pages() {
        click(control("newWorldToParadise"))
        compare(previousSpy.count, 1)
        compare(nextSpy.count, 0)
        click(control("newWorldBeyondWano"))
        compare(nextSpy.count, 1)
        compare(arcSpy.count, 0)
        compare(atlas.transientOpen, false)
    }
    function test_previews_and_native_controls_clear_each_other_data() {
        return [{ tag: "1280x720", w: 1280, h: 720 },
                { tag: "1600x900", w: 1600, h: 900 },
                { tag: "1920x1080", w: 1920, h: 1080 }]
    }
    function test_previews_and_native_controls_clear_each_other(data) {
        // Exercise actual QML layout sizes, not a claim about physical monitor size.
        atlas.width = data.w; atlas.height = data.h
        for (var i = 0; i < atlas.arcEntries.length; ++i) {
            atlas.focusArc(atlas.arcEntries[i].id)
            wait(30)
            var card = rect(control("newWorldArcPreview"))
            var seal = rect(atlas.markerItem(atlas.selectedMarkerId))
            verify(card.x >= 0 && card.y >= 0)
            verify(card.x + card.width <= atlas.width + 1 && card.y + card.height <= atlas.height + 1)
            verify(!overlaps(card, seal), atlas.selectedArcId + " covers its seal")
            var names = ["newWorldIndexButton", "newWorldZoomTools", "newWorldToParadise", "newWorldBeyondWano"]
            for (var n = 0; n < names.length; ++n)
                verify(!overlaps(card, rect(control(names[n]))), atlas.selectedArcId + " covers " + names[n])
        }
    }
}
