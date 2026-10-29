import QtQuick
import QtQuick.Window
import QtTest
import "../../qml" as App

TestCase {
    id: testCase
    name: "PhoneDCAULayout"
    when: windowShown

    Window {
        id: host
        width: 354
        height: 792
        visible: true
    }

    Component {
        id: pageComponent
        App.DCAUUniversePage {
            androidHost: true
            extensionId: ""
            reducedMotion: true
        }
    }

    property var page: null

    function init() {
        host.width = 354
        host.height = 792
        page = pageComponent.createObject(host.contentItem,
                                          { width: host.width, height: host.height })
        verify(page !== null, "the actual DCAU page instantiates without backend services")
        wait(0)
    }

    function cleanup() {
        if (page)
            page.destroy()
        page = null
        wait(0)
    }

    function findChild(parent, objectName) {
        if (!parent)
            return null
        if (parent.objectName === objectName)
            return parent
        const children = parent.children || []
        for (let i = 0; i < children.length; ++i) {
            const found = findChild(children[i], objectName)
            if (found)
                return found
        }
        return null
    }

    function findDescendant(parent, predicate) {
        if (!parent)
            return null
        if (predicate(parent))
            return parent
        const children = parent.children || []
        for (let i = 0; i < children.length; ++i) {
            const found = findDescendant(children[i], predicate)
            if (found)
                return found
        }
        return null
    }

    function collectPortals(parent, result) {
        if (!parent)
            return
        if (parent.objectName && parent.objectName.indexOf("dcauPortal_") === 0)
            result.push(parent)
        const children = parent.children || []
        for (let i = 0; i < children.length; ++i)
            collectPortals(children[i], result)
    }

    function findText(parent, value) {
        return findDescendant(parent, function(item) { return item.text === value })
    }

    function rectIn(item, target) {
        const point = item.mapToItem(target, 0, 0)
        return { x: point.x, y: point.y, width: item.width, height: item.height }
    }

    function touchItem(item) {
        const root = host.contentItem
        const point = item.mapToItem(root, item.width / 2, item.height / 2)
        touchEvent(root).press(0, root, point.x, point.y).commit()
        wait(20)
        touchEvent(root).release(0, root, point.x, point.y).commit()
        wait(30)
    }

    function setViewport(width, height, androidHost) {
        host.width = width
        host.height = height
        page.androidHost = androidHost
        wait(30)
        compare(page.width, width)
        compare(page.height, height)
        compare(page.phoneLayout, androidHost && Math.min(width, height) < 600)
        compare(page.extensionId, "")
        compare(page.payload, null)
    }

    function assertPhonePortalBounds(width, height) {
        setViewport(width, height, true)

        const landing = findChild(page, "dcauLandingScroll")
        const viewport = findChild(page, "dcauPortalViewport")
        const continueSection = findChild(page, "dcauContinueSection")
        const desktopControls = findChild(page, "dcauDesktopWindowControls")
        verify(landing && viewport && continueSection && desktopControls,
               "the actual landing scroll, portal viewport, Continue section, and window controls exist")
        verify(landing.clip, "phone landing content is clipped to the page")
        verify(viewport.clip, "portals are clipped to their horizontal viewport")
        compare(desktopControls.visible, false,
                "Android hides desktop minimize/fullscreen/quit controls")

        // Exercise the same vertical reservation as a populated Continue section without
        // calling Progress or manufacturing backend state.
        continueSection.visible = true
        wait(30)
        if (landing.contentHeight > landing.height)
            landing.contentY = landing.contentHeight - landing.height
        wait(30)

        const landingRect = rectIn(landing, page)
        compare(landingRect.x, 0)
        compare(landingRect.y, 0)
        verify(landingRect.width <= page.width + 1 && landingRect.height <= page.height + 1,
               "the outer landing viewport remains within the phone page")
        const viewportRect = rectIn(viewport, page)
        verify(viewportRect.x >= -1 && viewportRect.y >= -1
               && viewportRect.x + viewportRect.width <= page.width + 1
               && viewportRect.y + viewportRect.height <= page.height + 1,
               "the portal viewport is reachable fully inside the page at " + width + "x" + height
               + " (viewport " + viewportRect.x + "," + viewportRect.y + " "
               + viewportRect.width + "x" + viewportRect.height + ")")
        if (height < 600)
            verify(landing.interactive && landing.contentHeight > landing.height,
                   "short landscape scrolls the reserved landing content vertically")

        const portals = []
        collectPortals(page, portals)
        verify(portals.length >= 4, "the actual DCAU portal delegates are present")
        for (let i = 0; i < portals.length; ++i) {
            const portal = portals[i]
            const title = findText(portal, portal.title)
            verify(title !== null && title.visible,
                   "the " + portal.title + " portal has a visible title")
            const titleInPortal = rectIn(title, portal)
            verify(titleInPortal.x >= -1 && titleInPortal.y >= -1
                   && titleInPortal.x + titleInPortal.width <= portal.width + 1
                   && titleInPortal.y + titleInPortal.height <= portal.height + 1,
                   portal.title + " title layout stays inside its portal")
            const titleInViewport = rectIn(title, viewport)
            verify(titleInViewport.y >= -1
                   && titleInViewport.y + titleInViewport.height <= viewport.height + 1,
                   portal.title + " title stays vertically inside the clipped portal viewport")
        }

        const selected = findDescendant(page, function(item) {
            return item.objectName && item.objectName.indexOf("dcauPortal_") === 0 && item.selected
        })
        verify(selected !== null, "the landing page has a selected portal")
        const selectedRect = rectIn(selected, viewport)
        verify(selectedRect.x >= -1 && selectedRect.y >= -1
               && selectedRect.x + selectedRect.width <= viewport.width + 1
               && selectedRect.y + selectedRect.height <= viewport.height + 1,
               "selected portal fits the clipped viewport at " + width + "x" + height)
        const selectedInPage = rectIn(selected, page)
        verify(selectedInPage.x >= -1 && selectedInPage.y >= -1
               && selectedInPage.x + selectedInPage.width <= page.width + 1
               && selectedInPage.y + selectedInPage.height <= page.height + 1,
               "selected portal fits within the phone window at " + width + "x" + height)
        const selectedTitle = findText(selected, selected.title)
        const titleInPage = rectIn(selectedTitle, page)
        verify(titleInPage.x >= -1 && titleInPage.y >= -1
               && titleInPage.x + titleInPage.width <= page.width + 1
               && titleInPage.y + titleInPage.height <= page.height + 1,
               "selected portal title fits within the phone window at " + width + "x" + height)
    }

    function test_phone_portrait_and_short_landscape_bounds() {
        assertPhonePortalBounds(354, 792)
        assertPhonePortalBounds(792, 354)
    }

    function test_phone_portal_tap_routes_and_back_returns_to_landing() {
        assertPhonePortalBounds(792, 354)
        const gotham = findChild(page, "dcauPortal_gotham")
        verify(gotham !== null && gotham.visible)
        touchItem(gotham)
        tryCompare(page, "currentHubId", "gotham")

        const backAction = findDescendant(page, function(item) {
            return item.variant === "plain" && item.label === "Back"
                   && item.triggered !== undefined
        })
        verify(backAction !== null, "the real BackAction remains available inside the hub")
        let backCount = 0
        const onBackRequested = function() { ++backCount }
        page.backRequested.connect(onBackRequested)
        touchItem(backAction)
        tryCompare(page, "currentHubId", "")
        compare(backCount, 0, "Back from a hub returns to the landing page")
        touchItem(backAction)
        compare(backCount, 1, "Back on the landing page emits backRequested once")
        page.backRequested.disconnect(onBackRequested)
    }

    function assertShellEscapeReturnsToLanding(width, height) {
        setViewport(width, height, true)
        const landing = findChild(page, "dcauLandingScroll")
        const gotham = findChild(page, "dcauPortal_gotham")
        verify(landing && gotham && gotham.visible,
               "the landing and real Gotham portal are visible at " + width + "x" + height)
        compare(page.currentHubId, "", "the test begins on the DCAU landing page")

        let backCount = 0
        const onBackRequested = function() { ++backCount }
        page.backRequested.connect(onBackRequested)

        touchItem(gotham)
        tryCompare(page, "currentHubId", "gotham")
        compare(landing.visible, false, "the landing is hidden while the hub is active")
        verify(typeof page.requestEscape === "function",
               "the DCAU page exposes the Main shell's requestEscape hook")
        const escaped = page.requestEscape()
        compare(escaped, true, "the active hub consumes the first shell escape")
        compare(page.currentHubId, "", "shell escape clears the active hub")
        compare(landing.visible, true, "shell escape reveals the DCAU landing page")
        compare(backCount, 0, "returning to the landing does not emit backRequested")

        compare(page.requestEscape(), false,
                "a second shell escape is not consumed by the landing page")
        compare(backCount, 0,
                "the page leaves closing the universe to Main without emitting a child back signal")
        page.backRequested.disconnect(onBackRequested)
    }

    function test_phone_shell_escape_returns_to_landing_before_leaving_universe() {
        assertShellEscapeReturnsToLanding(354, 792)
        assertShellEscapeReturnsToLanding(792, 354)
    }

    function test_desktop_wide_portal_shape_and_window_controls_are_preserved() {
        setViewport(1280, 720, false)
        compare(page.phoneLayout, false)

        const viewport = findChild(page, "dcauPortalViewport")
        const controls = findChild(page, "dcauDesktopWindowControls")
        const gotham = findChild(page, "dcauPortal_gotham")
        verify(viewport && controls && gotham)
        compare(controls.visible, true)
        compare(gotham.width, 300)
        compare(gotham.height, 480)
        const controlsRect = rectIn(controls, page)
        verify(controlsRect.x >= 0 && controlsRect.y >= 0
               && controlsRect.x + controlsRect.width <= page.width
               && controlsRect.y + controlsRect.height <= page.height,
               "desktop window controls remain inside the wide page")
    }
}
