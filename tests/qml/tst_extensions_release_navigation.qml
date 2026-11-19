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
    QtObject {
        id: repositoryStore
        property var repositories: []
        property bool busy: false
        property string error: ""
        signal changed()
    }


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
    function test_poster_title_stays_put_and_activates_current_tribute() {
        const tributes = [
            { extensionId: "first", name: "First Tribute" },
            { extensionId: "second", name: "Second Tribute" }
        ]
        page = pageComponent.createObject(host.contentItem, {
            width: host.width, height: host.height, tributes: tributes, reducedMotion: true
        })
        verify(page !== null)
        const carousel = findChild(page, "extensionsTributeCarousel")
        const title = findChild(page, "extensionsPosterTitle")
        verify(carousel && title)
        const titlePosition = title.mapToItem(page, 0, 0)
        compare(title.text, "Extensions")
        var activated = ""
        page.tributeRequested.connect(tribute => { activated = tribute.extensionId })
        carousel.setCurrentIndex(1)
        tryVerify(() => {
            const action = findChild(carousel, "extensionsFeaturedTribute")
            return action && action.accessibleName === "Open Second Tribute tribute"
        })
        const action = findChild(carousel, "extensionsFeaturedTribute")
        verify(action.activate())
        compare(activated, "second")
        compare(title.mapToItem(page, 0, 0), titlePosition)
        compare(title.text, "Extensions")
    }
    function test_nuvio_drawer_preserves_link_and_respects_dock_clearance() {
        page = pageComponent.createObject(host.contentItem, {
            width: host.width, height: host.height,
            bottomInset: 76, nuvioRepositoryStore: repositoryStore
        })
        verify(page !== null)
        const trigger = findChild(page, "extensionsNuvioRepositoriesButton")
        const drawer = findChild(page, "extensionsNuvioRepositoryDrawer")
        const scrollbar = findChild(page, "extensionsWorldScrollBar")
        verify(trigger && drawer && scrollbar)
        host.requestActivate()
        tryCompare(host, "active", true)
        verify(scrollbar.prominent && !scrollbar.glassStyle)
        compare(drawer.visible, false)
        verify(trigger.activate())
        tryCompare(drawer, "opened", true)
        compare(drawer.y + drawer.height, host.height - 76)
        const repoBar = findChild(drawer.contentItem, "nuvioRepositoriesBar")
        const address = findChild(repoBar, "nuvioRepositoryAddress")
        verify(repoBar && address)
        tryVerify(() => address.activeFocus, 2000,
                  "repository field focus, enabled=" + address.enabled
                  + " visible=" + address.visible
                  + " focus=" + address.focus
                  + " active window item=" + host.activeFocusItem)
        repoBar.offerLink("https://example.test/repository.json")
        compare(page.requestEscape(), true)
        tryCompare(drawer, "visible", false)
        verify(trigger.activate())
        tryCompare(drawer, "opened", true)
        compare(address.text, "https://example.test/repository.json")
        const close = findChild(repoBar, "closeNuvioRepositories")
        verify(close && close.activate())
        tryCompare(drawer, "visible", false)
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
