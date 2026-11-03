import QtQuick 2.15
import QtQuick.Window 2.15
import QtTest 1.3
import "../../qml" as Colosseum

TestCase {
    id: testCase
    name: "TopBarSpatialNavigation"
    when: windowShown

    Window {
        id: testWindow
        width: 1100
        height: 180
        visible: true
        property bool feriaComingSoon: true

        Item { id: backdrop; anchors.fill: parent }

        Colosseum.TopBar {
            id: bar
            backdrop: backdrop
            x: 30; y: 30
            width: 1040
        }
    }

    SignalSpy { id: boundarySpy; target: bar; signalName: "boundaryArrowRequested" }
    SignalSpy { id: mediumSpy; target: bar; signalName: "mediumSelected" }

    function test_compact_world_picker_clears_system_chrome_data() {
        return [{tag: "Tankoban", medium: "Tankoban"},
                {tag: "Biblio", medium: "Biblio"},
                {tag: "Theatre", medium: "Theatre"},
                {tag: "Universes", medium: "Universes"},
                {tag: "Feria", medium: "Feria"}]
    }
    QtObject {
        id: localAccount
        property string mode: "localOnly"
        property string username: ""
        property bool accountFeaturesAvailable: true
    }
    function test_system_chrome_keeps_required_controls_data() {
        return [{tag:"904", width:904}, {tag:"1040", width:1040},
                {tag:"1172", width:1172}, {tag:"1500", width:1500}]
    }
    function test_system_chrome_keeps_required_controls(data) {
        bar.width = data.width
        bar.activeMedium = "Theatre"
        bar.accountController = localAccount
        wait(30)
        var picker = findChild(bar, "topBarWorldPicker")
        var systems = findChild(bar, "topBarSystemChrome")
        var left = findChild(bar, "topBarLeftChrome")
        var feedback = findChild(bar, "feedbackEntry")
        var search = findChild(bar, "topBarSearch")
        var account = findChild(bar, "colosseumTopbarAccountButton")
        var deviceLabel = findChild(bar, "colosseumTopbarDeviceLabel")
        verify(feedback !== null && feedback.visible)
        compare(feedback.parent.width, 44, "Feedback stays icon-only in the top bar")
        verify(search !== null && search.visible, "Search stays in the top bar for Theatre")
        verify(account !== null && account.visible, "Device/login stays in the top bar")
        verify(deviceLabel !== null && deviceLabel.visible)
        compare(deviceLabel.text, "Device")
        var minimize = findAccessibleName(bar, "Minimize")
        var fullscreen = findAccessibleNameEither(bar, "Enter fullscreen", "Exit fullscreen")
        var quit = findAccessibleName(bar, "Quit Colosseum")
        verify(minimize && minimize.visible, "Minimize stays in the desktop top bar")
        verify(fullscreen && fullscreen.visible, "Fullscreen stays in the desktop top bar")
        verify(quit && quit.visible, "Quit stays in the desktop top bar")
        verify(picker.x >= left.x + left.width + 8, "Picker must clear the left controls")
        verify(picker.x + picker.width + 8 <= systems.x, "Picker must clear the right controls")
        verify(systems.x >= 0 && systems.x + systems.width <= bar.width + 1)
        bar.accountController = null
        bar.width = 1040
    }
    function test_compact_world_picker_clears_system_chrome(data) {
        bar.width = 904 // 1024px window minus the production side margins
        bar.activeMedium = data.medium
        bar.accountController = localAccount
        wait(30)
        var picker = findChild(bar, "topBarWorldPicker")
        var systems = findChild(bar, "topBarSystemChrome")
        verify(picker.x + picker.width + 8 <= systems.x,
               "World picker must leave space before top bar system controls")
        var feria = findChild(bar, "modePillFocus_Feria")
        var universes = findChild(bar, "modePillFocus_Universes")
        verify(feria !== null)
        compare(feria.enabled, false)
        mediumSpy.clear()
        mouseClick(feria)
        compare(mediumSpy.count, 0)
        mouseClick(universes)
        compare(mediumSpy.count, 1)
        compare(mediumSpy.signalArguments[0][0], "Universes")
        bar.width = 1040
        bar.accountController = null
    }

    function findChild(root, objectName) {
        if (!root) return null
        if (root.objectName === objectName) return root
        var kids = root.children || []
        for (var i = 0; i < kids.length; i++) {
            var found = findChild(kids[i], objectName)
            if (found) return found
        }
        return null
    }

    function findAccessibleName(root, name) {
        if (!root) return null
        if (root.accessibleName === name) return root
        var kids = root.children || []
        for (var i = 0; i < kids.length; i++) {
            var found = findAccessibleName(kids[i], name)
            if (found) return found
        }
        return null
    }

    function findAccessibleNameEither(root, first, second) {
        return findAccessibleName(root, first) || findAccessibleName(root, second)
    }

    function focusableFace(root) {
        if (!root) return null
        if (root.focusPolicy !== undefined && root.focusPolicy !== Qt.NoFocus)
            return root
        var kids = root.children || []
        for (var i = 0; i < kids.length; i++) {
            var found = focusableFace(kids[i])
            if (found) return found
        }
        return null
    }

    function init() {
        testWindow.requestActivate()
        boundarySpy.clear()
        mediumSpy.clear()
        bar.accountController = null
        bar.activeMedium = ""
        bar.lifecycleActive = true
        testWindow.feriaComingSoon = true
        wait(20)
    }

    function test_feria_follows_host_availability() {
        var feria = findChild(bar, "modePillFocus_Feria")
        compare(feria.enabled, false)
        testWindow.feriaComingSoon = false
        wait(0)
        compare(feria.enabled, true)
        mouseClick(feria)
        compare(mediumSpy.count, 1)
        compare(mediumSpy.signalArguments[0][0], "Feria")
    }

    function test_topbar_has_feedback_search_account_and_window_controls() {
        bar.activeMedium = "Theatre"
        bar.accountController = localAccount
        wait(0)
        var feedback = findChild(bar, "feedbackEntry")
        var search = findChild(bar, "topBarSearch")
        var account = findChild(bar, "colosseumTopbarAccountButton")
        verify(feedback !== null && feedback.visible)
        compare(feedback.parent.width, 44)
        verify(search !== null && search.visible)
        verify(account !== null && account.visible)
        verify(findAccessibleName(bar, "Minimize").visible)
        verify(findAccessibleNameEither(bar, "Enter fullscreen", "Exit fullscreen").visible)
        verify(findAccessibleName(bar, "Quit Colosseum").visible)
        verify(findChild(bar, "topBarStremioButton") === null)
        verify(findChild(bar, "taskbarUtility_trackers") === null)
        verify(findChild(bar, "taskbarUtility_updates") === null)
        verify(findChild(bar, "topBarWallpaperButton") === null)
        verify(findChild(bar, "topBarDlnaButton") === null)
        bar.accountController = null
    }

    function test_right_moves_between_visible_mode_pills() {
        var tankoban = focusableFace(findChild(bar, "modePill_Tankoban"))
        var biblio = focusableFace(findChild(bar, "modePill_Biblio"))
        verify(tankoban !== null)
        verify(biblio !== null)
        tankoban.forceActiveFocus(Qt.OtherFocusReason)
        keyClick(Qt.Key_Right)
        verify(biblio.activeFocus)
    }

    function test_down_at_topbar_boundary_is_exported_to_host() {
        var tankoban = focusableFace(findChild(bar, "modePill_Tankoban"))
        verify(tankoban !== null)
        tankoban.forceActiveFocus(Qt.OtherFocusReason)
        keyClick(Qt.Key_Down)
        compare(boundarySpy.count, 1)
        compare(boundarySpy.signalArguments[0][0], Qt.Key_Down)
    }

    function test_universes_is_the_fourth_world_and_feria_is_non_navigable_while_coming_soon() {
        var theatre = focusableFace(findChild(bar, "modePill_Theatre"))
        var universesPill = findChild(bar, "modePill_Universes")
        var universes = focusableFace(universesPill)
        var feriaPill = findChild(bar, "modePill_Feria")
        var feriaFocus = findChild(bar, "modePillFocus_Feria")
        var vinylFocus = findChild(bar, "modePillFocus_Vinyl")
        verify(theatre !== null && universes !== null)
        verify(feriaPill !== null && feriaFocus !== null && vinylFocus !== null)
        compare(feriaPill.comingSoon, true)
        verify(!feriaFocus.enabled)
        verify(!vinylFocus.enabled)
        theatre.forceActiveFocus(Qt.OtherFocusReason)
        keyClick(Qt.Key_Right)
        verify(universes.activeFocus)
        keyClick(Qt.Key_Return)
        compare(mediumSpy.count, 1)
        compare(mediumSpy.signalArguments[0][0], "Universes")
        bar.activeMedium = "Universes"
        compare(universesPill.active, true)
        compare(findChild(bar, "modePill_Theatre").active, false)
        bar.activeMedium = "Feria"
        compare(feriaPill.active, false)
    }

    function test_compact_browser_chrome_flag() {
        bar.width = 1240
        compare(bar.compactChrome, false)
        bar.compactBrowserChrome = true
        compare(bar.compactChrome, true)
        bar.compactBrowserChrome = false
        compare(bar.compactChrome, false)
        bar.width = 1040
    }
}
