import QtQuick 2.15
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

    function createDuneWorld(source, visible, includeTribute) {
        const world = worldComponent.createObject(tc, {
            visible: visible,
            installed: duneInstalled(source),
            directorIncludeTributeArt: includeTribute
        })
        verify(world !== null)
        narrowToDune(world)
        return world
    }

    Component {
        id: worldComponent
        App.UniversesWorld {
            width: tc.width
            height: tc.height
            facet: "books"
        }
    }

    function test_zero_member_construction_never_settles() {
        const world = worldComponent.createObject(tc, {
            facet: "__no_arc63_members__",
            installed: [],
            directorIncludeTributeArt: false
        })
        verify(world !== null)
        compare(world.directorRequiredCount, 0)
        compare(world.directorReadyCount, 0)
        compare(world.directorFailedCount, 0)
        compare(world.directorReadinessState, "unavailable")
        verify(world.directorReadinessState !== "settled")
        world.destroy()
    }

    function test_required_image_error_is_partial() {
        const badSource = Qt.resolvedUrl("../../qml/Universes.js")
        const world = createDuneWorld(badSource, true, false)
        tryVerify(function() { return world.directorFailedCount === 1 }, 5000)
        compare(world.directorRequiredCount, 1)
        compare(world.directorReadyCount, 0)
        compare(world.directorFailedCount, 1)
        compare(world.directorReadinessState, "partial")
        verify(world.directorReadinessDiagnostics.length > 0)
        compare(world.directorReadinessDiagnostics[0].reason, "image-error")
        world.destroy()
    }

    function test_ready_pixels_still_need_two_presented_frames() {
        const source = Qt.resolvedUrl("../../assets/icons/lucide/chevron-right.svg")
        const world = createDuneWorld(source, false, false)

        tryVerify(function() {
            return world.directorRequiredCount === 1
                    && world.directorReadyCount === 1
                    && world.directorFailedCount === 0
        }, 5000)
        compare(world.directorPresentedFrameCount, 0)
        compare(world.directorReadinessState, "unavailable")

        world.visible = true
        compare(world.directorPresentedFrameCount, 0)
        compare(world.directorReadinessState, "loading")
        verify(world.directorReadinessState !== "settled")

        tryVerify(function() { return world.directorPresentedFrameCount >= 1 }, 3000)
        if (world.directorPresentedFrameCount < 2)
            compare(world.directorReadinessState, "loading")
        tryCompare(world, "directorReadinessState", "settled", 5000)
        verify(world.directorPresentedFrameCount >= 2)
        world.destroy()
    }

    function test_source_change_advances_epoch_and_rearms_barrier() {
        const right = Qt.resolvedUrl("../../assets/icons/lucide/chevron-right.svg")
        const left = Qt.resolvedUrl("../../assets/icons/lucide/chevron-left.svg")
        const world = createDuneWorld(right, true, false)

        tryCompare(world, "directorReadinessState", "settled", 5000)
        const settledEpoch = world.directorReadinessEpoch
        compare(world.directorRequiredCount, 1)
        compare(world.directorReadyCount, 1)

        world.installed = duneInstalled(left)
        verify(world.directorReadinessEpoch > settledEpoch)
        compare(world.directorPresentedFrameCount, 0)
        verify(world.directorReadinessState !== "settled")

        tryVerify(function() {
            return world.directorReadyCount === 1 && world.directorFailedCount === 0
        }, 5000)
        tryCompare(world, "directorReadinessState", "settled", 5000)
        verify(world.directorPresentedFrameCount >= 2)
        world.destroy()
    }

    function test_active_tribute_and_banner_are_both_required() {
        const source = Qt.resolvedUrl("../../assets/icons/lucide/chevron-right.svg")
        const world = createDuneWorld(source, false, true)

        tryVerify(function() {
            return world.directorRequiredCount === 2
                    && world.directorReadyCount === 2
                    && world.directorFailedCount === 0
        }, 5000)
        compare(world.directorReadinessState, "unavailable")

        world.visible = true
        compare(world.directorReadinessState, "loading")
        tryCompare(world, "directorReadinessState", "settled", 5000)
        compare(world.directorRequiredCount, 2)
        compare(world.directorReadyCount, 2)
        compare(world.directorFailedCount, 0)
        world.destroy()
    }
}
