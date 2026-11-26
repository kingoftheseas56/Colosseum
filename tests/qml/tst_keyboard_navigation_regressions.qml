import QtQuick
import QtQuick.Controls
import QtQuick.Window
import QtTest
import "../../qml" as C

// Exercise the shipped Home hero, not a parallel copy. Main's native services
// are outside this focus contract; only its roster and host actions are stubbed.
TestCase {
    id: testCase
    name: "KeyboardNavigationRegressions"
    when: windowShown

    Window {
        id: win
        width: 1100; height: 800; visible: true
        property bool televisionMode: false
        property bool immersiveSurfaceOpen: false
        property var installedUniverses: [
            { name: "One Piece", extensionId: "one-piece" },
            { name: "Star Wars", extensionId: "star-wars" },
            { name: "Middle Earth", extensionId: "middle-earth" }
        ]
        property string opened: ""
        function openUniverse(id, name) { opened = id }
        function openWorld(name) { opened = name }
        function openUniverseHall() { opened = "hall" }
        C.Theme { id: theme }
        QtObject {
            id: presentationPolicy
            property int homeHeroHeight: 360
            property bool shortAndroid: false
            property int heroCopyInset: 24
            property int heroTitleSize: 40
        }
        Item { id: wall; anchors.fill: parent }
        Item {
            id: page
            property real contentY: 0
            x: 40; y: 70; width: 1000; height: 360
        }
        C.KeyboardAction { id: before; x: 40; y: 10; width: 100; height: 40 }
        C.KeyboardAction { id: excluded; x: 160; y: 10; width: 100; height: 40; focusEnabled: false }
        C.KeyboardAction { id: after; x: 40; y: 460; width: 100; height: 40 }
        C.Taskbar { id: taskbar; visible: false; localMediaAvailable: true }
    }

    property var hero: null
    property var view: null
    property var timer: null

    function descendants(root, predicate) {
        let result = []
        function visit(item) {
            if (predicate(item)) result.push(item)
            for (let child of (item.children || [])) visit(child)
        }
        visit(root)
        return result
    }
    function belongsTo(item, ancestor) {
        for (let node = item; node; node = node.parent)
            if (node === ancestor) return true
        return false
    }
    function initTestCase() {
        const request = new XMLHttpRequest()
        request.open("GET", Qt.resolvedUrl("../../qml/Main.qml"), false)
        request.send()
        // Windows checkouts may carry CRLF; the slice markers below expect LF.
        const source = request.responseText.replace(/\r\n/g, "\n")
        const marker = source.indexOf("id: hero\n")
        verify(marker > 0)
        const start = source.lastIndexOf("Glass {", marker)
        const end = source.indexOf("\n\n                HomeFeedbackActions {", marker)
        verify(start > 0 && end > start)
        hero = Qt.createQmlObject('import QtQuick; import QtQuick.Controls; import "'
            + Qt.resolvedUrl("../../qml") + '"; ' + source.slice(start, end), page,
            Qt.resolvedUrl("HomeHeroKeyboardFixture.qml"))
        view = descendants(hero, item => item.currentIndex !== undefined && item.itemAt !== undefined)[0]
        verify(view !== undefined)
        for (let child of hero.data) {
            if (child.interval !== undefined && child.repeat !== undefined) timer = child
        }
        // Glass redirects its default data into its visual holder.
        if (!timer) {
            for (let item of descendants(hero, () => true)) {
                for (let child of (item.data || [])) {
                    if (child.interval !== undefined && child.repeat !== undefined) timer = child
                }
            }
        }
        verify(timer !== null)
    }
    function init() {
        win.requestActivate()
        win.televisionMode = false
        win.immersiveSurfaceOpen = false
        taskbar.visible = false
        page.visible = true
        before.forceActiveFocus(Qt.TabFocusReason)
        timer.interval = 6500
        view.currentIndex = 0
        win.opened = ""
        wait(350) // Settle the previous test's SwipeView transition.
    }
    function test_focus_disabled_action_is_not_a_tab_stop() {
        before.forceActiveFocus(Qt.TabFocusReason)
        keyClick(Qt.Key_Tab)
        verify(!excluded.activeFocus, "focusEnabled=false must remove an action from Tab order")
    }
    function test_focus_exclusion_takes_effect_after_active_item_loses_focus() {
        excluded.focusEnabled = true
        excluded.forceActiveFocus(Qt.TabFocusReason)
        excluded.focusEnabled = false
        before.forceActiveFocus(Qt.TabFocusReason)
        verify(!excluded.activeFocusOnTab, "Qt must not retain a rejected NoFocus policy")
        keyClick(Qt.Key_Tab)
        verify(!excluded.activeFocus)
    }
    function test_home_tabs_visit_only_current_slide_and_can_leave() {
        view.forceActiveFocus(Qt.TabFocusReason)
        const ring = findChild(hero, "homeHeroFocusRing")
        verify(ring !== null)
        verify(ring.shown, "The carousel's own focus stop needs a visible ring")
        let leftHero = false
        for (let i = 0; i < 6; ++i) {
            keyClick(Qt.Key_Tab)
            const focused = win.activeFocusItem
            if (!belongsTo(focused, hero)) { leftHero = true; break }
            const p = focused.mapToItem(hero, 0, 0)
            verify(p.x >= 0 && p.x + focused.width <= hero.width + 1,
                   "Tab must not enter an offscreen slide")
        }
        verify(leftHero, "Tab must leave Home hero in a bounded number of stops")
    }
    function test_home_reverse_tabs_and_manual_slide_change_stay_visible() {
        view.forceActiveFocus(Qt.TabFocusReason)
        keyClick(Qt.Key_Right)
        tryCompare(view, "currentIndex", 1)
        wait(350)
        keyClick(Qt.Key_Tab)
        const focused = win.activeFocusItem
        const p = focused.mapToItem(hero, 0, 0)
        verify(p.x >= 0 && p.x + focused.width <= hero.width + 1)
        keyClick(Qt.Key_Return)
        compare(win.opened, "star-wars")
        keyClick(Qt.Key_Backtab)
        if (win.activeFocusItem === view) keyClick(Qt.Key_Backtab)
        verify(!belongsTo(win.activeFocusItem, hero), "Shift+Tab must leave the hero")
    }
    function test_home_hidden_or_immersive_still_stops_autoplay() {
        timer.interval = 120
        page.visible = false
        wait(300)
        compare(view.currentIndex, 0)
        page.visible = true
        win.immersiveSurfaceOpen = true
        wait(300)
        compare(view.currentIndex, 0)
    }
    function test_home_autoplay_pauses_for_keyboard_and_resumes_after_leaving() {
        timer.interval = 120
        view.forceActiveFocus(Qt.TabFocusReason)
        keyClick(Qt.Key_Tab)
        const focused = win.activeFocusItem
        const current = view.currentIndex
        wait(400)
        compare(view.currentIndex, current, "Autoplay must pause while focus is in hero")
        compare(win.activeFocusItem, focused, "Timer must not reset focus")
        after.forceActiveFocus(Qt.TabFocusReason)
        tryVerify(() => view.currentIndex !== current, 1000)
    }
    function test_home_one_action_per_slide_in_tv_mode() {
        win.televisionMode = true
        view.forceActiveFocus(Qt.TabFocusReason)
        keyClick(Qt.Key_Tab)
        keyClick(Qt.Key_Return)
        compare(win.opened, "one-piece")
        keyClick(Qt.Key_Tab)
        keyClick(Qt.Key_Return)
        compare(win.opened, "Universes", "No duplicate tribute or legacy Hall stop")
    }
    function test_taskbar_toggle_has_one_semantic_focus_stop() {
        page.visible = false
        taskbar.visible = true
        const home = findChild(taskbar, "colosseumTaskbarHomeButton")
        verify(home !== null)
        const stops = descendants(home, item => item.activeFocusOnTab === true)
        compare(stops.length, 1, "Dock toggle must have one focusable action")
        stops[0].forceActiveFocus(Qt.TabFocusReason)
        taskbar.open = false
        keyClick(Qt.Key_Return)
        compare(taskbar.open, true)
        keyClick(Qt.Key_Space)
        compare(taskbar.open, false)
    }
}
