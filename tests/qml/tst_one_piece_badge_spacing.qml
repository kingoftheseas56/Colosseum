import QtQuick
import QtQuick.Window
import QtTest
import "../../qml" as App
import "../../qml/OnePieceEastBlueAtlasData.js" as EastBlue

TestCase {
    id: suite
    name: "OnePieceBadgeSpacing"
    when: windowShown
    Window { id: stage; width: 1280; height: 720; visible: true }
    Component { id: eastBlue; App.OnePieceEastBlueAtlas {} }
    Component { id: paradise; App.OnePieceParadiseAtlas {} }
    Component { id: newWorld; App.OnePieceNewWorldAtlas {} }
    Component { id: finalSea; App.OnePieceFinalSeaAtlas {} }

    function findItem(root, name) {
        if (root.objectName === name) return root
        for (var child of root.children) {
            var found = findItem(child, name)
            if (found) return found
        }
        return null
    }

    function test_spacious_badges_data() {
        var cases = [], maps = [eastBlue, paradise, newWorld, finalSea]
        var controls = [
            ["eastBlueToParadise", "eastBlueIndexButton", "eastBlueZoomTools"],
            ["paradiseEastBlue", "paradiseNewWorld", "paradiseIndexButton", "paradiseZoomTools"],
            ["newWorldToParadise", "newWorldBeyondWano", "newWorldIndexButton", "newWorldZoomTools"],
            ["finalSeaToNewWorld", "finalSeaIndexButton", "finalSeaZoomTools"]
        ]
        for (var i = 0; i < maps.length; ++i)
            for (var width of [980, 1280, 1920])
                cases.push({tag: i + "-" + width, component: maps[i], east: i === 0, width: width, controls: controls[i]})
        return cases
    }
    function test_spacious_badges(data) {
        var atlas = data.component.createObject(stage.contentItem,
            {width: data.width, height: data.width === 1920 ? 1080 : 720, reducedMotion: true})
        verify(atlas !== null)
        try {
            tryCompare(atlas, "captureReady", true, 5000)
            wait(100)
            var markers = data.east ? EastBlue.canonMarkers : atlas.markerEntries
            var diameter = data.east ? atlas.badgeDiameter : atlas.sealDiameter
            for (var i = 0; i < markers.length; ++i) {
                var a = data.east ? atlas.markerForTest(markers[i].id) : atlas.markerItem(markers[i].id)
                verify(a !== null && a.visible, markers[i].id + " remains on the map")
                var centerA = a.mapToItem(atlas, a.width / 2, a.height / 2)
                for (var name of data.controls) {
                    var control = findItem(atlas, name)
                    verify(control !== null, name + " exists")
                    var corner = control.mapToItem(atlas, 0, 0)
                    verify(centerA.x + diameter / 2 <= corner.x
                        || centerA.x - diameter / 2 >= corner.x + control.width
                        || centerA.y + diameter / 2 <= corner.y
                        || centerA.y - diameter / 2 >= corner.y + control.height,
                        markers[i].id + " is clear of " + name)
                }
                for (var j = i + 1; j < markers.length; ++j) {
                    var b = data.east ? atlas.markerForTest(markers[j].id) : atlas.markerItem(markers[j].id)
                    var centerB = b.mapToItem(atlas, b.width / 2, b.height / 2)
                    var dx = centerA.x - centerB.x, dy = centerA.y - centerB.y
                    verify(Math.sqrt(dx * dx + dy * dy) >= diameter * 1.5,
                        markers[i].id + " / " + markers[j].id + " keep clear space between badges")
                }
                if (!data.east) {
                    var placement = atlas.badgePlacements[markers[i].id]
                    var shiftX = (placement.x - markers[i].x) * 1680
                    var shiftY = (placement.y - markers[i].y) * 960
                    verify(Math.sqrt(shiftX * shiftX + shiftY * shiftY) <= 129,
                        markers[i].id + " stays near its island-side anchor")
                }
            }
            if (data.width === 1280) {
                var captured = false
                atlas.grabToImage(function(result) {
                    captured = result.saveToFile(Qt.resolvedUrl("../../output/badge-spacing-" + (data.east ? "east-blue" : markers[0].id) + ".png"))
                })
                tryVerify(function() { return captured }, 3000)
            }
        } finally { atlas.destroy(); wait(0) }
    }
}
