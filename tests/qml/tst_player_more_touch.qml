import QtQuick
import QtQuick.Window
import QtTest

// The real PlayerPage pointer area, More panel and inline controls are compiled
// with recording backend seams because full PlayerPage needs native player types.
// The only source substitution names the PlatformRuntime seam platformRuntime.
// This covers QML ownership/geometry, not native Media3/Qt composition.
TestCase {
    id: testCase
    name: "PlayerMoreTouch"
    when: windowShown
    Window { id: window; width: 640; height: 354; visible: true }
    property string playerSource: ""
    property var host: null
    property var pendingTouch: null
    SignalSpy { id: taps; signalName: "videoTapped" }
    SignalSpy { id: actions; signalName: "actionSelected" }
    SignalSpy { id: pauses; signalName: "playPauseRequested" }

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
    function initTestCase() {
        const request = new XMLHttpRequest()
        request.open("GET", Qt.resolvedUrl("../../qml/PlayerPage.qml"), false)
        request.send()
        playerSource = request.responseText
        verify(playerSource.length > 0, "Read production PlayerPage; enable QML_XHR_ALLOW_FILE_READ")
    }
    function init() {
        window.width = 640; window.height = 354
        window.requestActivate()
        const mouseMarker = playerSource.indexOf("// Cursor vanishes WITH the HUD")
        const pointer = blockAt(playerSource, playerSource.lastIndexOf("MouseArea {", mouseMarker))
        const panelMarker = playerSource.indexOf("id: overflowPanel")
        const panel = blockAt(playerSource, playerSource.lastIndexOf("Rectangle {", panelMarker))
        const source = [
            'pragma ComponentBehavior: Bound',
            'import QtQuick',
            'import QtQuick.Window',
            'import "../../qml"',
            'import "../../qml/PlayerFocusContainment.js" as FocusContainment',
            'Item { id: root; width: 640; height: 354',
            'property alias android: platformRuntime.android',
            'property alias morePanel: overflowPanel',
            'property alias gestureSurface: playerTouchSurface',
            'property bool controlsShown: true; property bool starting: false; property bool touchLocked: false',
            'property bool anyMenuOpen: overflowOpen; property bool overflowOpen: false',
            'property point contextMenuPos: Qt.point(320, 177); property var overflowFocusReturnItem: null',
            'property bool tight: true; property bool barTiny: true; property bool barSnug: true',
            'property string mediaId: "tt:fixture"; property string gifState: "idle"; property bool pipMode: false',
            'property var streamCandidates: [{}, {}]; property int wakeCount: 0',
            'signal videoTapped(); signal actionSelected(string kind); signal playPauseRequested()',
            'QtObject { id: platformRuntime; property bool android: true }',
            'QtObject { id: mpv; property real volume: 100; property bool mute: false }',
            'Theme { id: theme }',
            'function supportsPlayerCapability(name) { return false }',
            'function currentCastUrl() { return "https://example.invalid/media" }',
            'function loudnessLabel() { return "Off" }',
            'function overflowValue(kind) { return "" }',
            'function activateOverflowKind(kind) { actionSelected(kind); overflowOpen = false }',
            'function volumeFraction() { return mpv.volume / 100 }',
            'function setVolumeFromFraction(value) { mpv.volume = value * 100 }',
            'function adjustVolume(delta) { mpv.volume += delta }',
            'function adjustVolumeTo(value) { mpv.volume = value }',
            'function wakeChrome() { wakeCount++ }',
            'function togglePlayPause() { playPauseRequested() }',
            'function closeMenus() { overflowOpen = false }',
            'function currentKeyboardFocusItem() { return null }',
            'function moveKeyboardFocus(forward) { return false }',
            productionFunction("clamp"),
            productionFunction("openContextMenu"),
            'PlayerTouchSurface { id: playerTouchSurface; anchors.fill: parent',
            'enabled: platformRuntime.android; visible: platformRuntime.android; locked: root.touchLocked',
            'modalOpen: root.overflowOpen; topExclusion: 80; bottomExclusion: 126',
            'onSingleTapped: root.videoTapped()',
            'onContextMenuRequested: (x, y) => root.openContextMenu(playerTouchSurface.mapToItem(chrome, x, y))',
            '}',
            pointer,
            'Item { id: chrome; anchors.fill: parent; z: 99999',
            panel,
            '}',
            blockAt(playerSource, playerSource.indexOf("component RoundButton:")),
            blockAt(playerSource, playerSource.indexOf("component VolumeControl:")),
            '}'
        ].join("\n").replace(/\bPlatformRuntime\b/g, "platformRuntime")
        host = Qt.createQmlObject(source, window.contentItem, Qt.resolvedUrl("PlayerMoreProductionExtract.qml"))
        verify(host !== null)
        taps.target = host; taps.clear()
        actions.target = host; actions.clear()
        pauses.target = host; pauses.clear()
        mouseMove(window.contentItem, 2, 2)
        waitForRendering(host)
        host.wakeCount = 0
    }
    function releaseTouch(x, y) {
        if (!pendingTouch) return
        pendingTouch.release(0, window.contentItem, x, y).commit()
        pendingTouch = null
    }
    function beginTouch(item, x, y) {
        const p = item.mapToItem(window.contentItem, x, y)
        pendingTouch = touchEvent(window.contentItem)
        pendingTouch.press(0, window.contentItem, p.x, p.y).commit()
        return p
    }
    function cleanup() {
        releaseTouch(-50, -50)
        taps.target = null; actions.target = null; pauses.target = null
        if (host) host.destroy()
        host = null
        wait(20)
    }
    function row(item, kind) {
        if (item.modelData && item.modelData.kind === kind) return item
        const children = item.children || []
        for (let i = 0; i < children.length; ++i) {
            const found = row(children[i], kind)
            if (found) return found
        }
        return null
    }
    function inside(item, viewport) {
        const p = item.mapToItem(viewport, 0, 0)
        return p.x >= -1 && p.y >= -1 && p.x + item.width <= viewport.width + 1
            && p.y + item.height <= viewport.height + 1
    }

    function test_android_left_touch_stays_on_surface_without_hover_wake() {
        const p = beginTouch(host, 320, 170)
        wait(25)
        releaseTouch(p.x, p.y)
        tryCompare(taps, "count", 1)
        compare(pauses.count, 0)
        compare(host.overflowOpen, false)
        compare(host.wakeCount, 0)
    }
    function test_android_physical_right_click_opens_existing_more_and_respects_lock() {
        mouseClick(host, 320, 170, Qt.RightButton)
        compare(host.overflowOpen, true, "Android physical right-click must keep beta's existing More route")
        host.overflowOpen = false
        host.touchLocked = true
        mouseClick(host, 320, 170, Qt.RightButton)
        compare(host.overflowOpen, false)
        compare(pauses.count, 0)
        compare(taps.count, 0)
    }
    function test_desktop_pointer_routes_are_preserved() {
        host.android = false
        mouseMove(host, 320, 170)
        verify(host.wakeCount > 0)
        mouseClick(host, 320, 170, Qt.LeftButton)
        compare(pauses.count, 1)
        mouseClick(host, 320, 170, Qt.RightButton)
        compare(host.overflowOpen, true)
        compare(taps.count, 0)
    }
    function test_hold_opens_touch_controls_without_extra_tap() {
        const p = beginTouch(host, 320, 170)
        tryCompare(host, "overflowOpen", true, 1500)
        releaseTouch(p.x, p.y)
        const touchControls = row(host.morePanel, "touch")
        verify(touchControls !== null && inside(touchControls, host))
        const target = beginTouch(touchControls, touchControls.width / 2, touchControls.height / 2)
        wait(25)
        releaseTouch(target.x, target.y)
        tryCompare(actions, "count", 1)
        compare(actions.signalArguments[0][0], "touch")
        wait(Qt.styleHints.mouseDoubleClickInterval + 30)
        compare(taps.count, 0)
    }
    function test_more_panel_fits_short_landscape_and_scrolls_rows_without_activation() {
        host.openContextMenu(Qt.point(320, 177))
        waitForRendering(host.morePanel)
        verify(inside(host.morePanel, host), "The production More panel must fit the short Android viewport")
        const touchControls = row(host.morePanel, "touch")
        const before = touchControls.mapToItem(host, 0, 0).y
        const p = beginTouch(touchControls, touchControls.width / 2, touchControls.height / 2)
        for (let i = 1; i <= 4; ++i) {
            pendingTouch.move(0, window.contentItem, p.x, p.y - i * 20).commit()
            wait(25)
        }
        releaseTouch(p.x, p.y - 80)
        tryVerify(function() { return touchControls.mapToItem(host, 0, 0).y < before - 10 })
        compare(actions.count, 0)
        const scroller = findChild(host, "playerMoreScroll")
        verify(scroller !== null)
        scroller.cancelFlick()
        scroller.contentY = scroller.contentHeight - scroller.height
        const last = row(host.morePanel, "shortcuts")
        verify(last !== null && inside(last, host), "The retained last row must be reachable")
        scroller.contentY = 0
        let input = null
        for (const child of last.children)
            if (child.accessibleName === "Keyboard shortcuts" && typeof child.activate === "function") input = child
        verify(input !== null)
        input.forceActiveFocus(Qt.TabFocusReason)
        tryVerify(function() { return inside(last, host.morePanel) }, 1000,
                  "The retained keyboard action must be revealed inside the new viewport")
        host.height = 300
        window.height = 300
        tryVerify(function() { return inside(host.morePanel, host) }, 1000,
                  "An open More panel must remain bounded after viewport height changes")
        tryVerify(function() { return inside(last, host.morePanel) }, 1000)
    }
}
