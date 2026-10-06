import QtQuick 2.15
import QtTest 1.3
import "../../qml" as App

TestCase {
    id: tc
    name: "Arc63D1Readiness"
    width: 1280
    height: 720
    when: windowShown

    property int postReadyFrames: 0
    property bool countFrames: false
    property Item activeWorld: null

    function duneInstalled(bannerSource) {
        return [{
            extensionId: "diag.arc63.dune",
            name: "Dune",
            banner: bannerSource,
            logo: ""
        }]
    }

    function findNamed(item, name) {
        if (!item) return null
        if (item.objectName === name) return item
        for (let i = 0; i < item.children.length; ++i) {
            const hit = findNamed(item.children[i], name)
            if (hit) return hit
        }
        return null
    }

    function duneBanner() {
        return findNamed(tc.activeWorld, "universesBanner_dune")
    }

    function revealDune() {
        const banner = duneBanner()
        if (!banner || !tc.activeWorld) return false
        const flick = tc.activeWorld.pageFlickable
        const y = banner.mapToItem(flick.contentItem, 0, 0).y
        flick.contentY = Math.max(0, Math.min(y - 24, flick.contentHeight - flick.height))
        return true
    }

    function surfaceState() {
        const banner = duneBanner()
        if (!banner) return "unavailable"
        if (banner.directorAssetState === "unavailable") return "unavailable"
        if (banner.directorAssetState === "partial") return "partial"
        if (!banner.directorAssetsReady) return "loading"
        return postReadyFrames >= 2 ? "settled" : "loading"
    }

    function trace(label) {
        const banner = duneBanner()
        console.log("[D1TRACE] " + JSON.stringify({
            label: label,
            surface: "UniversesWorld/Dune",
            state: surfaceState(),
            required: banner ? banner.directorRequiredAssetCount : 0,
            ready: banner ? banner.directorReadyAssetCount : 0,
            failed: banner ? banner.directorFailedAssetCount : 0,
            bannerStatus: banner ? banner.directorBannerStatus : "missing",
            markStatus: banner ? banner.directorMarkStatus : "missing",
            framesAfterReady: postReadyFrames,
            source: banner ? banner.directorBannerSource : ""
        }))
    }

    Component {
        id: worldComponent
        App.UniversesWorld {
            width: tc.width
            height: tc.height
            facet: "books"
        }
    }

    Component {
        id: shelvesComponent
        App.ShelvesUniversePage {
            width: tc.width
            height: tc.height
            universeName: "James Bond"
        }
    }

    Connections {
        target: tc.Window.window
        function onFrameSwapped() {
            if (!tc.countFrames) return
            const banner = tc.duneBanner()
            if (!banner || !banner.directorAssetsReady) {
                tc.postReadyFrames = 0
                return
            }
            tc.postReadyFrames += 1
        }
    }

    function test_real_surface_rejects_blank_then_settles_on_ready_art() {
        tc.activeWorld = worldComponent.createObject(tc, {
            installed: tc.duneInstalled(Qt.resolvedUrl("../../qml/Universes.js"))
        })
        verify(tc.activeWorld !== null)
        tryVerify(function() { return tc.duneBanner() !== null }, 3000)
        verify(tc.revealDune())
        const bad = tc.duneBanner()
        verify(bad.directorAssetState !== "ready")
        compare(bad.directorAssetsReady, false)
        verify(tc.surfaceState() === "loading" || tc.surfaceState() === "partial")
        tc.trace("required-art-unready")
        tc.activeWorld.destroy()
        tc.activeWorld = null

        tc.postReadyFrames = 0
        tc.countFrames = true
        tc.activeWorld = worldComponent.createObject(tc, {
            installed: tc.duneInstalled(Qt.resolvedUrl("../../assets/icons/lucide/chevron-right.svg"))
        })
        verify(tc.activeWorld !== null)
        tryVerify(function() { return tc.duneBanner() !== null }, 3000)
        verify(tc.revealDune())
        tryVerify(function() {
            const banner = tc.duneBanner()
            return banner !== null && banner.directorAssetState === "ready"
        }, 5000)
        tryVerify(function() { return tc.surfaceState() === "settled" }, 3000)

        const good = tc.duneBanner()
        compare(good.directorFailedAssetCount, 0)
        compare(good.directorReadyAssetCount, good.directorRequiredAssetCount)
        verify(tc.postReadyFrames >= 2)
        tc.trace("all-required-art-presented")
        tc.countFrames = false
        tc.activeWorld.destroy()
        tc.activeWorld = null
    }

    function test_representative_shelves_surface_enumerates_real_curated_art() {
        const page = shelvesComponent.createObject(tc)
        verify(page !== null)
        tryVerify(function() {
            return page.uni && page.uni.name === "James Bond" && page.activeShelves.length > 0
        }, 1000)
        compare(page.objectName, "shelvesUniversePage")

        let firstTile = null
        tryVerify(function() {
            firstTile = tc.findNamed(page, "shelvesTile_films_0")
            return firstTile !== null
        }, 1000)
        verify(firstTile.directorArtSource.indexOf("tt0055928") >= 0)
        verify(firstTile.directorArtStatus !== "unavailable")
        console.log("[D1TRACE] " + JSON.stringify({
            label: "shelves-cohort-enumerated",
            surface: "ShelvesUniversePage/James Bond",
            state: firstTile.directorArtStatus === "error" ? "partial" : "loading",
            universe: page.uni.name,
            shelfCount: page.activeShelves.length,
            firstAssetKey: firstTile.directorAssetKey,
            firstAssetStatus: firstTile.directorArtStatus,
            firstAssetSource: firstTile.directorArtSource,
            wallpaperStatus: page.directorWallpaperStatus,
            wallpaperSource: page.directorWallpaperSource
        }))
        page.destroy()
    }
}
