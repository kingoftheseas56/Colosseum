import QtQuick
import QtQuick.Window
import QtTest
import "../../../qml" as Colosseum
import "../../../qml/ExtensionsStoreApi.js" as StoreApi

TestCase {
    id: tests
    name: "ExtensionsArtworkViewport"
    when: windowShown

    Window {
        id: host
        width: 1280
        height: 800
        visible: true
        property bool androidHost: true
    }

    Component { id: railComponent; Colosseum.ExtensionsStoreRail {} }
    Component { id: pageComponent; Colosseum.ExtensionsStorePage {} }

    property var rail: null
    property var page: null
    property var originalFetch: null
    property var pageCatalogue: []
    readonly property url localLogo: Qt.resolvedUrl("../../../assets/universes/star-wars/valo.jpg")

    function init() {
        originalFetch = StoreApi.fetchAll
        StoreApi.fetchAll = function(done) { done(pageCatalogue, "") }
        host.width = 1280
        host.height = 800
        host.androidHost = true
    }

    function cleanup() {
        if (rail) rail.destroy()
        if (page) page.destroy()
        rail = null
        page = null
        pageCatalogue = []
        StoreApi.fetchAll = originalFetch
    }

    function addon(slug, name, categories, resources, types) {
        return {
            id: slug,
            slug: slug,
            name: name,
            title: name,
            logo: String(localLogo),
            description: "Local artwork fixture for " + name + ".",
            stars: 100,
            categories: categories || [],
            resources: resources || [],
            types: types || ["movie"],
            manifestUrl: "https://example.test/manifest/" + slug,
            configureUrl: "",
            setupRequired: false,
            configurable: false,
            adult: false
        }
    }

    function railItems(count) {
        var items = []
        for (var i = 0; i < count; ++i)
            items.push(addon("rail-" + i, "Rail item " + i, ["movies"], ["stream"], ["movie"]))
        return items
    }

    function pageItems() {
        return [
            addon("essential", "OpenSubtitles v3", ["subtitles"], ["subtitles"], ["movie"]),
            addon("play-source", "Play source", ["movies"], ["stream"], ["movie"]),
            addon("catalogue", "Catalogue", ["movies"], ["catalog"], ["movie"]),
            addon("anime-source", "Anime source", ["anime"], ["stream"], ["anime"]),
            addon("cinema-catalogue", "World cinema", ["asian drama"], ["catalog"], ["movie"]),
            addon("subtitle-source", "Subtitle source", ["subtitles"], ["subtitles"], ["movie"]),
            addon("metadata-catalogue", "Better titles", ["metadata"], ["catalog"], ["movie"])
        ]
    }

    function findDescendant(node, predicate) {
        if (!node) return null
        if (predicate(node)) return node
        var children = node.children || []
        for (var i = 0; i < children.length; ++i) {
            var found = findDescendant(children[i], predicate)
            if (found) return found
        }
        return null
    }

    function imageWithSize(card, size) {
        return findDescendant(card, function(node) {
            return typeof node.sourceSize !== "undefined"
                    && node.sourceSize && node.sourceSize.width === size
        })
    }

    function washEffect(card) {
        return findDescendant(card, function(node) {
            return typeof node.blurEnabled === "boolean"
                    && typeof node.source !== "undefined"
                    && typeof node.visible === "boolean"
        })
    }

    function railForFlickable(flick) {
        for (var node = flick; node; node = node.parent) {
            if (typeof node.limitArtworkToViewport === "boolean"
                    && typeof node.artworkActive === "boolean")
                return node
        }
        return null
    }

    function test_inactive_rail_releases_logo_images_and_keeps_cards() {
        var items = railItems(6)
        rail = railComponent.createObject(host.contentItem, {
            width: 620,
            title: "Artwork fixture",
            items: items,
            phoneLayout: false,
            automationId: "extensionsArtworkRail"
        })
        verify(rail !== null)
        var first = findChild(rail, "extensionsCard_rail-0")
        var last = findChild(rail, "extensionsCard_rail-5")
        tryVerify(function() { return first !== null && last !== null }, 3000)
        var crisp = imageWithSize(first, 192)
        var wash = imageWithSize(first, 160)
        var effectBefore = washEffect(first)
        verify(crisp !== null && wash !== null && effectBefore !== null,
               "the actual card exposes its two logo Image nodes and MultiEffect")
        tryCompare(crisp, "status", Image.Ready, 5000)
        tryCompare(wash, "status", Image.Ready, 5000)
        tryVerify(function() { return effectBefore.visible }, 5000,
                  "the local wash is active before viewport deactivation")
        verify(String(crisp.source).length > 0, "the local fixture logo is loaded before deactivation")

        if (typeof rail.limitArtworkToViewport === "boolean")
            rail.limitArtworkToViewport = true
        if (typeof rail.artworkActive === "boolean")
            rail.artworkActive = false
        effectBefore = null // The production Loader may destroy this MultiEffect on deactivation.

        tryVerify(function() { return String(crisp.source).length === 0 }, 1500,
                  "an inactive Android rail clears the crisp logo source")
        tryVerify(function() { return String(wash.source).length === 0 }, 1500,
                  "an inactive Android rail clears the wash source")
        var effectAfter = washEffect(first)
        if (effectAfter !== null) {
            compare(effectAfter.visible, false, "a retained inactive wash effect must be hidden")
            compare(effectAfter.source, null, "a retained inactive wash effect must release its source")
            compare(effectAfter.blurEnabled, false, "a retained inactive wash effect must disable blur")
        }
        for (var i = 0; i < items.length; ++i)
            verify(findChild(rail, "extensionsCard_" + items[i].slug) !== null,
                   "artwork gating retains card identity " + items[i].slug)
    }

    function test_wide_scroll_moves_artwork_window_without_rebuilding_cards() {
        var items = railItems(8)
        rail = railComponent.createObject(host.contentItem, {
            width: 700,
            title: "Scrollable artwork fixture",
            items: items,
            phoneLayout: false,
            automationId: "extensionsArtworkScrollRail"
        })
        verify(rail !== null)
        tryVerify(function() { return findChild(rail, "extensionsCard_rail-7") !== null }, 3000)
        verify(typeof rail.limitArtworkToViewport === "boolean",
               "the actual rail exposes its artwork residency switch")
        verify(typeof rail.artworkActive === "boolean",
               "the actual rail exposes its vertical activity input")
        rail.limitArtworkToViewport = true
        rail.artworkActive = true

        var first = findChild(rail, "extensionsCard_rail-0")
        var later = findChild(rail, "extensionsCard_rail-4")
        var firstLogo = imageWithSize(first, 192)
        var laterLogo = imageWithSize(later, 192)
        var flick = findChild(rail, "extensionsArtworkScrollRail")
        verify(firstLogo !== null && laterLogo !== null && flick !== null)
        tryCompare(firstLogo, "status", Image.Ready, 5000)
        verify(String(firstLogo.source).length > 0, "the visible leading card keeps its logo")
        compare(String(laterLogo.source).length, 0,
                "a far card outside the horizontal residency window has no logo source")

        flick.contentX = 1100
        tryCompare(flick, "contentX", 1100, 1500)
        tryVerify(function() { return String(firstLogo.source).length === 0 }, 1500,
                  "scrolling releases the leading card's logo")
        tryVerify(function() { return String(laterLogo.source).length > 0 }, 1500,
                  "scrolling admits the newly visible card's logo")
        for (var i = 0; i < items.length; ++i)
            verify(findChild(rail, "extensionsCard_" + items[i].slug) !== null,
                   "horizontal artwork virtualization retains card identity " + items[i].slug)
    }

    function test_store_page_tracks_actual_world_vertical_viewport() {
        host.width = 412
        host.height = 560
        host.androidHost = true
        pageCatalogue = pageItems()
        page = pageComponent.createObject(host.contentItem, {width: host.width, height: host.height})
        verify(page !== null)
        var world = findChild(page, "extensionsStoreWorld")
        verify(world !== null)
        var flick = world.pageFlickable
        verify(flick !== null, "the production WorldPage owns the vertical scroll viewport")
        tryVerify(function() { return flick.contentHeight > flick.height }, 5000,
                  "fixture rows create real vertical overflow in the Extensions world")

        var farFlick = findChild(world, "extensionsStoreRail_6")
        verify(farFlick !== null, "the Better titles row has its production Flickable")
        var farRail = railForFlickable(farFlick)
        verify(farRail !== null, "the production row rail owns its viewport activity")
        compare(farRail.limitArtworkToViewport, true,
                "the Android-hosted store enables bounded artwork residency")
        var farCard = findChild(farRail, "extensionsCard_metadata-catalogue")
        verify(farCard !== null)
        var farLogo = imageWithSize(farCard, 192)
        verify(farLogo !== null)
        verify(farRail.mapToItem(world, 0, 0).y > world.viewportHeight,
               "the final row begins below the actual initial viewport")
        compare(farRail.artworkActive, false,
                "rows outside the initial WorldPage viewport are inactive")
        compare(String(farLogo.source).length, 0,
                "offscreen page rows do not retain their logo source")

        var retainedCard = farCard
        flick.contentY = Math.max(0, flick.contentHeight - flick.height)
        tryVerify(function() { return world.viewportContentY > 0 }, 2000)
        tryVerify(function() { return farRail.artworkActive }, 2000,
                  "scrolling the production WorldPage activates its newly visible row")
        verify(findChild(farRail, "extensionsCard_metadata-catalogue") === retainedCard,
               "vertical scrolling retains the original card object")
        tryVerify(function() { return String(farLogo.source).length > 0 }, 1500,
                  "the visible row reacquires its local logo")
    }
}
