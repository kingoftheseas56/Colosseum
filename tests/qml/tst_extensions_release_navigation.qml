import QtQuick
import QtQuick.Window
import QtTest
import "../../qml" as Colosseum
import "../../qml/ExtensionsStoreApi.js" as StoreApi

TestCase {
    id: tests
    name: "ExtensionsReleaseNavigation"
    when: windowShown
    Window { id: host; width: 1280; height: 800; visible: true }
    Component { id: pageComponent; Colosseum.ExtensionsStorePage {} }
    Component { id: houseComponent; Colosseum.ExtensionsHousePage {} }
    property var page: null
    property var originalFetch: null

    function init() {
        originalFetch = StoreApi.fetchAll
        StoreApi.fetchAll = function(callback) { callback([], "") }
    }
    function cleanup() {
        if (page) page.destroy()
        page = null
        StoreApi.fetchAll = originalFetch
    }
    function test_house_banner_uses_installed_manifest_and_local_fallback() {
        page = houseComponent.createObject(host.contentItem)
        verify(page !== null)
        page.installedRows = []
        compare(page.extensionBackground("starwars", "local.jpg"), "local.jpg")
        page.installedRows = [{id: "starwars", manifest: {background: "https://example.test/banner.jpg"}}]
        compare(page.extensionBackground("starwars", "local.jpg"), "https://example.test/banner.jpg")
        page.installedRows = [{id: "starwars", manifest: {}}]
        compare(page.extensionBackground("starwars", "local.jpg"), "local.jpg")
    }
    function test_single_store_world_preserves_escape_and_back() {
        page = pageComponent.createObject(host.contentItem, {width: host.width, height: host.height})
        verify(page !== null)
        compare(typeof page.section, "undefined")
        var store = findChild(page, "extensionsStoreWorld")
        verify(store !== null && store.visible, "Extensions opens into one store world")
        compare(findChild(page, "extensionsChainPage"), null)
        compare(findChild(page, "extensionsHousePage"), null)
        page.searching = true
        compare(page.requestEscape(), true)
        compare(page.searching, false)
        compare(page.requestEscape(), false)
    }
}
