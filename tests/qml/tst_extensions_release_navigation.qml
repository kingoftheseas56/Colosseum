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
    function test_section_index_and_installed_return() {
        page = pageComponent.createObject(host.contentItem, {width: host.width, height: host.height})
        verify(page !== null)
        const heading = findChild(page, "extensionsHeading")
        compare(heading.text, "Extensions")
        const world = findChild(page, "extensionsStoreWorld")
        const index = findChild(page, "extensionsRowIndexSidebar")
        compare(index.automationRows, "Essentials\nMore Sources\nEnrichment\nCatalogues")
        compare(page.essentials.length, 8)
        compare(page.moreSources.length, 5)
        compare(page.enrichment.length, 4)
        compare(page.catalogues.length, 7)
        const railWidth = index.width
        index.collapsed = false
        tryCompare(index, "width", index.openWidth)
        verify(index.width > railWidth)
        index.rowRequested(index.rows[1].target)
        tryCompare(index, "automationCurrentRow", "More Sources")
        const position = world.viewportContentY
        verify(position > 0)
        const installed = findChild(page, "extensionsInstalledButton")
        verify(installed.activate())
        compare(page.managingInstalled, true)
        compare(page.requestEscape(), true)
        compare(page.managingInstalled, false)
        compare(world.viewportContentY, position)
    }

    function test_section_sidebar_stays_pinned_during_page_scroll() {
        page = pageComponent.createObject(host.contentItem, {width: host.width, height: host.height})
        verify(page !== null)
        const world = findChild(page, "extensionsStoreWorld")
        const index = findChild(page, "extensionsRowIndexSidebar")
        const viewport = world.pageFlickable
        tryVerify(() => viewport.contentHeight > viewport.height)
        for (const descriptor of index.rows.slice(1, 3)) {
            const top = descriptor.target.mapToItem(viewport.contentItem, 0, 0).y
            viewport.contentY = top - world.parkGap
            tryCompare(index, "automationCurrentRow", descriptor.title)
            const railY = index.mapToItem(viewport, 0, 0).y
            verify(Math.abs(railY - index.pinLine) < 1,
                   "Sidebar should stay at the pin line, got " + railY)
        }
    }

    function test_banner_opens_the_current_tribute() {
        const tributes = [
            {extensionId: "first", name: "First Tribute"},
            {extensionId: "second", name: "Second Tribute"}
        ]
        page = pageComponent.createObject(host.contentItem, {
            width: host.width, height: host.height, tributes: tributes, reducedMotion: true
        })
        verify(page !== null)
        const carousel = findChild(page, "extensionsTributeCarousel")
        verify(carousel && carousel.visible)
        carousel.setCurrentIndex(1)
        tryVerify(() => {
            const action = findChild(carousel, "extensionsFeaturedTribute")
            return action && action.accessibleName === "Open Second Tribute tribute"
        })
        let opened = ""
        page.tributeRequested.connect(tribute => { opened = tribute.extensionId })
        verify(findChild(carousel, "extensionsFeaturedTribute").activate())
        compare(opened, "second")
    }

    function test_installed_source_labels_use_the_media() {
        page = pageComponent.createObject(host.contentItem, {width: host.width, height: host.height})
        verify(page !== null)
        compare(page.manifestResources({id: "colosseum.well.tankoyomi", resources: ["stream"],
                                        types: ["manga"]}), "Manga chapter sources")
        compare(page.manifestResources({id: "colosseum.well.libgen", resources: ["stream"],
                                        types: ["book"]}), "Book sources")
        compare(page.manifestResources({id: "colosseum.well.indexers", resources: ["stream"],
                                        types: ["comic", "book", "audiobook"]}), "Comic and book torrent sources")
        compare(page.manifestResources({resources: ["stream", "subtitles"],
                                        types: ["movie"]}), "Playback sources · Subtitles")
        compare(page.manifestResources({resources: [{name: "stream", types: ["audiobook"]}],
                                        types: ["book"]}), "Audiobook sources")
    }

    function test_tributes_catalogue_uses_the_installed_roster() {
        const tributes = [{extensionId: "test-tribute", name: "Test Tribute"}]
        page = pageComponent.createObject(host.contentItem, {
            width: host.width, height: host.height, tributes: tributes
        })
        verify(page !== null)
        const entry = page.catalogues.find(addon => addon.slug === "tributes")
        page.browseCatalogue(entry)
        compare(page.seeAllRow.items.length, 1)
        compare(page.seeAllRow.items[0].title, tributes[0].name)
        let opened = ""
        page.tributeRequested.connect(tribute => { opened = tribute.extensionId })
        page.browseCatalogue(page.seeAllRow.items[0])
        compare(opened, tributes[0].extensionId)
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
        compare(trigger.height, 74)
        host.requestActivate()
        tryCompare(host, "active", true)
        verify(scrollbar.prominent && !scrollbar.glassStyle)
        compare(drawer.visible, false)
        verify(trigger.activate())
        tryCompare(drawer, "opened", true)
        compare(drawer.y, trigger.mapToItem(page, 0, trigger.height).y + 12)
        compare(drawer.x + drawer.width, trigger.mapToItem(page, trigger.width, 0).x)
        verify(drawer.y + drawer.height <= host.height - 76)
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
