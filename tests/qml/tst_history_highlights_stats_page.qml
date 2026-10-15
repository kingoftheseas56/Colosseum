import QtQuick 2.15
import QtQuick.Window 2.15
import QtTest 1.3
import "../../qml" as Colosseum

TestCase {
    name: "HistoryHighlightsStatsPage"
    when: windowShown

    Window {
        id: testWindow
        width: 1280
        height: 720
        visible: true
    }

    Component { id: pageComponent; Colosseum.HistoryHighlightsStatsPage {} }
    Component {
        id: activityComponent
        QtObject {
            property int revision: 1
            function earliestActivityMonth() { return "2026-08" }
            function projectMonth(monthKey) {
                if (monthKey === "2026-08") {
                    return { month: monthKey, watchSeconds: 3600, listenSeconds: 0,
                        pagesRead: 20, completedCount: 1, activeDays: 2,
                        highlights: [{ role: "theatre", title: "Dune", watchSeconds: 3600 }],
                        recentActivity: [{ localDate: "2026-08-12", lastAtMs: 12,
                            sessionId: "s1", world: "theatre", kind: "movie",
                            titleKey: "theatre:dune", itemKey: "dune", title: "Dune",
                            watchSeconds: 3600, completed: true }] }
                }
                return { month: monthKey, watchSeconds: 0, listenSeconds: 0,
                    pagesRead: 0, completedCount: 0, activeDays: 0,
                    highlights: [], recentActivity: [] }
            }
        }
    }
    Component {
        id: nativeSequenceActivityComponent
        QtObject {
            property int revision: 1
            function earliestActivityMonth() { return "2026-08" }
            function sequence(value) { return ({ "0": value, length: 1 }) }
            function projectMonth(monthKey) {
                if (monthKey === "2026-08") {
                    return { month: monthKey, watchSeconds: 3600, listenSeconds: 0,
                        pagesRead: 20, completedCount: 1, activeDays: 2,
                        highlights: sequence({ role: "theatre", title: "Dune",
                            watchSeconds: 3600 }),
                        recentActivity: sequence({ localDate: "2026-08-12", lastAtMs: 12,
                            sessionId: "s1", world: "theatre", kind: "movie",
                            titleKey: "theatre:dune", itemKey: "dune", title: "Dune",
                            watchSeconds: 3600, completed: true }) }
                }
                return { month: monthKey, watchSeconds: 0, listenSeconds: 0,
                    pagesRead: 0, completedCount: 0, activeDays: 0,
                    highlights: ({ length: 0 }), recentActivity: ({ length: 0 }) }
            }
        }
    }
    property var page: null

    function findChild(root, objectName) {
        if (!root)
            return null
        if (root.objectName === objectName)
            return root
        var kids = root.children || []
        for (var i = 0; i < kids.length; ++i) {
            var found = findChild(kids[i], objectName)
            if (found)
                return found
        }
        return null
    }

    function init() {
        page = pageComponent.createObject(testWindow, {
            "width": testWindow.width,
            "height": testWindow.height,
            "activityStore": null,
            "historyStore": null,
            "trackerModel": null
        })
        verify(page !== null)
        wait(50)
    }

    function cleanup() {
        if (page)
            page.destroy()
        page = null
    }

    function test_three_sections_switch_without_fixture_data() {
        var history = findChild(page, "historySectionTab")
        var highlights = findChild(page, "highlightsSectionTab")
        var stats = findChild(page, "statsSectionTab")
        verify(history !== null)
        verify(highlights !== null)
        verify(stats !== null)
        compare(page.activeSection, "history")

        mouseClick(highlights)
        compare(page.activeSection, "highlights")
        mouseClick(stats)
        compare(page.activeSection, "stats")
        mouseClick(history)
        compare(page.activeSection, "history")
    }

    function test_empty_profile_is_honest() {
        compare(page.projections.length, 0)
        compare(page.historyModel.length, 0)
        compare(page.highlightModel.length, 0)
        compare(page.statsModel.rows.length, 0)
        compare(page.statsModel.summary[0].value, "0m")
    }

    function test_existing_activity_store_populates_history_highlights_and_stats() {
        var activity = activityComponent.createObject(page)
        page.activityStore = activity
        page.selectedMonthKey = "2026-08"
        wait(0)
        compare(page.earliestMonthKey, "2026-08")
        verify(page.projections.length >= 2)
        compare(page.historyModel.length, 1)
        compare(page.highlightModel.length, 1)
        compare(page.statsModel.summary[0].value, "1h 0m")
        compare(page.statsModel.summary[1].value, "20")
    }

    function test_native_sequence_rows_populate_all_sections() {
        var activity = nativeSequenceActivityComponent.createObject(page)
        page.activityStore = activity
        page.selectedMonthKey = "2026-08"
        wait(0)
        compare(page.historyModel.length, 1)
        compare(page.highlightModel.length, 1)
        compare(page.statsModel.rows.length, 1)
    }
}
