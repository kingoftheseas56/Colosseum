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
    SignalSpy { id: stremioSpy; target: bar; signalName: "stremioClicked" }
    SignalSpy { id: mediumSpy; target: bar; signalName: "mediumSelected" }
    SignalSpy { id: searchSpy; target: bar; signalName: "searchClicked" }

    function test_compact_world_picker_does_not_overlap_search_data() {
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
    function test_system_controls_have_space_with_local_account_data() {
        return [{tag:"904", width:904}, {tag:"1040", width:1040},
                {tag:"1172", width:1172}, {tag:"1500", width:1500}]
    }
    function test_system_controls_have_space_with_local_account(data) {
        bar.width = data.width
        bar.activeMedium = "Theatre"
        bar.trackersEnabled = true
        bar.accountController = localAccount
        wait(30)
        var picker = findChild(bar, "topBarWorldPicker")
        var systems = findChild(bar, "topBarSystemChrome")
        var left = findChild(bar, "topBarLeftChrome")
        verify(systems.spacing >= (data.width < 1000 ? 12 : 20),
               "System controls need breathing room at the active window size")
        verify(picker.x >= left.x + left.width + 8, "Picker must clear the left controls")
        verify(picker.x + picker.width + 8 <= systems.x, "Picker must clear the right controls")
        verify(systems.x >= 0 && systems.x + systems.width <= bar.width + 1)
        bar.accountController = null
        bar.trackersEnabled = false
        bar.width = 1040
    }
    function test_compact_world_picker_does_not_overlap_search(data) {
        bar.width = 904 // 1024px window minus the production side margins
        bar.activeMedium = data.medium
        bar.trackersEnabled = true
        wait(30)
        var picker = findChild(bar, "topBarWorldPicker")
        var systems = findChild(bar, "topBarSystemChrome")
        verify(picker.x + picker.width + 8 <= systems.x,
               "World picker must leave space before system controls")
        var feria = findChild(bar, "modePillFocus_Feria")
        var universes = findChild(bar, "modePillFocus_Universes")
        verify(feria !== null)
        compare(feria.enabled, false)
        mediumSpy.clear(); searchSpy.clear()
        mouseClick(feria)
        compare(mediumSpy.count, 0)
        mouseClick(universes)
        compare(mediumSpy.count, 1)
        compare(mediumSpy.signalArguments[0][0], "Universes")
        compare(searchSpy.count, 0)
        bar.width = 1040
        bar.trackersEnabled = false
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
        stremioSpy.clear()
        mediumSpy.clear()
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

    function test_stremio_is_theatre_only_and_between_search_and_account() {
        bar.activeMedium = "Theatre"
        wait(0)
        var stremio = findChild(bar, "topBarStremioButton")
        var search = findChild(bar, "topBarSearch")
        var account = findChild(bar, "colosseumTopbarAccountButton")
        verify(stremio !== null)
        var asset = findChild(stremio, "topBarStremioOfficialAsset")
        verify(asset !== null)
        verify(String(asset.source).indexOf("assets/icons/stremio-official.svg") >= 0)
        bar.activeMedium = "Biblio"
        compare(stremio.visible, false)
        bar.activeMedium = "Tankoban"
        compare(stremio.visible, false)
        bar.activeMedium = "Theatre"
        wait(0)
        compare(stremio.visible, true)
        compare(search.parent, stremio.parent)
        compare(stremio.parent, account.parent)
        var siblings = search.parent.children
        verify(siblings.indexOf(search) < siblings.indexOf(stremio))
        verify(siblings.indexOf(stremio) < siblings.indexOf(account))
        var face = focusableFace(stremio)
        verify(face !== null)
        face.forceActiveFocus(Qt.OtherFocusReason)
        keyClick(Qt.Key_Return)
        compare(stremioSpy.count, 1)
        bar.lifecycleActive = false
        compare(stremio.visible, false)
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

    function test_wallpaper_and_compact_browser_chrome_flags() {
        var wall = findChild(bar, "topBarWallpaperButton")
        verify(wall !== null)
        compare(wall.visible, true)
        bar.wallpaperEnabled = false
        compare(wall.visible, false)
        bar.wallpaperEnabled = true
        compare(wall.visible, true)
        bar.width = 1240
        compare(bar.compactChrome, false)
        bar.compactBrowserChrome = true
        compare(bar.compactChrome, true)
        bar.compactBrowserChrome = false
        compare(bar.compactChrome, false)
    }
}
