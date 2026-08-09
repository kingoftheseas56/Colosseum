import QtQuick 2.15
import QtQuick.Window 2.15
import QtTest 1.3
import "../../qml/guide" as Guide

TestCase {
    name: "GuideCatalog"

    Window {
        id: testWindow
        width: 1280
        height: 720
        visible: true
    }

    Component { id: catalogComponent; Guide.GuideCatalog {} }
    Component { id: progressComponent; Guide.GuideProgress { settingsCategory: "guide-test" } }

    function createProgress() {
        var progress = progressComponent.createObject(testWindow)
        verify(progress !== null)
        return progress
    }

    function cleanup() {
        var progress = createProgress()
        progress.resetJourney()
        progress.destroy()
    }

    // Break caught: a catalog path that skips the publication gate exposes an unverified draft.
    function test_catalog_hides_non_published_lessons() {
        var catalog = catalogComponent.createObject(testWindow)
        verify(catalog !== null)
        compare(catalog.find("fixture.published").id, "fixture.published")
        compare(catalog.find("fixture.draft"), null)
        compare(catalog.search("draft secret", "home").length, 0)
        compare(catalog.section("start").length, 1)
        catalog.destroy()
    }

    // Break caught: duplicate or malformed completion values corrupt the stable journey-ID set.
    function test_progress_is_idempotent_and_ignores_blank_step_ids() {
        var progress = createProgress()
        progress.resetJourney()
        progress.complete("open-guide")
        progress.complete("open-guide")
        progress.complete("  ")
        progress.complete(null)
        compare(progress.completedSteps, ["open-guide"])
        progress.destroy()
    }

    // Break caught: writing only in memory loses journey progress when the Guide surface is recreated.
    function test_progress_survives_recreation_and_reset_clears_the_store() {
        var first = createProgress()
        first.resetJourney()
        first.complete("open-guide")
        first.destroy()

        var recreated = createProgress()
        compare(recreated.completedSteps, ["open-guide"])
        recreated.resetJourney()
        recreated.destroy()

        var cleared = createProgress()
        compare(cleared.completedSteps, [])
        cleared.destroy()
    }
}
