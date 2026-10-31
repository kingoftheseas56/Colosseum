import QtQuick
import QtTest
import "../../qml" as App

TestCase {
    id: tests
    name: "PhoneAtlasPreviews"
    when: windowShown

    Window {
        id: host
        width: 1280
        height: 720
        visible: true
    }

    Component {
        id: eastBlueComponent
        App.OnePieceEastBlueAtlas { reducedMotion: true; androidHost: true }
    }

    property var atlas: null

    function init() {
        host.width = 1280
        host.height = 720
        atlas = eastBlueComponent.createObject(host.contentItem,
            { width: host.width, height: host.height })
        verify(atlas !== null, "the actual East Blue atlas instantiates without native services")
        wait(0)
        atlas.closePreview()
    }

    function cleanup() {
        if (atlas)
            atlas.destroy()
        atlas = null
        wait(0)
    }

    function findChild(parent, objectName) {
        if (!parent)
            return null
        if (parent.objectName === objectName)
            return parent
        const children = parent.children || []
        for (let i = 0; i < children.length; ++i) {
            const result = findChild(children[i], objectName)
            if (result)
                return result
        }
        return null
    }

    function rectIn(item, target) {
        const point = item.mapToItem(target, 0, 0)
        return { x: point.x, y: point.y, width: item.width, height: item.height }
    }

    function touchItem(item) {
        const root = host.contentItem
        const point = item.mapToItem(root, item.width / 2, item.height / 2)
        touchEvent(root).press(0, root, point.x, point.y).commit()
        wait(20)
        touchEvent(root).release(0, root, point.x, point.y).commit()
        wait(30)
    }

    function assertEastBluePreview(width, height) {
        const phone = Math.min(width, height) < 600
        host.width = width
        host.height = height
        atlas.width = width
        atlas.height = height
        atlas.androidHost = phone
        wait(20)
        compare(atlas.phoneLayout, phone,
                "the atlas must use the Android-host phone gate at " + width + "x" + height)
        atlas.openPreview("baratie")
        tryCompare(atlas, "previewVisible", true)
        wait(30)

        const preview = findChild(atlas, "eastBlueArcPreview")
        const open = findChild(atlas, "eastBlueOpenArc")
        const close = findChild(atlas, "eastBlueClosePreview")
        verify(preview !== null && open !== null && close !== null,
               "preview and both real actions exist at " + width + "x" + height)
        verify(preview.visible && open.visible && close.visible)

        const previewRect = rectIn(preview, atlas)
        verify(previewRect.x >= 0 && previewRect.y >= 0
               && previewRect.x + previewRect.width <= atlas.width + 1
               && previewRect.y + previewRect.height <= atlas.height + 1,
               "preview stays inside the atlas at " + width + "x" + height)

        const openRect = rectIn(open, preview)
        const closeRect = rectIn(close, preview)
        const actionRight = Math.max(openRect.x + openRect.width,
                                     closeRect.x + closeRect.width)
        const actionBottom = Math.max(openRect.y + openRect.height,
                                      closeRect.y + closeRect.height)
        verify(openRect.x >= 0 && closeRect.x >= 0
               && openRect.y >= 0 && closeRect.y >= 0
               && actionRight <= preview.width + 1 && actionBottom <= preview.height + 1,
               "Open Arc and Close fit preview width " + preview.width + " at "
               + width + "x" + height + " (actions right " + actionRight
               + "px, bottom " + actionBottom + "px)")
        const targetFloor = phone ? 48 : 44
        verify(open.width >= targetFloor && open.height >= targetFloor
               && close.width >= targetFloor && close.height >= targetFloor,
               "both preview actions retain " + targetFloor + "dp hit targets")
        if (phone) {
            const body = findChild(atlas, "eastBluePreviewContent")
            verify(body !== null, "phone preview body is a named clipped viewport")
            compare(body.clip, true)
            const bodyRect = rectIn(body, preview)
            verify(bodyRect.x >= 0 && bodyRect.y >= 0
                   && bodyRect.x + bodyRect.width <= preview.width + 1
                   && bodyRect.y + bodyRect.height <= preview.height + 1,
                   "phone preview body remains inside its card")
            compare(open.width, close.width,
                    "phone footer actions have equal widths")
            verify(bodyRect.y + bodyRect.height <= Math.min(openRect.y, closeRect.y) + 1,
                   "phone body viewport stops above the pinned action footer")
        }

        let openCount = 0
        const opened = function() { ++openCount }
        atlas.arcRequested.connect(opened)
        touchItem(open)
        compare(openCount, 1, "Open Arc emits once at " + width + "x" + height)
        compare(atlas.previewVisible, false, "Open Arc closes the preview")
        atlas.arcRequested.disconnect(opened)

        atlas.openPreview("baratie")
        tryCompare(atlas, "previewVisible", true)
        wait(30)
        const reopenedClose = findChild(atlas, "eastBlueClosePreview")
        verify(reopenedClose !== null && reopenedClose.visible)
        touchItem(reopenedClose)
        compare(atlas.previewVisible, false, "Close dismisses the preview")
        wait(20)
        compare(atlas.previewVisible, false, "Close does not immediately reopen the preview")
    }

    function test_phone_portrait_and_landscape_preview_bounds_and_actions() {
        assertEastBluePreview(354, 792)
        assertEastBluePreview(792, 354)
    }

    function test_desktop_preview_keeps_wide_card_shape_and_actions() {
        assertEastBluePreview(1280, 720)
        const preview = findChild(atlas, "eastBlueArcPreview")
        fuzzyCompare(preview.width, Math.min(500, Math.max(360, atlas.width * 0.38)), 0.1)
        const open = findChild(atlas, "eastBlueOpenArc")
        const close = findChild(atlas, "eastBlueClosePreview")
        fuzzyCompare(rectIn(open, preview).x, 161, 0.1)
        fuzzyCompare(rectIn(close, preview).x, 273, 0.1)
        compare(open.width, 104)
        compare(close.width, 80)
    }
}
