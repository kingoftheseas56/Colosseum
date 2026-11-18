import QtQuick
import QtTest
import "../../../qml" as App

TestCase {
    id: testCase
    name: "VaultStorageSheet"
    width: 1280
    height: 720
    visible: true
    when: windowShown

    property color backingColor: "#ffffff"
    App.Theme { id: theme }
    Rectangle {
        id: testBackdrop
        objectName: "vaultStorageTestBackdrop"
        anchors.fill: parent
        color: testCase.backingColor
        z: -1
    }

    Component {
        id: adapterComponent
        QtObject {
            property var roots: []
            property bool busy: false
            property var patterns: ["*.tmp"]
            property var calls: []
            function scanIgnore() { return patterns }
            function setScanIgnore(next) { patterns = next }
            function rescan(path) { calls.push({action: "rescan", path: path}) }
            function forgetRoot(path) { calls.push({action: "forget", path: path}) }
        }
    }
    Component {
        id: sheetComponent
        App.VaultStorageSheet {
            anchors.fill: parent
            backdrop: testBackdrop
        }
    }
    SignalSpy { id: addSpy; signalName: "addFolderRequested" }
    SignalSpy { id: connectSpy; signalName: "connectServerRequested" }

    function makeAdapter(roots) {
        const adapter = createTemporaryObject(adapterComponent, testCase)
        adapter.roots = roots || []
        return adapter
    }
    function makeSheet(adapter) {
        return createTemporaryObject(sheetComponent, testCase, {adapter: adapter})
    }
    function linear(channel) {
        return channel <= 0.04045 ? channel / 12.92 : Math.pow((channel + 0.055) / 1.055, 2.4)
    }
    function luminance(color) {
        return 0.2126 * linear(color.r) + 0.7152 * linear(color.g) + 0.0722 * linear(color.b)
    }
    function over(foreground, background) {
        const alpha = foreground.a
        return Qt.rgba(foreground.r * alpha + background.r * (1 - alpha),
                       foreground.g * alpha + background.g * (1 - alpha),
                       foreground.b * alpha + background.b * (1 - alpha), 1)
    }
    function contrast(foreground, background) {
        const a = luminance(foreground)
        const b = luminance(background)
        return (Math.max(a, b) + 0.05) / (Math.min(a, b) + 0.05)
    }
    function maxPixelDelta(first, second) {
        const r = Math.abs(((first >>> 16) & 255) - ((second >>> 16) & 255))
        const g = Math.abs(((first >>> 8) & 255) - ((second >>> 8) & 255))
        const b = Math.abs((first & 255) - (second & 255))
        return Math.max(r, g, b)
    }
    function cleanup() {
        addSpy.target = null
        addSpy.clear()
        connectSpy.target = null
        connectSpy.clear()
    }

    function test_panelReadabilityOverBrightAndDarkBacking() {
        const longPath = "C:/fixture/library/this/path/has/many/segments/and/a/descriptive/name/that/is/much/longer/than/the/sources/drawer/row/can/show/all-at-once/Barry-Lyndon"
        const adapter = makeAdapter([{name: "Art", path: longPath, subtitle: "10 files", state: "Watching"}])
        const sheet = makeSheet(adapter)
        verify(sheet !== null)
        sheet.show(null)
        wait(260)

        const panel = findChild(sheet, "vaultSourcesPanel")
        const surface = findChild(panel, "vaultSourcesReadabilitySurface")
        const title = findChild(sheet, "vaultSourcesTitle")
        const subtitle = findChild(sheet, "vaultSourcesSubtitle")
        const pathText = findChild(sheet, "vaultSourcesPathText")
        const statusText = findChild(sheet, "vaultSourcesStatusText")
        verify(panel !== null && surface !== null && title !== null && subtitle !== null)
        verify(pathText !== null && statusText !== null)
        verify(panel.x >= 0 && panel.x + panel.width <= 1280)
        verify(panel.y + panel.height <= 720)
        // QColor stores the requested 0.98 alpha at limited precision; allow the
        // nearest representable value while still enforcing the intended opaque backing.
        verify(surface.color.a > 0.97)
        verify(Math.abs(surface.color.r - theme.vaultSurface.r) < 0.001)
        compare(title.color, theme.ink)
        compare(subtitle.color, theme.inkDimmer)
        compare(pathText.color, theme.inkDim)
        compare(statusText.color, theme.inkDim)
        compare(pathText.text, longPath)
        compare(pathText.elide, Text.ElideMiddle)
        verify(pathText.implicitWidth > pathText.width)
        verify(pathText.truncated)

        const white = Qt.rgba(1, 1, 1, 1)
        const black = Qt.rgba(0, 0, 0, 1)
        const brightPanel = over(surface.color, white)
        const darkPanel = over(surface.color, black)
        const brightRow = over(theme.glassTint, brightPanel)
        const darkRow = over(theme.glassTint, darkPanel)
        verify(contrast(title.color, brightPanel) >= 7)
        verify(contrast(subtitle.color, brightPanel) >= 4.5)
        verify(contrast(pathText.color, brightRow) >= 4.5)
        verify(contrast(statusText.color, brightRow) >= 4.5)
        verify(contrast(title.color, darkPanel) >= 7)
        verify(contrast(subtitle.color, darkPanel) >= 4.5)
        verify(contrast(pathText.color, darkRow) >= 4.5)
        verify(contrast(statusText.color, darkRow) >= 4.5)

        backingColor = "#ffffff"
        waitForRendering(testBackdrop)
        waitForRendering(sheet)
        const sample = panel.mapToItem(testCase, 10, 220)
        const brightPixel = grabImage(testCase).pixel(Math.round(sample.x), Math.round(sample.y))
        backingColor = "#050609"
        waitForRendering(testBackdrop)
        waitForRendering(sheet)
        const darkPixel = grabImage(testCase).pixel(Math.round(sample.x), Math.round(sample.y))
        verify(maxPixelDelta(brightPixel, darkPixel) <= 10)
        sheet.close()
    }

    function test_buttonsStatesCallbacksAndFooterAt720p() {
        const roots = []
        for (let i = 0; i < 18; ++i)
            roots.push({name: "Folder " + i, path: "C:/fixture/media/folder-" + i, subtitle: "10 files", state: "Watching"})
        const adapter = makeAdapter(roots)
        const sheet = makeSheet(adapter)
        verify(sheet !== null)
        sheet.show(null)
        wait(260)

        const panel = findChild(sheet, "vaultSourcesPanel")
        const folders = findChild(sheet, "vaultSourcesFolderList")
        const footer = findChild(sheet, "vaultEditIgnorePatterns")
        const rescan = findChild(sheet, "vaultSourcesRescan")
        const forget = findChild(sheet, "vaultSourcesForget")
        const addFolder = findChild(sheet, "vaultSourcesAddFolder")
        const connectServer = findChild(sheet, "vaultSourcesConnectServer")
        const close = findChild(sheet, "vaultSourcesClose")
        verify(panel !== null && folders !== null && footer !== null)
        verify(rescan !== null && forget !== null && addFolder !== null && connectServer !== null && close !== null)
        compare(folders.count, 18)
        verify(folders.height > 0)
        verify(footer.visible)
        verify(footer.mapToItem(sheet, 0, footer.height).y <= 696)
        verify(panel.y + panel.height <= 696)
        for (const button of [rescan, forget, addFolder, connectServer, close, footer]) {
            verify(button.visible)
            compare(button.background.border.width, 1)
        }
        compare(rescan.background.color, theme.glassTint)
        compare(forget.background.color, theme.glassTint)

        mouseMove(rescan, rescan.width / 2, rescan.height / 2)
        wait(230)
        tryCompare(rescan.background, "color", theme.glassHi, 1000)
        mousePress(rescan, rescan.width / 2, rescan.height / 2)
        tryVerify(function() { return Math.abs(rescan.background.scale - 0.97) < 0.005 }, 1000)
        verify(rescan.down)
        mouseRelease(rescan, rescan.width / 2, rescan.height / 2)
        close.forceActiveFocus(Qt.TabFocusReason)
        rescan.forceActiveFocus(Qt.TabFocusReason)
        tryVerify(function() { return rescan.visualFocus }, 500)
        compare(rescan.background.border.color, theme.gold)

        adapter.calls = []
        compare(adapter.calls.length, 0)
        mouseClick(rescan)
        compare(adapter.calls.length, 1)
        compare(adapter.calls[0].action, "rescan")
        compare(adapter.calls[0].path, roots[0].path)
        addSpy.target = sheet
        compare(addSpy.count, 0)
        mouseClick(addFolder)
        compare(addSpy.count, 1)
        verify(sheet.open)

        mouseClick(forget)
        const forgetDialog = findChild(sheet, "vaultForgetSourceDialog")
        verify(forgetDialog.opened)
        compare(forgetDialog.path, roots[0].path)
        forgetDialog.close()

        const edit = findChild(sheet, "vaultEditIgnorePatterns")
        const input = findChild(sheet, "vaultIgnorePatterns")
        mouseClick(edit)
        compare(sheet.editingIgnore, true)
        compare(edit.primary, true)
        compare(input.text, "*.tmp")
        input.text += "\nfixture-rule"
        mouseClick(edit)
        compare(sheet.editingIgnore, false)
        compare(adapter.patterns.join("\n"), "*.tmp\nfixture-rule")

        connectSpy.target = sheet
        mouseClick(connectServer)
        compare(connectSpy.count, 1)
        compare(sheet.open, false)
    }
}
