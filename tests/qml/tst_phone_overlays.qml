import QtQuick
import QtQuick.Window
import QtTest 1.3
import "../../qml" as App

TestCase {
    id: testCase
    name: "PhoneOverlays"
    when: windowShown

    Window {
        id: testWindow
        width: 354
        height: 792
        visible: true
        property bool androidHost: true
    }

    Component {
        id: sourcesSheetComponent
        App.SourcesSheet {}
    }

    Component {
        id: subtitleMenuComponent
        App.SubtitleMenu {}
    }

    Component {
        id: watchPartyFixtureComponent
        Item {
            id: watchPartyFixture
            property alias carrier: movableCarrier
            property alias party: watchParty

            Item {
                id: movableCarrier
                x: 40
                y: 300
                width: 96
                height: 80

                App.WatchPartyPanel {
                    id: watchParty
                    x: 12
                    y: 12
                    overlayParent: testWindow.contentItem
                    panelOpen: true
                }
            }
        }
    }

    SignalSpy {
        id: playSpy
        signalName: "playRequested"
    }

    function findChild(root, name) {
        if (!root)
            return null
        if (root.objectName === name)
            return root
        const children = root.children || []
        for (let i = 0; i < children.length; ++i) {
            const found = findChild(children[i], name)
            if (found)
                return found
        }
        return null
    }

    function sourceRows() {
        const rows = []
        for (let i = 0; i < 12; ++i) {
            rows.push({
                "infoHash": ("000000000000000000000000000000000000000" + (i + 1).toString(16)).slice(-40),
                "fileIdx": i,
                "addonName": "Fixture Provider " + (i % 3 + 1),
                "qualityLine": "1080p · HEVC · HDR",
                "quality": "1080p",
                "release": "Fixture release " + i + " with a long descriptive title for phone rows",
                "seeders": 18 + i,
                "size": "1.4 GB",
                "sourceName": "Fixture P2P",
                "streamKind": "Torrent",
                "streamLabel": "P2P stream",
                "audio": "AAC Stereo",
                "languages": ["English", "Japanese"],
                "tags": ["HEVC", "10-bit", "HDR"],
                "videoCompatibility": { "state": "supported" }
            })
        }
        return rows
    }

    function subtitleTracks() {
        const tracks = []
        for (let i = 0; i < 10; ++i) {
            tracks.push({
                "id": "fixture-subtitle-" + i,
                "lang": "en",
                "label": "English",
                "tech": "Fixture subtitles " + i,
                "external": i % 2 === 1,
                "hearingImpaired": i % 3 === 0,
                "forced": i % 4 === 0,
                "selected": i === 0
            })
        }
        return tracks
    }

    function exerciseSourceSheetSize(sheet, list, width, height, expectedHash) {
        testWindow.width = width
        testWindow.height = height
        wait(60)

        compare(sheet.open, true)
        compare(sheet.phoneLayout, true)
        verify(list.visible, "source list should remain visible with fixture rows")
        verify(list.height >= 88,
               "source list needs a useful phone viewport at " + width + "x" + height
               + "; actual height was " + list.height)
        verify(list.contentHeight > list.height, "fixture rows should exercise list scrolling")

        const index = sheet.visibleRows.length - 1
        list.positionViewAtIndex(index, ListView.End)
        list.forceLayout()
        tryVerify(function() { return list.itemAtIndex(index) !== null })
        const row = list.itemAtIndex(index)
        verify(row !== null, "scrolled source row should be instantiated")
        if (!row)
            return

        const action = findChild(row, "sourcePlay_" + index)
        verify(action !== null, "scrolled source row should expose its Play action")
        if (!action)
            return

        const actionPoint = action.mapToItem(list, 0, 0)
        verify(actionPoint.y >= -0.5, "Play action top should be inside the scrolled viewport")
        verify(actionPoint.y + action.height <= list.height + 0.5,
               "Play action bottom should be inside the scrolled viewport")
        verify(action.width >= 48 && action.height >= 48,
               "Play action should remain a phone-size touch target")

        playSpy.clear()
        touchEvent(action)
            .press(0, action, action.width / 2, action.height / 2)
            .commit()
        wait(20)
        touchEvent(action)
            .release(0, action, action.width / 2, action.height / 2)
            .commit()
        tryCompare(playSpy, "count", 1)
        compare(playSpy.signalArguments[0][0], expectedHash)
    }

    function test_sources_sheet_scrolls_and_accepts_touch_in_both_phone_orientations() {
        testWindow.width = 354
        testWindow.height = 792
        const sheet = createTemporaryObject(sourcesSheetComponent, testWindow.contentItem)
        verify(sheet !== null)
        if (!sheet)
            return

        sheet.rows = sourceRows()
        sheet.askedNames = ["Fixture Provider"]
        sheet.loading = false
        sheet.title = "Fixture title"
        sheet.open = true
        playSpy.target = sheet
        const list = findChild(sheet, "sourcesSheetList")
        verify(list !== null)
        if (!list)
            return

        exerciseSourceSheetSize(sheet, list, 354, 792, sheet.visibleRows[11].infoHash)
        exerciseSourceSheetSize(sheet, list, 792, 354, sheet.visibleRows[11].infoHash)
    }

    function assertSubtitlePanelGeometry(menu, panel, body, variants, language, filters,
                                         footer, delay, width, height) {
        testWindow.width = width
        testWindow.height = height
        menu.x = width - 60
        menu.y = height - 50
        wait(60)

        compare(menu.phoneLayout, true)
        compare(panel.visible, true)
        verify(panel.width > 0 && panel.height > 0)
        verify(panel.x >= -0.5 && panel.y >= -0.5)
        verify(panel.x + panel.width <= testWindow.width + 0.5,
               "subtitle panel should fit the host width")
        verify(panel.y + panel.height <= testWindow.height + 0.5,
               "subtitle panel should fit the host height")
        verify(body.y >= 56, "panel body should begin below the phone header")
        verify(body.width <= panel.width + 0.5 && body.height > 0)
        verify(body.y + body.height <= panel.height + 0.5,
               "subtitle body should stay inside the panel")
        compare(body.interactive, true)
        verify(body.contentHeight >= body.height)

        const filterPoint = filters.mapToItem(body.contentItem, 0, 0)
        const languagePoint = language.mapToItem(body.contentItem, 0, 0)
        const variantsPoint = variants.mapToItem(body.contentItem, 0, 0)
        const footerPoint = footer.mapToItem(body.contentItem, 0, 0)
        verify(filterPoint.y + filters.height <= languagePoint.y + 0.5,
               "filters and language selector should not overlap")
        verify(languagePoint.y + language.height <= variantsPoint.y + 0.5,
               "language selector and track list should not overlap")
        verify(variants.width > 0 && variants.height >= 40)
        verify(variantsPoint.y + variants.height <= footerPoint.y + 0.5,
               "track list viewport should end before the footer")
        verify(footer.width > 0 && footer.height > 0)
        verify(delay.width > 0 && delay.height > 0)

        body.contentY = Math.max(0, body.contentHeight - body.height)
        wait(30)
        const footerInPanel = footer.mapToItem(panel, 0, 0)
        const delayInPanel = delay.mapToItem(panel, 0, 0)
        verify(footerInPanel.y >= body.y - 0.5,
               "footer should be reachable within the scrollable body")
        verify(footerInPanel.y + footer.height <= body.y + body.height + 0.5,
               "scrolled footer should be bounded by the panel body")
        verify(delayInPanel.y >= footerInPanel.y + footer.height - 0.5,
               "delay controls should not overlap the footer")
        verify(delayInPanel.y + delay.height <= body.y + body.height + 0.5,
               "scrolled delay controls should be bounded by the panel body")
    }

    function assertAppearanceDrawer(menu, panel, body, styleButton, width) {
        const stylePoint = styleButton.mapToItem(panel, 0, 0)
        verify(stylePoint.x >= -0.5 && stylePoint.x + styleButton.width <= panel.width + 0.5)
        verify(stylePoint.y >= -0.5 && stylePoint.y + styleButton.height <= 56.5,
               "appearance action should remain reachable in the panel header")
        mouseClick(styleButton)
        tryCompare(menu, "appearanceOpen", true)

        const drawer = findChild(panel, "subtitleAppearanceDrawer")
        verify(drawer !== null)
        if (!drawer)
            return
        compare(drawer.visible, true)
        const drawerPoint = drawer.mapToItem(panel, 0, 0)
        const drawerContentPoint = drawer.mapToItem(body.contentItem, 0, 0)
        verify(Math.abs(drawerContentPoint.x) <= 0.5 && Math.abs(drawerContentPoint.y) <= 0.5,
               "appearance drawer origin in body-content coordinates was ("
               + drawerContentPoint.x + ", " + drawerContentPoint.y + "); panel origin is ("
               + drawerPoint.x + ", " + drawerPoint.y + "); body origin is ("
               + body.mapToItem(panel, 0, 0).x + ", " + body.mapToItem(panel, 0, 0).y + ")")
        verify(Math.abs(drawer.width - panel.width) <= 0.5,
               "phone appearance view should use the full panel width")
        verify(drawer.height >= 300 && drawer.height <= body.contentHeight + 0.5,
               "appearance view should remain bounded by its scroll content")
        verify(drawerContentPoint.x + drawer.width <= body.contentWidth + 0.5)
        verify(drawerContentPoint.y + drawer.height <= body.contentHeight + 0.5)
        verify(body.contentY <= 0.5,
               "opening appearance should return its top controls to the reachable viewport")
        const drawerVisibleTop = Math.max(drawerPoint.y, body.mapToItem(panel, 0, 0).y)
        const drawerVisibleBottom = Math.min(drawerPoint.y + drawer.height,
                                             body.mapToItem(panel, 0, 0).y + body.height)
        verify(drawerVisibleTop >= body.mapToItem(panel, 0, 0).y - 0.5)
        verify(drawerVisibleBottom <= body.mapToItem(panel, 0, 0).y + body.height + 0.5)
        verify(drawerVisibleBottom > drawerVisibleTop,
               "appearance drawer should expose content inside the panel viewport")
        menu.appearanceOpen = false
        wait(0)
    }

    function test_subtitle_panel_reflows_and_style_view_is_reachable() {
        testWindow.width = 354
        testWindow.height = 792
        const menu = createTemporaryObject(subtitleMenuComponent, testWindow.contentItem, {
            "androidHost": true,
            "overlayParent": testWindow.contentItem,
            "x": 294,
            "y": 742
        })
        verify(menu !== null)
        if (!menu)
            return

        menu.tracks = subtitleTracks()
        menu.panelOpen = true
        wait(60)
        const panel = findChild(testWindow.contentItem, "subtitlePanel")
        const body = findChild(testWindow.contentItem, "subtitleBodyScroll")
        const language = findChild(testWindow.contentItem, "subtitleLanguageCollection")
        const filters = findChild(testWindow.contentItem, "subtitleFilters")
        const variants = findChild(testWindow.contentItem, "subtitleVariants")
        const footer = findChild(testWindow.contentItem, "subtitleFooter")
        const delay = findChild(testWindow.contentItem, "subtitleDelay")
        const styleButton = findChild(testWindow.contentItem, "subtitleStyleButton")
        verify(panel && body && language && filters && variants && footer && delay && styleButton,
               "production subtitle panel should expose all phone layout regions")
        if (!(panel && body && language && filters && variants && footer && delay && styleButton))
            return

        assertSubtitlePanelGeometry(menu, panel, body, variants, language, filters,
                                    footer, delay, 354, 792)
        assertAppearanceDrawer(menu, panel, body, styleButton, 354)

        testWindow.width = 792
        testWindow.height = 354
        menu.x = 732
        menu.y = 304
        wait(60)
        compare(menu.appearanceOpen, false)
        assertSubtitlePanelGeometry(menu, panel, body, variants, language, filters,
                                    footer, delay, 792, 354)
        assertAppearanceDrawer(menu, panel, body, styleButton, 792)
    }

    function test_watch_party_height_tracks_moving_ancestor_without_moving_control() {
        testWindow.width = 354
        testWindow.height = 792
        const fixture = createTemporaryObject(watchPartyFixtureComponent, testWindow.contentItem)
        verify(fixture !== null)
        if (!fixture)
            return

        const party = fixture.party
        const popover = findChild(testWindow.contentItem, "watchPartyPanel")
        verify(party && popover)
        if (!(party && popover))
            return

        tryVerify(function() { return popover.visible && party.panelEffectiveHeight > 0 })
        const controlX = party.x
        const controlY = party.y
        const portraitHeight = party.panelEffectiveHeight
        fixture.carrier.y = 500
        tryVerify(function() { return party.panelEffectiveHeight > portraitHeight + 100 })
        compare(party.x, controlX)
        compare(party.y, controlY)
        compare(popover.height, party.panelEffectiveHeight)

        testWindow.width = 792
        testWindow.height = 354
        wait(60)
        verify(party.panelEffectiveHeight <= testWindow.height - party.panelEdgeMargin * 2 + 0.5,
               "landscape panel should respect the short host height")
        fixture.carrier.y = 100
        const lowPositionHeight = party.panelEffectiveHeight
        fixture.carrier.y = 220
        tryVerify(function() { return party.panelEffectiveHeight > lowPositionHeight + 80 })
        compare(party.x, controlX)
        compare(party.y, controlY)
        compare(popover.height, party.panelEffectiveHeight)
    }
}
