import QtQuick
import QtTest
import "../../qml" as App

TestCase {
    id: tests
    name: "PhoneShelvesTabs"
    when: windowShown

    Window {
        id: host
        width: 792
        height: 354
        visible: true
    }

    Component {
        id: shelvesComponent
        App.ShelvesUniversePage { }
    }

    property var shelvesPage: null

    function makeShelf(key, title, count) {
        var works = []
        for (var i = 0; i < count; ++i) {
            works.push({
                id: key + "-" + i,
                title: title + " Film " + (i + 1),
                medium: "movie",
                type: "movie",
                year: 2020 + i,
                resolved: true
            })
        }
        return {
            key: key,
            title: title,
            tab: title,
            kind: "movie",
            runs: [{ title: "Fixture run", works: works }]
        }
    }

    function fixtureUniverse() {
        return {
            name: "Phone Shelves Fixture",
            category: "Fixture",
            blurb: "Local geometry fixture.",
            blurbSource: "",
            banner: "",
            wallpaper: "",
            color: "#26364c",
            catalogueNotice: "",
            shelves: [
                makeShelf("fixture-first", "The First Collection Has A Long Natural Label", 2),
                makeShelf("fixture-second", "A Second Shelf With A Long Label", 2),
                makeShelf("fixture-third", "The Third Shelf Is Long Enough To Scroll", 2),
                makeShelf("fixture-last", "The Last Shelf Must Remain Reachable", 2)
            ]
        }
    }

    function init() {
        host.width = 792
        host.height = 354
        shelvesPage = shelvesComponent.createObject(host.contentItem, {
            width: host.width,
            height: host.height,
            uni: fixtureUniverse()
        })
        verify(shelvesPage !== null, "the production Shelves page instantiates with local data")
        wait(30)
    }

    function cleanup() {
        if (shelvesPage)
            shelvesPage.destroy()
        shelvesPage = null
        wait(0)
    }

    function findChild(parent, objectName) {
        if (!parent)
            return null
        if (parent.objectName === objectName)
            return parent
        var children = parent.children || []
        for (var i = 0; i < children.length; ++i) {
            var result = findChild(children[i], objectName)
            if (result)
                return result
        }
        return null
    }

    function findAction(parent, accessibleName, ancestorsVisible) {
        if (!parent)
            return null
        var visible = ancestorsVisible !== false
        try { visible = visible && parent.visible } catch (error) { }
        var name = ""
        try { name = parent.accessibleName } catch (error) { }
        if (visible && name === accessibleName && typeof parent.activate === "function")
            return parent
        var children = parent.children || []
        for (var i = 0; i < children.length; ++i) {
            var result = findAction(children[i], accessibleName, visible)
            if (result)
                return result
        }
        return null
    }

    function rectIn(item, target) {
        var point = item.mapToItem(target, 0, 0)
        return { x: point.x, y: point.y, width: item.width, height: item.height }
    }

    function touchItem(item) {
        var point = item.mapToItem(host.contentItem, item.width / 2, item.height / 2)
        touchEvent(host.contentItem).press(0, host.contentItem, point.x, point.y).commit()
        wait(20)
        touchEvent(host.contentItem).release(0, host.contentItem, point.x, point.y).commit()
        wait(40)
    }

    function setPhoneViewport(width, height) {
        host.width = width
        host.height = height
        shelvesPage.width = width
        shelvesPage.height = height
        if (shelvesPage.hasOwnProperty("androidHost"))
            shelvesPage.androidHost = true
        wait(30)
    }

    function test_phone_shelf_tabs_fit_scroll_and_reveal_by_touch() {
        setPhoneViewport(792, 354)

        var tabs = findChild(shelvesPage, "shelvesTabs")
        verify(tabs !== null, "the production shelf tab bar is present")
        var tabsRect = rectIn(tabs, shelvesPage)
        verify(tabsRect.x >= 0 && tabsRect.x + tabsRect.width <= shelvesPage.width + 1,
               "long shelf labels must stay inside the short landscape viewport; bar x="
               + tabsRect.x + ", width=" + tabsRect.width + ", page width=" + shelvesPage.width)
        fuzzyCompare(tabsRect.y, 22, 0.1)

        if (shelvesPage.hasOwnProperty("phoneLayout"))
            compare(shelvesPage.phoneLayout, true)
        verify(tabs.height >= 48, "phone shelf tabs have a reachable-height container")
        verify(findAction(shelvesPage, "Minimize") === null,
               "Android shelves hide the desktop minimize action")
        verify(findAction(shelvesPage, "Enter fullscreen") === null
               && findAction(shelvesPage, "Exit fullscreen") === null,
               "Android shelves hide the desktop fullscreen action")
        verify(findAction(shelvesPage, "Close Colosseum") === null,
               "Android shelves hide the desktop quit action")

        var scroller = findChild(shelvesPage, "shelvesTabScroller")
        verify(scroller !== null, "phone tabs use a named clipped horizontal viewport")
        compare(scroller.clip, true)
        verify(scroller.width >= 48 && scroller.height >= 48)
        verify(scroller.contentWidth > scroller.width,
               "the fixture's long labels require horizontal scrolling")
        compare(scroller.interactive, true)

        var firstTab = findChild(shelvesPage, "shelvesTab_0")
        var lastShelfTab = findChild(shelvesPage, "shelvesTab_3")
        var lastTab = findChild(shelvesPage, "shelvesTab_4")
        var firstAction = findChild(shelvesPage, "shelvesTabAction_0")
        var lastShelfAction = findChild(shelvesPage, "shelvesTabAction_3")
        var countLabel = findChild(shelvesPage, "shelvesTabCount_0")
        verify(firstTab && lastShelfTab && lastTab && firstAction && lastShelfAction,
               "all fixture shelf tabs have stable geometry and touch targets")
        verify(countLabel && countLabel.visible,
               "numeric shelf counts remain visible in the phone tab")
        var countRect = rectIn(countLabel, firstTab)
        verify(countRect.x >= 0 && countRect.x + countRect.width <= firstTab.width + 1,
               "the phone tab width includes its label and numeric count")
        for (var index = 0; index < 5; ++index) {
            var action = findChild(shelvesPage, "shelvesTabAction_" + index)
            verify(action && action.width >= 48 && action.height >= 48,
                   "phone shelf tab " + index + " has at least a 48dp touch target")
        }

        scroller.contentX = 0
        wait(10)
        var firstRect = rectIn(firstTab, scroller)
        verify(firstRect.x >= -1 && firstRect.x + firstRect.width <= scroller.width + 1,
               "the first shelf tab is reachable at the start of the strip")
        scroller.contentX = Math.max(0, scroller.contentWidth - scroller.width)
        wait(20)
        verify(scroller.contentX > 0, "the horizontal scroll range reaches later shelf tabs")
        var lastRect = rectIn(lastTab, scroller)
        verify(lastRect.x >= -1 && lastRect.x + lastRect.width <= scroller.width + 1,
               "the final About tab is reachable at the end of the strip")

        var before = findChild(shelvesPage, "shelvesScroll").contentY
        touchItem(lastShelfAction)
        verify(findChild(shelvesPage, "shelvesScroll").contentY > before,
               "touching the last shelf tab reveals its real section through revealSection")
        compare(shelvesPage.activeTab, 3,
                "the active tab follows the section brought into view by touch")
    }

    function test_phone_portrait_tabs_fit_the_viewport() {
        setPhoneViewport(354, 792)
        var tabs = findChild(shelvesPage, "shelvesTabs")
        verify(tabs !== null)
        var tabsRect = rectIn(tabs, shelvesPage)
        verify(tabsRect.x >= 0 && tabsRect.x + tabsRect.width <= shelvesPage.width + 1,
               "long shelf labels stay within the portrait viewport")
        if (shelvesPage.hasOwnProperty("phoneLayout"))
            compare(shelvesPage.phoneLayout, true)
        verify(tabs.height >= 48)
        var firstAction = findChild(shelvesPage, "shelvesTabAction_0")
        verify(firstAction && firstAction.width >= 48 && firstAction.height >= 48)
        verify(findAction(shelvesPage, "Minimize") === null)
    }

    function test_desktop_shelf_tab_geometry_and_window_chrome_are_preserved() {
        host.width = 1280
        host.height = 720
        shelvesPage.width = host.width
        shelvesPage.height = host.height
        if (shelvesPage.hasOwnProperty("androidHost"))
            shelvesPage.androidHost = false
        wait(30)

        var tabs = findChild(shelvesPage, "shelvesTabs")
        verify(tabs !== null)
        compare(tabs.height, 46)
        fuzzyCompare(tabs.x, 64, 0.1)
        fuzzyCompare(tabs.y, 400, 0.1)
        compare(tabs.width, 1168,
                "desktop keeps the measured natural-width shelf tab strip")
        var firstTab = findChild(shelvesPage, "shelvesTab_0")
        if (firstTab)
            compare(firstTab.height, 36)
        verify(findAction(shelvesPage, "Minimize") !== null,
               "desktop keeps its minimize action")
        verify(findAction(shelvesPage, "Enter fullscreen") !== null
               || findAction(shelvesPage, "Exit fullscreen") !== null,
               "desktop keeps its fullscreen action")
        verify(findAction(shelvesPage, "Close Colosseum") !== null,
               "desktop keeps its quit action")
    }
}
