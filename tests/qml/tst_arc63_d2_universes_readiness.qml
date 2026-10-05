import QtQuick 2.15
import QtQuick.Window 2.15
import QtTest 1.3
import "../../qml" as App

TestCase {
    id: tc
    name: "Arc63D2UniversesReadiness"
    width: 1280
    height: 720
    when: windowShown

    function duneInstalled(source) {
        return [{
            extensionId: "diag.arc63.dune",
            name: "Dune",
            banner: source,
            logo: ""
        }]
    }

    function duneIndex(world) {
        for (let i = 0; i < world.shown.length; ++i) {
            if (String(world.shown[i].name || "") === "Dune")
                return i
        }
        return -1
    }

    function narrowToDune(world) {
        const index = duneIndex(world)
        verify(index >= 0)
        world.directorShotFirstBannerIndex = index
        world.directorShotLastBannerIndex = index
        return index
    }

    function createDuneHost(source, windowVisible, includeTribute) {
        const host = hostComponent.createObject(null, {
            installedSeed: duneInstalled(source),
            includeTributeSeed: includeTribute
        })
        verify(host !== null)
        narrowToDune(host.world)
        host.visible = windowVisible
        return host
    }

    function destroyHost(host) {
        host.visible = false
        host.destroy()
    }

    Component {
        id: hostComponent
        Window {
            id: hostWindow
            width: tc.width
            height: tc.height
            visible: false
            property alias world: universes
            property var installedSeed: []
            property string facetSeed: "books"
            property bool includeTributeSeed: false

            App.UniversesWorld {
                id: universes
                anchors.fill: parent
                facet: hostWindow.facetSeed
                installed: hostWindow.installedSeed
                directorIncludeTributeArt: hostWindow.includeTributeSeed
            }
        }
    }

    function test_zero_member_construction_never_settles() {
        const host = hostComponent.createObject(null, {
            facetSeed: "__no_arc63_members__",
            installedSeed: [],
            includeTributeSeed: false
        })
        verify(host !== null)
        host.visible = true
        compare(host.world.directorRequiredCount, 0)
        compare(host.world.directorReadyCount, 0)
        compare(host.world.directorFailedCount, 0)
        compare(host.world.directorReadinessState, "unavailable")
        verify(host.world.directorReadinessState !== "settled")
        destroyHost(host)
    }

    function test_required_image_error_is_partial() {
        const badSource = Qt.resolvedUrl("../../qml/Universes.js")
        const host = createDuneHost(badSource, true, false)
        const world = host.world
        tryVerify(function() { return world.directorFailedCount === 1 }, 5000)
        compare(world.directorRequiredCount, 1)
        compare(world.directorReadyCount, 0)
        compare(world.directorFailedCount, 1)
        compare(world.directorReadinessState, "partial")
        verify(world.directorReadinessDiagnostics.length > 0)
        compare(world.directorReadinessDiagnostics[0].reason, "image-error")
        destroyHost(host)
    }

    function test_ready_pixels_still_need_two_presented_frames() {
        const source = Qt.resolvedUrl("../../assets/icons/lucide/chevron-right.svg")
        const host = createDuneHost(source, false, false)
        const world = host.world

        tryVerify(function() {
            return world.directorRequiredCount === 1
                    && world.directorReadyCount === 1
                    && world.directorFailedCount === 0
        }, 5000)
        compare(world.directorPresentedFrameCount, 0)
        compare(world.directorReadinessState, "unavailable")

        host.visible = true
        compare(world.directorPresentedFrameCount, 0)
        compare(world.directorReadinessState, "loading")
        verify(world.directorReadinessState !== "settled")

        tryVerify(function() { return world.directorPresentedFrameCount >= 1 }, 3000)
        if (world.directorPresentedFrameCount < 2)
            compare(world.directorReadinessState, "loading")
        tryCompare(world, "directorReadinessState", "settled", 5000)
        verify(world.directorPresentedFrameCount >= 2)
        destroyHost(host)
    }

    function test_source_change_advances_epoch_and_rearms_barrier() {
        const right = Qt.resolvedUrl("../../assets/icons/lucide/chevron-right.svg")
        const left = Qt.resolvedUrl("../../assets/icons/lucide/chevron-left.svg")
        const host = createDuneHost(right, true, false)
        const world = host.world

        tryCompare(world, "directorReadinessState", "settled", 5000)
        const settledEpoch = world.directorReadinessEpoch
        compare(world.directorRequiredCount, 1)
        compare(world.directorReadyCount, 1)

        host.installedSeed = duneInstalled(left)
        verify(world.directorReadinessEpoch > settledEpoch)
        compare(world.directorPresentedFrameCount, 0)
        verify(world.directorReadinessState !== "settled")

        tryVerify(function() {
            return world.directorReadyCount === 1 && world.directorFailedCount === 0
        }, 5000)
        tryCompare(world, "directorReadinessState", "settled", 5000)
        verify(world.directorPresentedFrameCount >= 2)
        destroyHost(host)
    }

    function test_active_tribute_and_banner_are_both_required() {
        const source = Qt.resolvedUrl("../../assets/icons/lucide/chevron-right.svg")
        const host = createDuneHost(source, false, true)
        const world = host.world

        tryVerify(function() {
            return world.directorRequiredCount === 2
                    && world.directorReadyCount === 2
                    && world.directorFailedCount === 0
        }, 5000)
        compare(world.directorReadinessState, "unavailable")

        host.visible = true
        compare(world.directorReadinessState, "loading")
        tryCompare(world, "directorReadinessState", "settled", 5000)
        compare(world.directorRequiredCount, 2)
        compare(world.directorReadyCount, 2)
        compare(world.directorFailedCount, 0)
        destroyHost(host)
    }
}
