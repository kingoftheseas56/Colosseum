import QtQuick
import QtQuick.Window
import QtTest
import "../../qml" as Colosseum

TestCase {
    name: "VaultOfflineRestore"
    when: windowShown
    Window { id: testWindow; width: 1100; height: 820; visible: true }
    Component { id: pageComponent; Colosseum.VaultPage {} }
    property var page: null
    property string savedRoot: "/fixture/library"

    function init() {
        if (typeof VaultLibrary === "undefined") skip("Use vault_browse_restore_qml_tests for the isolated projection fixture")
        VaultLibrary.setRoots([{path: savedRoot, name: "Library", available: true}])
        page = pageComponent.createObject(testWindow.contentItem, {width: 1100, height: 820})
        verify(page !== null)
        page.selectRoot(savedRoot, "Library") // replaces this runner's previous test crumbs
        page.pushCrumb(savedRoot + "/Show", "Show", "show")
        page.pushCrumb(savedRoot + "/Show/Season 1", "Season 1", "season")
        compare(page.currentBrowsePath, savedRoot + "/Show/Season 1")
    }
    function cleanup() {
        if (page) page.destroy()
        page = null
        wait(0)
    }
    function reopenWithRoots(roots) {
        page.destroy()
        page = null
        wait(0)
        VaultLibrary.setRoots(roots)
        page = pageComponent.createObject(testWindow.contentItem, {width: 1100, height: 820})
        verify(page !== null)
        wait(0)
    }
    function test_offline_root_keeps_saved_season() {
        reopenWithRoots([{path: savedRoot, name: "Library", available: false},
                         {path: "/fixture/online", name: "Online", available: true}])
        compare(page.currentBrowsePath, savedRoot + "/Show/Season 1")
        compare(page.crumbStack.length, 3)
        compare(page.crumbStack[2].displayTitle, "Season 1")
        compare(page.crumbStack[2].nodeType, "season")
    }
    function test_available_root_still_restores_saved_season() {
        reopenWithRoots([{path: savedRoot, name: "Library", available: true}])
        compare(page.currentBrowsePath, savedRoot + "/Show/Season 1")
        compare(page.crumbStack.length, 3)
    }
    function test_removed_root_falls_back_to_registered_available_root() {
        reopenWithRoots([{path: "/fixture/offline", name: "Offline", available: false},
                         {path: "/fixture/online", name: "Online", available: true}])
        compare(page.currentBrowsePath, "/fixture/online")
        compare(page.crumbStack.length, 1)
    }
}
