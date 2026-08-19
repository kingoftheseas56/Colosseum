// Your Colosseum — layout and interaction regression against the real production QML.
// The narrow-width test exists specifically to prevent the locked-mock regression where
// the highlights shelf kept a desktop-side reservation and left a large empty region.
//
// The objectNames this file locates (yourColosseumPage, yourColosseumPreviousMonth,
// yourColosseumNextMonth, yourColosseumPortraitGrid, yourColosseumMonthPortrait,
// yourColosseumMetricsGrid, yourColosseumMonthShelf, yourColosseumFeatureGrid) are
// present on qml/account/AccountYourColosseumPage.qml.
import QtQuick 2.15
import QtQuick.Window 2.15
import QtTest 1.3
import "../../qml/account" as Account

TestCase {
    id: testCase
    name: "AccountYourColosseum"

    Window {
        id: testWindow
        width: 1200
        height: 900
        visible: true
    }

    Component {
        id: pageComponent
        Account.AccountYourColosseumPage {}
    }

    property var page: null

    SignalSpy {
        id: previousSpy
        target: testCase.page
        signalName: "previousMonthRequested"
    }

    SignalSpy {
        id: nextSpy
        target: testCase.page
        signalName: "nextMonthRequested"
    }

    function byName(root, name) {
        if (!root)
            return null
        if (root.objectName === name)
            return root
        var children = root.children || []
        for (var i = 0; i < children.length; ++i) {
            var found = byName(children[i], name)
            if (found)
                return found
        }
        return null
    }

    function init() {
        page = pageComponent.createObject(testWindow.contentItem, {
            "width": 1080,
            "height": 800,
            "monthName": "August",
            "monthYear": "2026",
            "watchTimeText": "37h 24m",
            "pagesReadText": "1,284",
            "completedText": "9",
            "activeDaysText": "22",
            "highlights": [
                { "title": "Blue Eye Samurai", "label": "Most watched", "value": "11h 18m" },
                { "title": "One Piece", "label": "Most read manga", "value": "412 pages" },
                { "title": "Dune", "label": "Most read book", "value": "268 pages" },
                { "title": "Berserk", "label": "Completed", "value": "3 volumes" }
            ],
            "recentActivity": [
                { "date": "Aug 16", "title": "Blue Eye Samurai", "meta": "Finished Episode 8", "world": "Theatre" }
            ]
        })
        verify(page !== null)
        wait(0)
        previousSpy.clear()
        nextSpy.clear()
    }

    function cleanup() {
        if (page)
            page.destroy()
        page = null
    }

    function test_desktop_portrait_keeps_highlights_beside_month() {
        page.width = 1080
        wait(0)

        verify(page.widePortrait)
        var portrait = byName(page, "yourColosseumMonthPortrait")
        var shelf = byName(page, "yourColosseumMonthShelf")
        verify(portrait !== null)
        verify(shelf !== null)
        compare(Math.round(shelf.y), Math.round(portrait.y))
        compare(Math.round(shelf.x), Math.round(portrait.width + 38))
    }

    function test_narrow_portrait_places_highlights_immediately_below_month() {
        page.width = 900
        wait(0)

        verify(!page.widePortrait)
        var portrait = byName(page, "yourColosseumMonthPortrait")
        var shelf = byName(page, "yourColosseumMonthShelf")
        verify(portrait !== null)
        verify(shelf !== null)
        compare(Math.round(shelf.x), Math.round(portrait.x))
        compare(Math.round(shelf.y), Math.round(portrait.y + portrait.height + 26))
    }

    function test_compact_width_uses_two_column_metrics_and_highlights() {
        page.width = 480
        wait(0)

        verify(page.compactCards)
        var metrics = byName(page, "yourColosseumMetricsGrid")
        var features = byName(page, "yourColosseumFeatureGrid")
        verify(metrics !== null)
        verify(features !== null)
        compare(metrics.columns, 2)
        compare(features.columns, 2)
    }

    function test_month_buttons_emit_requests_without_mutating_month() {
        var previous = byName(page, "yourColosseumPreviousMonth")
        var next = byName(page, "yourColosseumNextMonth")
        verify(previous !== null)
        verify(next !== null)

        mouseClick(previous, previous.width / 2, previous.height / 2)
        mouseClick(next, next.width / 2, next.height / 2)

        compare(previousSpy.count, 1)
        compare(nextSpy.count, 1)
        compare(page.monthName, "August")
        compare(page.monthYear, "2026")
    }
}
