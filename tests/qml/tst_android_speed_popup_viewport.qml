import QtQuick
import QtQuick.Window
import QtTest

// PF-05: the production PlayerPage speedPopup block (speed / sleep timer / skip step) is
// compiled with recording seams (same extraction technique as tst_player_more_touch.qml).
// Covers QML geometry/scroll/focus ownership, not native Media3 composition.
TestCase {
    id: testCase
    name: "AndroidSpeedPopupViewport"
    when: windowShown
    Window { id: window; width: 1185; height: 533; visible: true }
    property string playerSource: ""
    property var host: null

    function blockAt(source, start) {
        verify(start >= 0, "Required production source block exists")
        const opening = source.indexOf("{", start)
        let depth = 1
        for (let end = opening + 1; end < source.length; ++end) {
            if (source[end] === "{") ++depth
            else if (source[end] === "}" && --depth === 0) return source.slice(start, end + 1)
        }
        fail("Unterminated production source block")
        return ""
    }
    function productionFunction(name) {
        return blockAt(playerSource, playerSource.indexOf("function " + name + "("))
    }
    function productionProperty(marker) {
        const start = playerSource.indexOf(marker)
        verify(start >= 0, marker)
        return blockAt(playerSource, start).replace(/^\s*/, "")
    }
    function initTestCase() {
        const request = new XMLHttpRequest()
        request.open("GET", Qt.resolvedUrl("../../qml/PlayerPage.qml"), false)
        request.send()
        playerSource = request.responseText
        verify(playerSource.length > 0, "Read production PlayerPage; enable QML_XHR_ALLOW_FILE_READ")
    }
    function build(w, h, sleepActive) {
        window.width = w; window.height = h
        const popupMarker = playerSource.indexOf("id: speedPopup")
        const popup = blockAt(playerSource, playerSource.lastIndexOf("Rectangle {", popupMarker))
        const presetsStart = playerSource.indexOf("readonly property var sleepPresets: [")
        const presets = playerSource.slice(presetsStart, playerSource.indexOf("]", presetsStart) + 1)
        const source = [
            'import QtQuick',
            'import QtQuick.Window',
            'import "../../qml"',
            'import "../../qml/PlayerFocusContainment.js" as FocusContainment',
            'Item { id: root; width: ' + w + '; height: ' + h,
            'property string sleepTimerMode: "' + (sleepActive ? "minutes" : "off") + '"',
            'readonly property bool sleepTimerActive: root.sleepTimerMode !== "off"',
            'readonly property var speedChoices: [0.5, 0.75, 1, 1.25, 1.5, 1.75, 2]',
            presets,
            'property int wakeCount: 0; property var chosenSleep: null; property int cancelCount: 0',
            'Theme { id: theme }',
            'QtObject { id: mpv; property real speed: 1 }',
            'QtObject { id: playerSettings; property int seekStepSeconds: 10 }',
            'function wakeChrome() { wakeCount++ }',
            'function setSleepTimer(p) { chosenSleep = p; sleepTimerMode = p.mode }',
            'function cancelSleepTimer() { cancelCount++; sleepTimerMode = "off" }',
            'function sleepTimerLabel() { return "12:00" }',
            'function sleepPresetSelected(p) { return false }',
            productionFunction("clamp"),
            'Item { id: chrome; anchors.fill: parent }',
            'Item { id: sm; property bool panelOpen: false; x: root.width - 260; y: root.height - 60; width: 80; height: 40',
            popup,
            '}',
            '}'
        ].join("\n")
        host = Qt.createQmlObject(source, window.contentItem, Qt.resolvedUrl("SpeedPopupProductionExtract.qml"))
        verify(host !== null)
        return host
    }
    function cleanup() {
        if (host) { host.destroy(); host = null }
    }
    function find(root, name) {
        if (root.objectName === name) return root
        const kids = root.children || []
        for (let i = 0; i < kids.length; ++i) { const f = find(kids[i], name); if (f) return f }
        return null
    }
    function smOf(h) { return h.children[h.children.length - 1] }
    function open(h) {
        const sm = h.children.filter(c => c.panelOpen !== undefined)[0]
        sm.panelOpen = true
        waitForRendering(h)
        return sm
    }
    function popupIn(h) { return find(h, "playerSpeedPanel") }

    function test_desktop_size_unchanged_when_room() {
        const h = build(1280, 900, false)
        open(h)
        const p = popupIn(h)
        compare(p.width, 420)
        compare(p.height, p.bodyHeight)
        verify(!find(h, "playerSpeedScroll").interactive)
    }

    function test_supported_viewports_bound_popup_and_reach_last_controls_data() {
        return [
            { tag: "zoom1.15", w: 1237, h: Math.floor(640 / 1.15), sleep: false },
            { tag: "zoom1.20", w: 1185, h: Math.floor(640 / 1.20), sleep: true },
            { tag: "short-354", w: 640, h: 354, sleep: true },
            { tag: "narrow-360", w: 360, h: 300, sleep: true }
        ]
    }
    function test_supported_viewports_bound_popup_and_reach_last_controls(data) {
        const h = build(data.w, data.h, data.sleep)
        open(h)
        const p = popupIn(h)
        verify(p.visible)
        verify(p.x >= 0 && p.y >= 0, "popup origin inside chrome " + p.x + "," + p.y)
        verify(p.x + p.width <= data.w + 0.5 && p.y + p.height <= data.h + 0.5,
               "popup inside chrome " + JSON.stringify([p.x, p.y, p.width, p.height]))
        verify(p.bodyHeight > 500 - 0.5 || !data.sleep, "ten presets + active timer row exceed 500")
        const scroll = find(h, "playerSpeedScroll")
        verify(scroll.interactive === (p.bodyHeight > p.height + 0.5))
        // Final skip-step control reachable by scrolling to the end.
        scroll.contentY = Math.max(0, scroll.contentHeight - scroll.height)
        const lastSkipTop = p.skipStepTop + 22
        verify(lastSkipTop - scroll.contentY >= 0 && lastSkipTop + 28 - scroll.contentY <= scroll.height + 0.5)
    }

    function test_keyboard_focus_reveals_last_sleep_preset() {
        const h = build(1185, Math.floor(640 / 1.20), true)
        open(h)
        const p = popupIn(h)
        const scroll = find(h, "playerSpeedScroll")
        verify(scroll.interactive)
        compare(scroll.contentY, 0)
        const sleep = find(p, "playerSleepChoices")
        verify(sleep !== null, "production sleep repeater")
        sleep.keyboardIndex = h.sleepPresets.length - 1
        const top = 46 + (h.sleepPresets.length - 1) * 38
        verify(top >= scroll.contentY && top + 36 <= scroll.contentY + scroll.height + 0.5,
               "last sleep preset revealed; contentY=" + scroll.contentY)
        sleep.keyboardIndex = 0
        verify(46 >= scroll.contentY, "first preset revealed again")
    }

    function test_resize_relocates_open_popup() {
        const h = build(1280, 900, false)
        open(h)
        const p = popupIn(h)
        window.height = 400; h.height = 400
        wait(30)
        verify(p.y >= 0 && p.y + p.height <= 400.5, "relocated after resize")
    }

    function test_touch_choose_speed_closes_and_applies() {
        const h = build(1185, Math.floor(640 / 1.20), false)
        const sm = open(h)
        const p = popupIn(h)
        // Speed row 2.0x is the last speed delegate (y = 46 + 6*38 in body coordinates).
        const scroll = find(h, "playerSpeedScroll")
        scroll.contentY = 0
        const bodyY = 46 + 6 * 38 + 18
        const pt = p.mapToItem(window.contentItem, 40, bodyY - scroll.contentY)
        verify(bodyY - scroll.contentY < scroll.height, "2.0x row visible at the top of the body")
        touchEvent(window.contentItem).press(0, window.contentItem, pt.x, pt.y).commit()
        touchEvent(window.contentItem).release(0, window.contentItem, pt.x, pt.y).commit()
        wait(30)
        verify(!sm.panelOpen, "choosing a speed closes the panel")
        verify(h.wakeCount > 0)
    }
}
