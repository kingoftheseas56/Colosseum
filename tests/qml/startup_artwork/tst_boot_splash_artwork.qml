import QtQuick
import QtQuick.Window
import QtTest
import "../../../qml/TheatreApi.js" as TheatreApi
import "../../../qml/Catalog.js" as Catalog

TestCase {
    id: testCase
    name: "BootSplashArtwork"
    when: windowShown

    property var catalogArrays: []
    property var catalogSnapshots: []
    property var pendingCallbacks: []
    property var requestedUrls: []
    property var splash: null
    property int finishCount: 0

    Window {
        id: testWindow
        width: 640
        height: 480
        visible: true
        color: "#08080d"
        Item { id: splashHost; anchors.fill: parent }
    }

    function initTestCase() {
        // Keep the test offline: the actual component uses its real catalogue code,
        // but its shared static data arrays are temporarily empty so no Image can fetch.
        catalogArrays = [Catalog.featured, Catalog.topManga, Catalog.topComics,
                         Catalog.genresManga, Catalog.theatreFeatured,
                         Catalog.theatreTopMovies, Catalog.theatreTopSeries,
                         Catalog.theatreGenres, Catalog.biblioFeatured,
                         Catalog.biblioTop, Catalog.biblioGenres]
        catalogSnapshots = []
        for (var i = 0; i < catalogArrays.length; ++i) {
            catalogSnapshots.push(catalogArrays[i].slice())
            catalogArrays[i].splice(0, catalogArrays[i].length)
        }
    }

    function cleanupTestCase() {
        for (var i = 0; i < catalogArrays.length; ++i) {
            catalogArrays[i].splice(0, catalogArrays[i].length)
            for (var j = 0; j < catalogSnapshots[i].length; ++j)
                catalogArrays[i].push(catalogSnapshots[i][j])
        }
        TheatreApi.resetRequestAdapter()
        TheatreApi.resetLiveCaches()
    }

    function init() {
        requestedUrls = []
        pendingCallbacks = []
        finishCount = 0
        splash = null
        TheatreApi.resetRequestAdapter()
        TheatreApi.resetLiveCaches()
        TheatreApi.setRequestAdapter(function(url, done) {
            requestedUrls.push(String(url))
            pendingCallbacks.push(done)
            return function() {}
        })
    }

    function cleanup() {
        if (splash)
            splash.destroy()
        splash = null
        TheatreApi.resetRequestAdapter()
        TheatreApi.resetLiveCaches()
    }

    function createSplash(initialProperties) {
        var componentUrl = Qt.resolvedUrl("../../../qml/BootSplash.qml")
        var component = Qt.createComponent(componentUrl)
        tryCompare(component, "status", Component.Ready, 3000)
        if (component.status !== Component.Ready) {
            fail("could not load production BootSplash.qml: " + component.errorString())
            return null
        }
        var props = { visible: false }
        for (var key in initialProperties)
            props[key] = initialProperties[key]
        var item = component.createObject(splashHost, props)
        if (item)
            item.finished.connect(function() { testCase.finishCount += 1 })
        return item
    }

    function deliverEmptyTheatreResponse() {
        var callbacks = pendingCallbacks.slice()
        for (var i = 0; i < callbacks.length; ++i)
            callbacks[i]({ metas: [] })
    }

    function countPrefetchImages(root) {
        var count = 0
        function visit(item) {
            if (!item)
                return
            if (item.sourceSize !== undefined
                    && Number(item.sourceSize.width) === 264
                    && Number(item.sourceSize.height) === 392)
                ++count
            var children = item.children || []
            for (var i = 0; i < children.length; ++i)
                visit(children[i])
        }
        visit(root)
        return count
    }

    function countRunningTimers(root) {
        var count = 0
        function visit(object) {
            if (!object)
                return
            var resources = object.resources || []
            for (var i = 0; i < resources.length; ++i) {
                var resource = resources[i]
                if (resource && resource.interval !== undefined
                        && resource.running !== undefined && resource.running)
                    ++count
                visit(resource)
            }
            var children = object.children || []
            for (var j = 0; j < children.length; ++j)
                visit(children[j])
        }
        visit(root)
        return count
    }

    function test_waitForArtwork_false_finishes_without_catalogue_work() {
        splash = createSplash({ waitForArtwork: false })
        verify(splash !== null,
               "production BootSplash must accept the waitForArtwork opt-out property")
        if (!splash)
            return
        tryCompare(splash, "completed", true, 1000)
        compare(finishCount, 1, "the asynchronous opt-out emits finished once")
        compare(requestedUrls.length, 0, "the opt-out arm must not call TheatreApi.loadTheatre")
        compare(splash.prefetchStarted, false, "the opt-out arm must not start prefetch")
        compare(splash.urls.length, 0, "the opt-out arm must not collect catalogue URLs")
        compare(countPrefetchImages(splash), 0, "the opt-out arm must not create hidden image warmers")
        compare(countRunningTimers(splash), 0, "the opt-out arm must not start either artwork timeout")
        splash.complete()
        splash.complete()
        compare(finishCount, 1, "repeated complete calls leave the asynchronous signal count at one")
    }

    function test_default_waits_for_empty_art_callback_then_finishes_once() {
        splash = createSplash({})
        verify(splash !== null, "production BootSplash instance must load")
        if (!splash)
            return
        compare(requestedUrls.length, 3, "default artwork wait starts the three Theatre catalog requests")
        compare(finishCount, 0, "the splash must wait while Theatre responses are pending")
        verify(countRunningTimers(splash) > 0, "the real API timeout is running during the default wait")
        compare(splash.completed, false)
        deliverEmptyTheatreResponse()
        compare(splash.completed, true, "empty static and Theatre art completes the wait")
        compare(finishCount, 1, "the finished signal fires once when the empty response arrives")
        compare(splash.urls.length, 0)
        compare(countPrefetchImages(splash), 0, "empty art produces no hidden image delegates")
        splash.complete()
        splash.complete()
        compare(finishCount, 1, "later completion attempts do not emit finished again")
    }

    function test_prefetch_disabled_diagnostic_still_waits_for_completion() {
        splash = createSplash({ prefetchEnabled: false })
        verify(splash !== null, "production BootSplash diagnostic instance must load")
        if (!splash)
            return
        compare(requestedUrls.length, 3, "diagnostic mode retains its Theatre request")
        compare(finishCount, 0, "diagnostic mode has not completed while the API callback is pending")
        deliverEmptyTheatreResponse()
        compare(splash.prefetchStarted, true, "empty callback still enters the diagnostic prefetch phase")
        compare(splash.completed, false,
                "prefetchEnabled=false preserves its original timeout/completion behavior")
        compare(finishCount, 0)
        splash.complete()
        compare(splash.completed, true)
        compare(finishCount, 1)
        splash.complete()
        compare(finishCount, 1, "the completion signal remains idempotent")
    }
}
