import QtQuick
import QtQuick.Window
import QtTest
import "../../qml" as App

TestCase {
    id: testCase
    name: "PhoneSetupInsets"
    when: windowShown

    Window {
        id: testWindow
        width: 792
        height: 354
        visible: true
        property bool androidHost: true
    }

    Component { id: sheetComponent; App.ExtensionsSetupSheetAndroid {} }
    QtObject {
        id: fakeInputMethod
        property bool visible: false
        property rect keyboardRectangle: Qt.rect(0, 0, 0, 0)
        property int hides: 0
        function hide() { visible = false; hides++ }
    }
    SignalSpy { id: closeSpy; signalName: "closeRequested" }

    function byName(root, name) {
        if (root.objectName === name) return root
        if(root["activePhoneSheet"] && root.activePhoneSheet.visible) {
            if(name==="extensionsSetupScroll")return root.activePhoneSheet.bodyViewport
            const found=byName(root.activePhoneSheet.contentItem,name)
            if(found)return found
        }
        const children = root.children || []
        for (let i = 0; i < children.length; ++i) {
            const found = byName(children[i], name)
            if (found) return found
        }
        return null
    }

    function cleanup() {
        fakeInputMethod.visible = false
        fakeInputMethod.keyboardRectangle = Qt.rect(0, 0, 0, 0)
        closeSpy.target = null
    }

    function findPanel(root) {
        verify(root.activePhoneSheet && root.activePhoneSheet.visible,"the shared sheet must be the visible surface")
        if(root.activePhoneSheet && root.activePhoneSheet.visible)return root.activePhoneSheet.contentItem.parent
        const children = root.children || []
        for (let i = 0; i < children.length; ++i) {
            const child = children[i]
            if (child.objectName === "extensionsSetupPanel"
                    || (child.radius === 24 && String(child.color) === "#0e1016"))
                return child
        }
        return null
    }

    function test_long_status_contained_data() {
        return [
            {tag: "portrait", w: 354, h: 792},
            {tag: "landscape", w: 792, h: 354},
            {tag: "short_split", w: 354, h: 480}
        ]
    }

    function test_long_status_contained(data) {
        testWindow.width = data.w
        testWindow.height = data.h
        const sheet = createTemporaryObject(sheetComponent, testWindow.contentItem, {
            width: data.w, height: data.h,
            addon: {name: "Torrentio", manifestUrl: "https://example.invalid/manifest.json"}
        })
        verify(sheet !== null)
        sheet.status = Array(30).join("The local setup message must remain readable and every action reachable. ")
        wait(30)
        const panel = findPanel(sheet)
        verify(panel !== null)
        verify(panel.y >= 0, "Panel top must stay inside the window: " + panel.y)
        verify(panel.y + panel.height <= data.h,
               "Panel bottom must stay inside " + data.h + ": " + (panel.y + panel.height))
        verify(panel.x >= 0 && panel.x + panel.width <= data.w)
    }

    function test_keyboard_avoidance_data() {
        return [
            {tag: "docked", w: 354, h: 792, keyboard: Qt.rect(0, 450, 354, 342)},
            {tag: "floating_right", w: 792, h: 354, keyboard: Qt.rect(494, 68, 246, 230)},
            {tag: "floating_center", w: 354, h: 792, keyboard: Qt.rect(60, 250, 220, 260)},
            {tag: "unknown_landscape", w: 792, h: 354, keyboard: Qt.rect(0, 0, 0, 0)},
            {tag: "unknown_portrait", w: 354, h: 792, keyboard: Qt.rect(0, 0, 0, 0)}
        ]
    }

    function test_keyboard_avoidance(data) {
        testWindow.width = data.w; testWindow.height = data.h
        testWindow.requestActivate()
        const sheet = createTemporaryObject(sheetComponent, testWindow.contentItem, {
            width: data.w, height: data.h, inputMethod: fakeInputMethod,
            addon: {name: "Torrentio", manifestUrl: "https://example.invalid/manifest.json"}
        })
        verify(sheet !== null)
        wait(20)
        fakeInputMethod.keyboardRectangle = data.keyboard
        fakeInputMethod.visible = true
        wait(20)
        const panel = findPanel(sheet)
        verify(panel.x >= 0 && panel.y >= 0)
        verify(panel.x + panel.width <= data.w && panel.y + panel.height <= data.h)
        if (data.keyboard.width > 0 && data.keyboard.height > 0) {
            const k = data.keyboard
            verify(panel.x + panel.width <= k.x || panel.x >= k.x + k.width
                   || panel.y + panel.height <= k.y || panel.y >= k.y + k.height,
                   "Panel must avoid the reported floating/docked keyboard")
        }
        const field = byName(sheet, "extensionsSetupLink")
        const scroll = byName(sheet, "extensionsSetupScroll")
        verify(field !== null && scroll !== null)
        field.forceActiveFocus()
        wait(30)
        let point = field.mapToItem(scroll, 0, 0)
        verify(point.y >= -0.5 && point.y + field.height <= scroll.height + 0.5,
               "Focused input must be revealed: y=" + point.y + " h=" + field.height
               + " view=" + scroll.height + " contentY=" + scroll.contentY
               + " focus=" + testWindow.activeFocusItem)
        const install = byName(sheet, "extensionsSetupInstall")
        install.forceActiveFocus()
        wait(30)
        point = install.mapToItem(scroll, 0, 0)
        verify(point.y >= -0.5 && point.y + install.height <= scroll.height + 0.5,
               "Lower action must be reachable: y=" + point.y + " h=" + install.height
               + " view=" + scroll.height + " contentY=" + scroll.contentY
               + " focus=" + testWindow.activeFocusItem)
        verify(fakeInputMethod.visible)
    }

    function test_back_hides_keyboard_before_sheet() {
        testWindow.width = 792; testWindow.height = 354
        const sheet = createTemporaryObject(sheetComponent, testWindow.contentItem, {
            width: 792, height: 354, inputMethod: fakeInputMethod,
            addon: {name: "Torrentio", manifestUrl: "https://example.invalid/manifest.json"}
        })
        verify(sheet !== null)
        wait(20)
        closeSpy.target = sheet; closeSpy.clear()
        fakeInputMethod.visible = true
        verify(sheet.handleBack())
        verify(!fakeInputMethod.visible)
        compare(closeSpy.count, 0)
        verify(sheet.handleBack())
        compare(closeSpy.count, 1)
    }
}
