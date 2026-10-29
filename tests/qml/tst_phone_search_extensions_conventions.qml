import QtQuick
import QtQuick.Window
import QtTest
import "../../qml" as Colosseum

TestCase {
    name: "PhoneSearchExtensionsConventions"
    when: windowShown

    Window {
        id: testWindow
        visible: true
        width: 354
        height: 792
        property bool androidHost: true
    }

    QtObject {
        id: history
        property var entries: []
        signal changed(string scope)
        function list(scope) { return entries.slice(0) }
        function record(scope, query) { entries = [query].concat(entries); changed(scope); return list(scope) }
        function remove(scope, query) {
            entries = entries.filter(function(value) { return value !== query })
            changed(scope)
            return list(scope)
        }
    }

    Component { id: searchComponent; Colosseum.SearchSurface {} }
    Component { id: cardComponent; Colosseum.ExtensionsStoreCard {} }
    Component { id: seeAllComponent; Colosseum.ExtensionsSeeAllPage {} }

    SignalSpy { id: itemSpy; signalName: "itemRequested" }
    SignalSpy { id: activationSpy; signalName: "activated" }
    SignalSpy { id: addonSpy; signalName: "addonActivated" }

    function findItem(root, objectName) {
        if (!root)
            return null
        if (root.objectName === objectName)
            return root
        for (let i = 0; i < root.children.length; ++i) {
            const found = findItem(root.children[i], objectName)
            if (found)
                return found
        }
        return null
    }

    function longTopMatch() {
        return {
            testKey: "top",
            title: "Invincible: The Entire Long-Form Match Name That Must Never Push the Useful Controls Away",
            meta: "ANIMATION · SERIES · 2021",
            subtitle: "Animation · Series · 2021",
            synopsis: "Mark Grayson is an ordinary teenager except that his father is the most powerful superhero on the planet. " +
                      "When Mark begins developing powers of his own, he learns that the world around him is more complicated " +
                      "than the stories he grew up hearing. The second paragraph continues with enough detail to exceed a phone " +
                      "preview in both orientations. This final paragraph exists so expansion has real content to reveal and " +
                      "can be collapsed again.",
            data: { id: "invincible" },
            group: "Series"
        }
    }

    function normalResult() {
        return {
            testKey: "normal",
            title: "A Normal Result",
            subtitle: "Drama · 2023",
            cover: "",
            data: { id: "normal" },
            group: "Series"
        }
    }

    function createSearch(width, height, results, dispatcher, historyStore) {
        testWindow.width = width
        testWindow.height = height
        testWindow.androidHost = true
        const props = {
            searchMode: "Theatre",
            width: width,
            height: height,
            results: results || [],
            historyStore: historyStore === undefined ? null : historyStore
        }
        props.searchDispatcher = dispatcher || function(mode, query, callback) { return null }
        const surface = searchComponent.createObject(testWindow.contentItem, props)
        if (results && results.length > 0) {
            const input = findItem(surface, "searchSurfaceInput")
            if (input)
                input.text = "Existing query"
        }
        return surface
    }

    function createAddon(slug, description) {
        return {
            slug: slug,
            name: slug,
            title: slug,
            description: description,
            categories: ["streaming"],
            stars: 5,
            logo: "",
            configurable: true,
            setupRequired: true
        }
    }

    function test_longTopMatchStaysBoundedAndLeavesLandscapeResultDiscoverable() {
        const results = [longTopMatch(), normalResult()]
        let surface = createSearch(354, 792, results)
        wait(0)
        waitForRendering(surface)

        verify(surface.phoneNarrow, "354dp portrait must use the phone result layout")
        const portraitCard = findItem(surface, "phoneTopCard")
        const portraitTitle = findItem(surface, "phoneTopTitle")
        const portraitSynopsis = findItem(surface, "phoneTopSynopsis")
        const portraitMore = findItem(surface, "phoneTopMore")
        const portraitOpen = findItem(surface, "phoneTopOpen")
        verify(portraitCard !== null)
        if (portraitCard) {
            verify(portraitCard.visible)
            verify(portraitCard.height < 360,
                   "a long synopsis must not consume the portrait result viewport")
        }
        if (portraitTitle)
            verify(portraitTitle.lineCount <= 2, "the top title is capped at two lines")
        if (portraitSynopsis)
            verify(portraitSynopsis.lineCount <= 3, "the collapsed synopsis is a short preview")
        if (portraitMore) {
            verify(portraitMore.visible)
            verify(portraitMore.height >= 48, "More has a 48dp touch target")
        }
        if (portraitOpen)
            verify(portraitOpen.height >= 48, "Open has a 48dp touch target")
        surface.destroy()

        surface = createSearch(792, 354, results)
        wait(0)
        waitForRendering(surface)
        verify(surface.phoneNarrow, "792dp short landscape remains in phone conventions")
        const landscapeCard = findItem(surface, "phoneTopCard")
        const landscapeTitle = findItem(surface, "phoneTopTitle")
        const landscapeSynopsis = findItem(surface, "phoneTopSynopsis")
        const landscapeOpen = findItem(surface, "phoneTopOpen")
        const viewport = findItem(surface, "searchResultsViewport")
        const ordinaryRow = findItem(surface, "phoneSearchResult_normal")
        verify(landscapeCard !== null)
        if (landscapeCard)
            verify(landscapeCard.height <= 180, "short landscape keeps the hero compact")
        if (landscapeTitle)
            verify(landscapeTitle.lineCount <= 2, "landscape title is capped at two lines")
        if (landscapeSynopsis)
            verify(landscapeSynopsis.lineCount <= 2, "landscape synopsis is capped at two lines")
        if (landscapeOpen && viewport) {
            const openY = landscapeOpen.mapToItem(viewport, 0, 0).y
            verify(openY >= 0 && openY + landscapeOpen.height <= viewport.height,
                   "the main action is visible in the short-height viewport")
        }
        if (ordinaryRow && viewport) {
            const rowY = ordinaryRow.mapToItem(viewport, 0, 0).y
            verify(rowY < viewport.height && rowY + ordinaryRow.height > 0,
                   "a normal result remains discoverable below the top match")
        } else {
            verify(false, "the normal result row must be instantiated")
        }
        surface.destroy()
    }

    function test_topMatchMoreLessAndSemanticOpenActivateOnlyTheirOwnActions() {
        const surface = createSearch(354, 792, [longTopMatch(), normalResult()])
        wait(0)
        waitForRendering(surface)
        itemSpy.target = surface
        itemSpy.clear()

        const synopsis = findItem(surface, "phoneTopSynopsis")
        const more = findItem(surface, "phoneTopMore")
        const moreAction = findItem(surface, "phoneTopMoreAction")
        const open = findItem(surface, "phoneTopOpen")
        const openAction = findItem(surface, "phoneTopOpenAction")
        const viewport = findItem(surface, "searchResultsViewport")
        verify(synopsis !== null && more !== null && moreAction !== null
               && open !== null && openAction !== null)
        if (synopsis && more && moreAction && open && openAction) {
            verify(moreAction.accessibleName.length > 0)
            const initialViewportY = viewport ? viewport.contentY : 0
            mouseClick(more, more.width / 2, more.height / 2)
            wait(0)
            verify(synopsis.lineCount > 3, "More reveals the full synopsis")
            if (viewport)
                compare(viewport.contentY, initialViewportY,
                        "expanding text leaves the results viewport in place")
            verify(moreAction.accessibleName.toLowerCase().indexOf("less") >= 0)
            compare(itemSpy.count, 0, "expanding text must not open the match")
            moreAction.forceActiveFocus(Qt.OtherFocusReason)
            testWindow.requestActivate()
            tryCompare(moreAction, "activeFocus", true)
            keyClick(Qt.Key_Return)
            wait(0)
            verify(synopsis.lineCount <= 3, "keyboard activation collapses the synopsis")
            if (viewport)
                compare(viewport.contentY, initialViewportY,
                        "collapsing text leaves the results viewport in place")
            compare(itemSpy.count, 0)

            verify(openAction.accessibleName.indexOf("Invincible") >= 0)
            openAction.forceActiveFocus()
            keyClick(Qt.Key_Return)
            compare(itemSpy.count, 1, "the named Open action activates exactly once")
            itemSpy.clear()
            mouseClick(open, open.width / 2, open.height / 2)
            compare(itemSpy.count, 1, "pointer activation is delivered once")

            mouseClick(more, more.width / 2, more.height / 2)
            verify(findItem(surface, "phoneTopCard").synopsisExpanded)
            surface.results = [normalResult(), longTopMatch()]
            compare(findItem(surface, "phoneTopCard").synopsisExpanded, false,
                    "changing the top result collapses the prior synopsis expansion")
        }
        surface.destroy()
    }

    function test_phoneResultRowsHaveNamedKeyboardAndPointerActivation() {
        const surface = createSearch(354, 792, [longTopMatch(), normalResult()])
        wait(0)
        waitForRendering(surface)
        itemSpy.target = surface
        itemSpy.clear()

        const row = findItem(surface, "phoneSearchResult_normal")
        const action = findItem(surface, "phoneSearchResultAction_normal")
        verify(row !== null && action !== null)
        if (row && action) {
            verify(action.accessibleName.indexOf("A Normal Result") >= 0)
            action.forceActiveFocus(Qt.OtherFocusReason)
            testWindow.requestActivate()
            tryCompare(action, "activeFocus", true)
            keyClick(Qt.Key_Return)
            compare(itemSpy.count, 1, "keyboard activation opens the result once")
            itemSpy.clear()
            mouseClick(row, row.width / 2, row.height / 2)
            compare(itemSpy.count, 1, "pointer activation opens the result once")
        }
        surface.destroy()
    }

    function test_searchSubmitUsesPendingTextOnceAndRejectsLateResults() {
        const dispatched = []
        const callbacks = []
        const surface = createSearch(354, 792, [], function(mode, query, callback) {
            dispatched.push(query)
            callbacks.push(callback)
            return null
        })
        wait(0)
        itemSpy.target = surface
        itemSpy.clear()
        const input = findItem(surface, "searchSurfaceInput")
        verify(input !== null)
        if (input) {
            compare(input.EnterKey.type, Qt.EnterKeySearch,
                    "the phone input advertises the Search IME action")
            input.forceActiveFocus(Qt.OtherFocusReason)
            testWindow.requestActivate()
            tryCompare(input, "activeFocus", true)
            input.text = "pending exact query"
            keyClick(Qt.Key_Return)
            compare(dispatched.length, 1, "submit dispatches the pending query immediately")
            compare(dispatched[0], "pending exact query")
            compare(itemSpy.count, 0, "submitting a query does not open the previous top result")

            callbacks[0]([{ title: "Fresh pending result", data: { id: "fresh" }, group: "Series" }], "")
            compare(surface.results[0].title, "Fresh pending result")
            input.text = "current query"
            keyClick(Qt.Key_Enter)
            compare(dispatched.length, 2)
            compare(dispatched[1], "current query")
            compare(surface.results[0].title, "Fresh pending result",
                    "submitting another query keeps the last successful results visible while loading")
            callbacks[0]([{ title: "Stale result", data: { id: "stale" }, group: "Series" }], "")
            compare(surface.results[0].title, "Fresh pending result",
                    "a stale asynchronous callback cannot replace results for the current query")
            callbacks[1]([{ title: "Current result", data: { id: "current" }, group: "Series" }], "")
            compare(surface.results[0].title, "Current result")
        }
        surface.destroy()
    }

    function test_phoneClearAndRecentRemoveHaveSeparateFortyEightDpTargets() {
        history.entries = ["Recent title"]
        const surface = createSearch(354, 792, [], null, history)
        wait(0)
        waitForRendering(surface)
        const input = findItem(surface, "searchSurfaceInput")
        verify(input !== null)
        input.text = "clear me"
        wait(0)
        const clear = findItem(surface, "searchClearAction")
        verify(clear !== null)
        if (clear)
            verify(clear.width >= 48 && clear.height >= 48)
        input.text = ""
        surface.loadRecent()
        wait(0)
        waitForRendering(surface)
        const remove = findItem(surface, "searchRecentRemove")
        const label = findItem(surface, "searchRecentBody")
        verify(remove !== null && label !== null)
        if (remove && label) {
            verify(remove.width >= 48 && remove.height >= 48)
            const removeRect = remove.mapToItem(surface, 0, 0)
            const labelRect = label.mapToItem(surface, 0, 0)
            verify(removeRect.x >= labelRect.x + label.width
                   || removeRect.y >= labelRect.y + label.height
                   || labelRect.x >= removeRect.x + remove.width
                   || labelRect.y >= removeRect.y + remove.height,
                   "recent text and remove target do not overlap")
        }
        surface.destroy()
    }

    function test_compactAddonRowContainsActionsAndPreservesInstalledSetupState() {
        const description = "A streaming add-on with a long description that explains catalogue matching, " +
                            "language choices, source quality and account configuration. " +
                            "The full details remain available when the named More description action is opened."
        const card = cardComponent.createObject(testWindow.contentItem, {
            width: 354,
            addon: createAddon("torrentio", description),
            phoneLayout: true,
            installed: true,
            busy: false
        })
        wait(0)
        waitForRendering(card)
        activationSpy.target = card
        activationSpy.clear()

        const art = findItem(card, "extensionsPhoneArtwork")
        const artworkFallback = findItem(card, "extensionsPhoneArtworkFallback")
        const title = findItem(card, "extensionsPhoneTitle")
        const preview = findItem(card, "extensionsPhoneDescription")
        const more = findItem(card, "extensionsPhoneMore")
        const moreAction = findItem(card, "extensionsPhoneMoreAction")
        const primary = findItem(card, "extensionsPhonePrimaryAction")
        const primaryAction = findItem(card, "extensionsPhonePrimaryActionKeyboard")
        verify(card.height <= 140, "a brief phone card stays compact enough for an Essentials list")
        verify(art !== null && art.width <= 64 && art.height <= 64)
        verify(artworkFallback !== null && artworkFallback.visible,
               "an absent catalogue logo still has a visible artwork fallback")
        verify(title !== null && preview !== null && more !== null && moreAction !== null)
        verify(primary !== null && primaryAction !== null)
        if (primary && primaryAction && more && preview) {
            verify(primary.height >= 48 && more.height >= 48)
            verify(primaryAction.enabled, "an installed configurable add-on remains set-up-able")
            verify(primaryAction.accessibleName.toLowerCase().indexOf("set up") >= 0)
            verify(moreAction.accessibleName.length > 0)
            const primaryRect = primary.mapToItem(card, 0, 0)
            const moreRect = more.mapToItem(card, 0, 0)
            verify(primaryRect.x >= moreRect.x + more.width
                   || moreRect.x >= primaryRect.x + primary.width
                   || primaryRect.y >= moreRect.y + more.height
                   || moreRect.y >= primaryRect.y + primary.height,
                   "setup and description controls have separate bounds")
            mouseClick(more, more.width / 2, more.height / 2)
            wait(0)
            verify(card.phoneDescriptionExpanded)
            verify(preview.lineCount > 2, "More reveals the full add-on description")
            compare(activationSpy.count, 0)
            mouseClick(primary, primary.width / 2, primary.height / 2)
            compare(activationSpy.count, 1, "the setup button emits one activation")
            card.busy = true
            verify(!primaryAction.enabled, "busy add-ons disable setup")
            card.busy = false
            card.addon = {
                slug: "installed-fixed", name: "Installed fixed", title: "Installed fixed",
                description: "Already installed.", categories: ["streaming"], stars: 4,
                logo: "", configurable: false, setupRequired: false
            }
            card.installed = true
            verify(!card.phoneDescriptionExpanded, "a recycled card resets its expanded description")
            verify(!primaryAction.enabled, "installed non-configurable add-ons disable setup")
        }
        card.destroy()
    }

    function test_seeAllUsesCompactLandscapeHeaderAndKeepsSearchCatalogueState() {
        testWindow.width = 792
        testWindow.height = 354
        testWindow.androidHost = true
        const first = createAddon("torrentio", "A fast streaming catalogue with a full description.")
        const second = createAddon("subtitles", "Subtitles in multiple languages.")
        const page = seeAllComponent.createObject(testWindow.contentItem, {
            width: 792,
            height: 354,
            row: { title: "Essentials", sub: "Quick setup", all: [first, second] },
            pool: [first, second]
        })
        wait(0)
        waitForRendering(page)
        verify(page.phoneLayout)
        verify(page.phoneLandscape, "short landscape gets the compact header")
        const header = findItem(page, "extensionsSeeAllHeader")
        const list = findItem(page, "extensionsSeeAllList")
        verify(header !== null && list !== null)
        if (list)
            verify(list.anchors.topMargin <= 12, "landscape list spacing does not waste the short viewport")
        const phoneRow = findItem(page, "extensionsPhoneRow_torrentio")
        const delegate = phoneRow ? phoneRow.parent : null
        verify(delegate !== null)
        if (delegate)
            verify(delegate.height <= 140, "See all reuses the compact phone card")
        compare(page.items.length, 2)

        page.searching = true
        const box = findItem(page, "extensionsSearchField")
        verify(box !== null)
        if (box) {
            box.text = "Torrentio"
            wait(0)
            compare(page.items.length, 1)
            compare(page.items[0].slug, "torrentio")
        }
        page.searching = false
        wait(0)
        compare(page.items.length, 2, "returning to See all retains the catalogue")
        page.destroy()
    }

    function test_seeAllListReturnHonorsDisabledPhoneAction() {
        testWindow.width = 354
        testWindow.height = 792
        testWindow.androidHost = true
        const fixed = createAddon("installed-fixed", "Already installed.")
        fixed.configurable = false
        fixed.setupRequired = false
        const page = seeAllComponent.createObject(testWindow.contentItem, {
            width: 354,
            height: 792,
            row: { title: "Installed", sub: "Already here", all: [fixed] },
            installedFn: function(addon) { return true },
            busy: ({})
        })
        wait(0)
        waitForRendering(page)
        const list = findItem(page, "extensionsSeeAllList")
        verify(list !== null)
        addonSpy.target = page
        addonSpy.clear()
        if (list) {
            list.currentIndex = 0
            page.takeKeyboardFocus()
            testWindow.requestActivate()
            tryCompare(list, "activeFocus", true)
            keyClick(Qt.Key_Return)
            compare(addonSpy.count, 0,
                    "a disabled installed add-on cannot activate from ListView Return")
        }
        page.destroy()
    }

    function test_addonSearchHas48dpSearchSubmission() {
        testWindow.width=354; testWindow.height=792; testWindow.requestActivate()
        const page=seeAllComponent.createObject(testWindow.contentItem,{
            width:354,height:792,searching:true,pool:[createAddon("torrentio","Streaming source")]
        })
        const input=findItem(page,"extensionsSearchField")
        verify(input.height>=48,"The addon search input is too short to tap")
        compare(input.EnterKey.type,Qt.EnterKeySearch)
        input.text="Torrentio";input.forceActiveFocus()
        keyClick(Qt.Key_Return)
        compare(page.query,"Torrentio")
        compare(page.items.length,1)
        verify(page.visible,"Submitting must keep the results open")
        page.destroy()
    }

    function test_wideEnterKeepsTheExistingTopMatchOpenAction() {
        const surface = createSearch(1280, 720, [longTopMatch(), normalResult()])
        wait(0)
        waitForRendering(surface)
        itemSpy.target = surface
        itemSpy.clear()
        const input = findItem(surface, "searchSurfaceInput")
        verify(input !== null)
        if (input) {
            input.forceActiveFocus(Qt.OtherFocusReason)
            testWindow.requestActivate()
            tryCompare(input, "activeFocus", true)
            keyClick(Qt.Key_Return)
            compare(itemSpy.count, 1, "wide Enter keeps its existing Open top match behavior")
        }
        surface.destroy()
    }

    function test_wideSearchAndExtensionCardsKeepTheirExistingGridAndArtLayout() {
        const results = [longTopMatch(), normalResult()]
        const surface = createSearch(1280, 720, results)
        wait(0)
        waitForRendering(surface)
        verify(!surface.phoneNarrow)
        const desktopTop = findItem(surface, "searchResult_invincible")
        const phoneTop = findItem(surface, "phoneTopCard")
        verify(desktopTop !== null && desktopTop.visible)
        if (phoneTop)
            verify(!phoneTop.visible)
        surface.destroy()

        testWindow.width = 1280
        testWindow.height = 720
        const card = cardComponent.createObject(testWindow.contentItem, {
            width: 365,
            addon: createAddon("torrentio", "A short description."),
            phoneLayout: false
        })
        wait(0)
        waitForRendering(card)
        compare(card.height, 299)
        card.destroy()
    }
}
