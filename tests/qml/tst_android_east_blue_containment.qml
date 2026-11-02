import QtQuick
import QtQuick.Window
import QtTest
import "../../qml" as Colosseum
import "../../qml/OnePieceEastBlueAtlasData.js" as AtlasData

// WT-11: the production East Blue index and arc preview stay inside the atlas root at the
// supported Android desktop-presentation canvases (1024x640 / zoom) and at narrower/shorter
// conditional roots; long copy scrolls while OPEN ARC / CLOSE stay inside the preview.
TestCase {
    id: testCase
    name: "AndroidEastBlueContainment"
    when: windowShown

    Window { id: testWindow; width: 1280; height: 720; visible: true; color: "#101816" }
    Component { id: atlasComponent; Colosseum.OnePieceEastBlueAtlas {} }

    function find(root, name) {
        if (!root) return null
        if (root.objectName === name) return root
        const kids = root.children || []
        for (let i = 0; i < kids.length; ++i) { const f = find(kids[i], name); if (f) return f }
        return null
    }
    function inside(item, root, label) {
        const p = item.mapToItem(root, 0, 0)
        verify(p.x >= -0.5 && p.y >= -0.5 && p.x + item.width <= root.width + 0.5 && p.y + item.height <= root.height + 0.5,
               label + " inside root " + JSON.stringify([p.x, p.y, item.width, item.height, root.width, root.height]))
    }
    function within(item, outer, label) {
        const p = item.mapToItem(outer, 0, 0)
        verify(p.x >= -0.5 && p.y >= -0.5 && p.x + item.width <= outer.width + 0.5 && p.y + item.height <= outer.height + 0.5,
               label + " inside " + outer.objectName + " " + JSON.stringify([p.x, p.y, item.width, item.height, outer.width, outer.height]))
    }

    function test_index_and_preview_contained_data() {
        return [
            { tag: "zoom1.15", w: 1237, h: 556 },
            { tag: "zoom1.20", w: 1185, h: 533 },
            { tag: "min-desktop-presentation", w: 853, h: 533 },
            { tag: "conditional-354", w: 354, h: 640 },
            { tag: "short-300", w: 900, h: 300 }
        ]
    }
    function test_index_and_preview_contained(data) {
        testWindow.width = data.w; testWindow.height = data.h
        const atlas = createTemporaryObject(atlasComponent, testWindow.contentItem,
                                            { width: data.w, height: data.h, touchInput: true })
        verify(atlas !== null)
        waitForRendering(atlas)
        const markers = AtlasData.canonMarkers || []
        verify(markers.length > 0)
        // Try every canon arc; the longest copy is the stress case.
        let longest = markers[0].id, longestLen = -1
        for (let i = 0; i < markers.length; ++i) {
            atlas.openPreview(markers[i].id)
            const len = String(atlas.selectedArc ? (atlas.selectedArc.title + atlas.selectedArc.summary) : "").length
            if (len > longestLen) { longestLen = len; longest = markers[i].id }
        }
        atlas.closePreview()
        atlas.openPreview(longest)
        wait(80)
        const preview = find(atlas, "eastBlueArcPreview")
        verify(preview && preview.visible, "preview opened")
        inside(preview, atlas, "preview")
        within(find(atlas, "eastBlueOpenArc"), preview, "OPEN ARC")
        within(find(atlas, "eastBlueClosePreview"), preview, "CLOSE")
        const copy = find(atlas, "eastBlueArcPreviewCopy")
        verify(copy.height > 0)
        if (copy.interactive) {
            copy.contentY = copy.contentHeight - copy.height
            verify(copy.contentY > 0, "long copy scrolls")
        }
        atlas.closePreview()
        if (atlas.indexVisible !== undefined) {
            atlas.indexVisible = true
            wait(30)
            const index = find(atlas, "eastBlueIndexPanel")
            verify(index.visible)
            inside(index, atlas, "index")
            atlas.indexVisible = false
        }
    }
}
