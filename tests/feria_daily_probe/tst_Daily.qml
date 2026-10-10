import QtQuick
import QtQuick.Controls
import QtQuick.Window
import QtTest
import QtWebEngine
import "../../qml/feria" as Feria
import "../../qml" as Colosseum

// Set 4 daily-browser contract: drive the production deck and real QtWebEngine offscreen.
Item {
    id: root
    width: 1060
    height: 760
    Colosseum.Theme { id: menuTheme }
    Item { id: embedded; x: 25; y: 125; width: 820; height: 580 }
    Feria.FeriaSessionDeck {
        id: deck
        parent: embedded
        anchors.fill: parent
        sessions: Sessions
        accountStore: FeriaAccount
        policy: FeriaBrowserPolicy
        displayed: true
    }
    TestCase {
        id: tests
        name: "FeriaDaily"
        when: windowShown
        property int serial: 0
        SignalSpy { id: privateDownloadSpy; target: deck.privateProfile; signalName: "downloadRequested" }
        SignalSpy { id: printMessageSpy; signalName: "message" }

        function init() {
            failOnWarning(/TypeError|ReferenceError|incompatible arguments|Could not convert argument/)
            deck.startQuiescence(false)
            tryCompare(deck, "quiescing", false, 15000)
            Fixture.switchProfile("daily-assert-" + (++serial))
            tryCompare(deck, "quiescing", false, 15000)
            deck.displayed = true
            deck.suppressed = false
            privateDownloadSpy.clear()
            printMessageSpy.target = null
            printMessageSpy.clear()
        }
        function cleanup() {
            printMessageSpy.target = null
            deck.startQuiescence(false)
            tryCompare(deck, "quiescing", false, 15000)
            compare(FeriaBrowserPolicy.runtimeViewCount, 0)
            compare(FeriaBrowserPolicy.runtimeProfileCount, 0)
        }
        function descriptor(path, privateMode) {
            return { providerId: "mangaplus", providerName: "Daily PDF fixture",
                url: FixtureUrl + path, startUrl: FixtureUrl + "/a",
                mode: "read", privateBrowsing: !!privateMode }
        }
        function loaded(page, path) {
            tryVerify(function() {
                return page && page.documentReady && !page.loading && !page.replacementLoadPending
                    && String(page.url) === FixtureUrl + path
            }, 15000, "Expected an engine-completed document at " + path)
        }
        function open(path, privateMode) {
            var id = deck.open(descriptor(path, privateMode), true, false, true)
            verify(id.length > 0, "production deck must allocate a browser session")
            tryVerify(function() {
                return privateMode ? deck.privateProfile !== null : deck.sharedProfile !== null
            }, 10000)
            // Profiles are owned by the deck and created lazily. Set the isolated
            // path before any test navigation that could possibly download.
            var profile = privateMode ? deck.privateProfile : deck.sharedProfile
            profile.downloadPath = Fixture.downloadRoot
            tryVerify(function() { return !!deck.browserForSession(id) }, 10000)
            loaded(deck.browserForSession(id), path)
            return id
        }
        function pdfUrl() { return FixtureUrl + "/daily-document.pdf" }
        function attachmentUrl() { return FixtureUrl + "/daily-attachment.pdf" }
        function go(id, path) {
            var page = deck.browserForSession(id)
            verify(deck.navigateSession(id, FixtureUrl + path, 0, null, null),
                "production navigation must accept " + path)
            loaded(page, path)
            return page
        }
        function inlinePdf(id) {
            var before = deck.downloads ? deck.downloads.items.length : 0
            var page = go(id, "/daily-document.pdf")
            wait(250)
            // The inline viewer can send a second load-status transition after
            // HTML navigation first reports ready. Require its settled state.
            loaded(page, "/daily-document.pdf")
            compare(deck.downloads.items.length, before, "inline PDF must not download")
            return page
        }
        function js(page, source) {
            var done = false, result
            verify(page.evaluateJavaScript(source, function(value) {
                result = value
                done = true
            }), "production wrapper must admit a script")
            tryVerify(function() { return done }, 5000)
            return result
        }
        function printBytes(path) {
            var bytes = new Uint8Array(Fixture.downloadedContent(path))
            var magic = bytes.length >= 4
                ? String.fromCharCode(bytes[0], bytes[1], bytes[2], bytes[3]) : ""
            return {length:bytes.length, magic:magic}
        }
        function countVisits(url) {
            return FeriaAccount.browserHistory.filter(function(entry) {
                return String(entry.url) === url
            }).length
        }

        function test_inlinePdfNavigation() {
            var id = open("/a", false)
            var page = inlinePdf(id)
            compare(String(page.url), pdfUrl())
            compare(page.documentReady, true)
            console.log("DAILY_ASSERT_INLINE url=" + String(page.url)
                + " documentReady=" + page.documentReady
                + " downloads=" + deck.downloads.items.length)
        }

        function test_backForwardAcrossInlinePdf() {
            var id = open("/a", false)
            var page = inlinePdf(id)
            tryVerify(function() { return page.navCanGoBack }, 5000)
            verify(deck.back(id), "Back must be accepted from the PDF")
            loaded(page, "/a")
            tryVerify(function() { return page.navCanGoForward }, 5000)
            verify(deck.forward(id), "Forward must be accepted back to the PDF")
            loaded(page, "/daily-document.pdf")
            compare(deck.downloads.items.length, 0)
            console.log("DAILY_ASSERT_PDF_HISTORY_NAV back=/a forward=/daily-document.pdf downloads=0")
        }

        function test_singlePdfHistoryVisit() {
            var id = open("/a", false)
            tryCompare(FeriaAccount.browserHistory, "length", 1)
            inlinePdf(id)
            tryCompare(FeriaAccount.browserHistory, "length", 2)
            compare(countVisits(pdfUrl()), 1, "a PDF is a single canonical history visit")
            compare(String(FeriaAccount.browserHistory[0].url), pdfUrl())
            console.log("DAILY_ASSERT_PDF_VISIT historyLength="
                + FeriaAccount.browserHistory.length + " pdfRows=" + countVisits(pdfUrl()))
        }

        function test_attachmentDownloadsWithoutNavigation() {
            var id = open("/a", false)
            var page = deck.browserForSession(id)
            var before = deck.downloads.items.length
            verify(deck.navigateSession(id, attachmentUrl(), 0, null, null))
            tryVerify(function() { return deck.downloads.items.length === before + 1 }, 10000)
            var item = deck.downloads.items[before]
            tryCompare(item, "completed", true, 15000)
            compare(item.name, "daily-attachment.pdf")
            var file = printBytes(item.path)
            verify(file.length > 300, "the attachment must be saved inside Fixture.downloadRoot")
            compare(file.magic, "%PDF")
            tryVerify(function() {
                return !page.loading && String(page.url) === FixtureUrl + "/a"
            }, 10000, "attachment must not replace the source page")
            compare(FeriaAccount.browserHistory.length, 1)
            console.log("DAILY_ASSERT_ATTACHMENT filename=" + item.name
                + " path=" + item.path + " bytes=" + file.length
                + " magic=" + file.magic + " page=" + String(page.url))
        }

        function test_privatePdfIsInlineAndUnrecorded() {
            var id = open("/a", true)
            var page = deck.browserForSession(id)
            compare(page.profile.offTheRecord, true)
            compare(FeriaAccount.browserHistory.length, 0)
            var before = privateDownloadSpy.count
            page = go(id, "/daily-document.pdf")
            wait(250)
            compare(privateDownloadSpy.count, before, "a private inline PDF must not download")
            compare(FeriaAccount.browserHistory.length, 0, "private PDF visits are not stored")
            console.log("DAILY_ASSERT_PRIVATE_PDF url=" + String(page.url)
                + " documentReady=" + page.documentReady + " history="
                + FeriaAccount.browserHistory.length + " downloadSignals="
                + (privateDownloadSpy.count - before))
        }

        function test_pluginNavigatorExposure() {
            var id = open("/a", false)
            var page = deck.browserForSession(id)
            var script = "JSON.stringify({count:navigator.plugins.length,"
                + "pdfViewerEnabled:typeof navigator.pdfViewerEnabled==='undefined'"
                + "?'undefined':navigator.pdfViewerEnabled})"
            var enabled = js(page, script)
            compare(page.settings.pluginsEnabled, true)
            page.settings.pluginsEnabled = false
            var generation = page.navigationGeneration
            verify(deck.reload(id))
            tryVerify(function() {
                return page.navigationGeneration > generation && page.documentReady
                    && !page.loading && !page.replacementLoadPending
                    && String(page.url) === FixtureUrl + "/a"
            }, 15000)
            var disabled = js(page, script)
            page.settings.pluginsEnabled = true
            generation = page.navigationGeneration
            verify(deck.reload(id))
            tryVerify(function() {
                return page.navigationGeneration > generation && page.documentReady
                    && !page.loading && !page.replacementLoadPending
                    && String(page.url) === FixtureUrl + "/a"
            }, 15000)
            var restored = js(page, script)
            console.log("DAILY_PLUGIN_NAVIGATOR enabled=" + enabled
                + " disabled=" + disabled + " restored=" + restored)
        }

        function test_pdfPrintObservation() {
            var id = open("/a", false)
            var page = inlinePdf(id)
            var requests = page.browserRequests
            printMessageSpy.target = requests
            printMessageSpy.clear()
            requests.printPage()
            var offered = requests.printTicket !== null
            var path = Fixture.downloadRoot + "/daily-viewer-print.pdf"
            var accepted = offered ? requests.savePrint("file:///" + path) : false
            for (var i = 0; i < 100 && accepted && printMessageSpy.count === 0; ++i)
                wait(200)
            var message = printMessageSpy.count
                ? String(printMessageSpy.signalArguments[printMessageSpy.count - 1][0]) : ""
            var file = printBytes(path)
            // Observation only: do not conflate engine PDF printing with the
            // accepted in-tab viewer/navigation contract.
            console.log("DAILY_PDF_PRINT offered=" + offered + " saveAccepted=" + accepted
                + " response=" + JSON.stringify(message)
                + " bytes=" + file.length + " magic=" + file.magic)
        }
        // Every action below is driven by the real WebEngineView in the
        // disposable loopback fixture. Clipboard actions are NEVER executed.
        function menuFor(page, x, y) {
            mouseClick(page, x, y, Qt.RightButton)
            var menu = page.pageMenu
            tryCompare(menu, "visible", true, 4000)
            return menu
        }
        function menuRows(menu, expected) {
            var actual = menu.visibleEntries()
            compare(actual.join("|"), expected.join("|"), "exactly the applicable menu items")
            for (var i = 0; i < expected.length; ++i) {
                var item = menu.entry(expected[i])
                verify(!!item && item.visible && item.objectName === expected[i])
            }
        }
        function menuStructure(menu) {
            var rows = []
            // Inspect the actual MenuItem and MenuSeparator sequence, not a
            // predicted group list (hidden separators must never be counted).
            for (var i = 0; i < menu.count; ++i) {
                var item = menu.itemAt(i)
                if (item && item.visible) rows.push(String(item.objectName))
            }
            return rows
        }
        function checkMenuStructure(menu, expected, message) {
            var observed = menuStructure(menu)
            compare(observed.join("|"), expected.join("|"), message)
            return observed
        }
        function choose(menu, objectName) {
            var item = menu.entry(objectName)
            verify(item !== null && item.visible && item.enabled,
                "visible, enabled menu entry: " + objectName)
            mouseClick(item)
        }
        function menuLabelsInOverlay() {
            var found = []
            function walk(item, depth) {
                if (!item || depth > 6) return
                if (item.visible && typeof item.text === "string" && item.text.length)
                    found.push(String(item.text))
                var children = item.children || []
                for (var i = 0; i < children.length; ++i) walk(children[i], depth + 1)
            }
            walk(Overlay.overlay, 0)
            return found
        }
        function test_context_emptyNoStock() {
            var id = open("/daily", false)
            var page = deck.browserForSession(id)
            var menu = menuFor(page, 430, 45)
            menuRows(menu, ["feriaPageMenuBack","feriaPageMenuForward",
                "feriaPageMenuReload"])
            var layout = checkMenuStructure(menu, [
                "feriaPageMenuBack","feriaPageMenuForward","feriaPageMenuReload"],
                "empty page has only three navigation rows, no separator")
            console.log("DAILY_ASSERT_MENU_EMPTY_STRUCTURE " + layout.join(","))
            var overlay = menuLabelsInOverlay()
            verify(overlay.indexOf("Back") >= 0 && overlay.indexOf("Reload") >= 0)
            for (var unwanted of ["Save page", "View page source",
                "Open link in new window", "Inspect", "Undo", "Redo"]) {
                verify(overlay.indexOf(unwanted) < 0, "no Qt stock action: " + unwanted)
            }
            compare(menu.entry("feriaPageMenuBack").enabled, false)
            compare(menu.entry("feriaPageMenuForward").enabled, false)
            console.log("DAILY_ASSERT_MENU_EMPTY rows=" + menu.visibleEntries().join(",")
                + " stockActions=0 backEnabled=false forwardEnabled=false")
        }
        function test_context_backAndForwardRealHistory() {
            var id = open("/a", false)
            var page = go(id, "/b")
            tryVerify(function() { return page.navCanGoBack }, 5000)
            var menu = menuFor(page, 420, 45)
            compare(menu.entry("feriaPageMenuBack").enabled, true)
            choose(menu, "feriaPageMenuBack")
            loaded(page, "/a")
            tryVerify(function() { return page.navCanGoForward }, 5000)
            menu = menuFor(page, 420, 45)
            compare(menu.entry("feriaPageMenuForward").enabled, true)
            choose(menu, "feriaPageMenuForward")
            loaded(page, "/b")
            var freshId = open("/a", false)
            var fresh = deck.browserForSession(freshId)
            menu = menuFor(fresh, 420, 45)
            compare(menu.entry("feriaPageMenuBack").enabled, false)
            console.log("DAILY_ASSERT_MENU_HISTORY back=/a forward=/b"
                + " noHistoryBackEnabled=" + menu.entry("feriaPageMenuBack").enabled)
        }
        function test_context_reloadAdvancesNavigation() {
            var id = open("/a", false)
            var page = deck.browserForSession(id)
            var before = page.navigationGeneration
            var menu = menuFor(page, 420, 45)
            choose(menu, "feriaPageMenuReload")
            tryVerify(function() {
                return page.navigationGeneration > before && page.documentReady
                    && !page.loading && !page.replacementLoadPending
                    && String(page.url) === FixtureUrl + "/a"
            }, 15000)
            console.log("DAILY_ASSERT_MENU_RELOAD before=" + before
                + " after=" + page.navigationGeneration + " ready=" + page.documentReady)
        }
        function test_context_linkOpensOwnedTab() {
            var id = open("/daily", false)
            var source = deck.browserForSession(id)
            var before = Sessions.browsers().length
            var menu = menuFor(source, 70, 95)
            menuRows(menu, ["feriaPageMenuOpenLink","feriaPageMenuCopyLinkAddress",
                "feriaPageMenuBack","feriaPageMenuForward","feriaPageMenuReload"])
            var layout = checkMenuStructure(menu, [
                "feriaPageMenuOpenLink","feriaPageMenuCopyLinkAddress",
                "feriaPageMenuBeforeNavigationDivider",
                "feriaPageMenuBack","feriaPageMenuForward","feriaPageMenuReload"],
                "link and navigation have exactly one separator")
            console.log("DAILY_ASSERT_MENU_LINK_STRUCTURE " + layout.join(","))
            verify(menu.entry("feriaPageMenuCopyLinkAddress").enabled,
                "Copy link address is wired; deliberately not triggered")
            choose(menu, "feriaPageMenuOpenLink")
            tryVerify(function() { return Sessions.browsers().length === before + 1 }, 10000)
            var added = Sessions.browsers().filter(function(row) { return row.id !== id })
            compare(added.length, 1, "one new canonical tab must be owned by the deck")
            var targetId = added[0].id
            verify(targetId !== id)
            verify(deck.activate(targetId), "background tab can be activated explicitly")
            var target = deck.browserForSession(targetId)
            loaded(target, "/b")
            compare(String(source.url), FixtureUrl + "/daily")
            console.log("DAILY_ASSERT_MENU_LINK before=" + before
                + " after=" + Sessions.browsers().length
                + " source=" + source.url + " target=" + target.url
                + " copyLink=wired-not-action-proven")
        }
        function test_context_imageSaveExactBytes() {
            var id = open("/daily", false)
            var page = deck.browserForSession(id)
            // Must be installed before any action with download potential.
            deck.sharedProfile.downloadPath = Fixture.downloadRoot
            tryVerify(function() {
                return js(page, "document.getElementById('dailyImage').complete") === true
            }, 5000)
            var before = deck.downloads.items.length
            var menu = menuFor(page, 65, 175)
            menuRows(menu, ["feriaPageMenuOpenImage","feriaPageMenuSaveImage",
                "feriaPageMenuCopyImageAddress","feriaPageMenuBack",
                "feriaPageMenuForward","feriaPageMenuReload"])
            verify(menu.entry("feriaPageMenuCopyImageAddress").enabled,
                "Copy image address is wired; deliberately not triggered")
            choose(menu, "feriaPageMenuSaveImage")
            tryVerify(function() {
                return deck.downloads.items.length === before + 1
            }, 10000, "Save image must reach the existing download owner")
            var item = deck.downloads.items[before]
            tryCompare(item, "completed", true, 15000)
            verify(item.path.indexOf(Fixture.downloadRoot) === 0)
            var data = new Uint8Array(Fixture.downloadedContent(item.path))
            var signature = Array.from(data.slice(0, 8)).join(",")
            compare(signature, "137,80,78,71,13,10,26,10", "PNG signature intact")
            compare(data.length, 70, "saved bytes must equal loopback fixture PNG")
            console.log("DAILY_ASSERT_MENU_IMAGE_SAVE file=" + item.path
                + " bytes=" + data.length + " signature=" + signature
                + " copyImage=wired-not-action-proven")
        }
        function test_context_imageOpensTab() {
            var id = open("/daily", false)
            var source = deck.browserForSession(id)
            var before = Sessions.browsers().length
            var menu = menuFor(source, 65, 175)
            choose(menu, "feriaPageMenuOpenImage")
            tryVerify(function() { return Sessions.browsers().length === before + 1 }, 10000)
            var targetId = Sessions.activeId
            verify(targetId !== id)
            loaded(deck.browserForSession(targetId), "/daily-image.png")
            compare(String(source.url), FixtureUrl + "/daily")
            console.log("DAILY_ASSERT_MENU_IMAGE_TAB source=" + source.url
                + " target=" + deck.browserForSession(targetId).url
                + " tabs=" + Sessions.browsers().length)
        }
        function test_context_editableFlagsAndSelectAll() {
            var id = open("/daily", false)
            var page = deck.browserForSession(id)
            var menu = menuFor(page, 65, 270)
            menuRows(menu, ["feriaPageMenuCut","feriaPageMenuCopy",
                "feriaPageMenuPaste","feriaPageMenuSelectAll",
                "feriaPageMenuBack","feriaPageMenuForward","feriaPageMenuReload"])
            var layout = checkMenuStructure(menu, [
                "feriaPageMenuCut","feriaPageMenuCopy","feriaPageMenuPaste",
                "feriaPageMenuSelectAll","feriaPageMenuBeforeNavigationDivider",
                "feriaPageMenuBack","feriaPageMenuForward","feriaPageMenuReload"],
                "editable and navigation have exactly one separator")
            console.log("DAILY_ASSERT_MENU_EDIT_STRUCTURE " + layout.join(","))
            compare(menu.isEditable, true)
            compare(menu.entry("feriaPageMenuCut").enabled, false)
            compare(menu.entry("feriaPageMenuCopy").enabled, false)
            var pasteEnabled = menu.entry("feriaPageMenuPaste").enabled
            // Paste only inspected: never call it or read the user's clipboard.
            compare(pasteEnabled, (menu.editFlags & 16) !== 0)
            compare(menu.entry("feriaPageMenuSelectAll").enabled, true)
            choose(menu, "feriaPageMenuSelectAll")
            var state = null
            for (var attempt = 0; attempt < 20; ++attempt) {
                state = js(page, "(function(){var e=document.getElementById('dailyEdit');"
                    + "return {start:e.selectionStart,end:e.selectionEnd,len:e.value.length,"
                    + "active:document.activeElement===e};})()")
                if (state.start === 0 && state.end === state.len) break
                wait(75)
            }
            console.log("DAILY_EDIT_SELECT_RESULT " + JSON.stringify(state)
                + " flags=" + menu.editFlags)
            compare(state.start, 0)
            compare(state.end, state.len)
            console.log("DAILY_ASSERT_MENU_EDITABLE flags=" + menu.editFlags
                + " cut=false copy=false pasteEnabled=" + pasteEnabled
                + " selectRange=" + state.start + ".." + state.end
                + " cutCopyPaste=wired-not-action-proven")
        }
        function test_context_selectedTextAtRealRectangle() {
            var id = open("/daily", false)
            var page = deck.browserForSession(id)
            var rect = js(page, "(function(){var e=document.getElementById('dailyText');"
                + "var r=document.createRange();r.selectNodeContents(e.firstChild);"
                + "var sel=window.getSelection();sel.removeAllRanges();sel.addRange(r);"
                + "var box=r.getBoundingClientRect();"
                + "return {x:Math.round(box.x+Math.min(20,box.width/3)),"
                + "y:Math.round(box.y+box.height/2),w:box.width,h:box.height,"
                + "selected:sel.toString()};})()")
            verify(rect.w > 60 && rect.h > 5 && rect.selected.length > 0,
                "selection rectangle is from the actual text range")
            var menu = menuFor(page, rect.x, rect.y)
            var requested = String(menu.selectedText)
            console.log("DAILY_CONTEXT_SELECTION_AT_RECT x=" + rect.x + " y=" + rect.y
                + " width=" + rect.w + " height=" + rect.h
                + " dom=" + JSON.stringify(rect.selected)
                + " request=" + JSON.stringify(requested))
            compare(requested, rect.selected,
                "Qt's actual right-click request must retain the selection at its computed rectangle")
            menuRows(menu, ["feriaPageMenuCopy","feriaPageMenuBack",
                "feriaPageMenuForward","feriaPageMenuReload"])
            verify(menu.entry("feriaPageMenuCopy").enabled)
            menu.cancel()
            menu = menuFor(page, 430, 45)
            menuRows(menu, ["feriaPageMenuBack","feriaPageMenuForward",
                "feriaPageMenuReload"])
            console.log("DAILY_ASSERT_MENU_SELECTION selected=" + JSON.stringify(requested)
                + " copyPresent=true plainCopyPresent=false copy=wired-not-action-proven")
        }
        function test_context_navigationWithdrawsLateAction() {
            var id = open("/a", false)
            var page = deck.browserForSession(id)
            var menu = menuFor(page, 420, 45)
            verify(menu.visible && menu.contextActive)
            go(id, "/b")
            tryCompare(menu, "visible", false, 5000)
            compare(menu.contextActive, false)
            var before = page.navigationGeneration
            menu.navigate("back") // stale invocation must be refused
            compare(String(page.url), FixtureUrl + "/b")
            compare(page.navigationGeneration, before)
            console.log("DAILY_ASSERT_MENU_WITHDRAW closed=true lateBackNoOp=true"
                + " generation=" + before)
        }
        function test_context_hiddenTabWithdraws() {
            var id = open("/daily", false)
            var page = deck.browserForSession(id)
            var menu = menuFor(page, 430, 45)
            var next = open("/a", false)
            verify(next !== id)
            tryCompare(menu, "visible", false, 5000)
            compare(menu.contextActive, false)
            console.log("DAILY_ASSERT_MENU_HIDDEN closed=true sourceVisible=" + page.visible)
        }

        function test_context_disabledTextColorUnderBasicStyle() {
            var id = open("/a", false)
            var page = go(id, "/b")
            tryVerify(function() { return page.navCanGoBack && !page.navCanGoForward }, 5000)
            var menu = menuFor(page, 340, 45)
            var back = menu.entry("feriaPageMenuBack")
            var forward = menu.entry("feriaPageMenuForward")
            compare(back.enabled, true)
            compare(forward.enabled, false)
            verify(back.contentItem !== null && forward.contentItem !== null)
            var backColor = String(back.contentItem.color)
            var forwardColor = String(forward.contentItem.color)
            compare(backColor, String(menuTheme.ink), "enabled row's actual text uses ink")
            compare(forwardColor, String(menuTheme.inkDimmer),
                "disabled row's actual text uses inkDimmer")
            verify(backColor !== forwardColor, "disabled row is visibly dimmer")
            console.log("DAILY_ASSERT_MENU_DISABLED_COLOR back=" + backColor
                + " forward=" + forwardColor + " ink=" + String(menuTheme.ink))
        }
        function test_context_imageWithinLinkHasThreeGroups() {
            var id = open("/daily", false)
            var page = deck.browserForSession(id)
            tryVerify(function() {
                return js(page, "document.getElementById('dailyImage').complete") === true
            }, 5000)
            var wrapper = js(page, "(function(){var image=document.getElementById('dailyImage');"
                + "var link=document.createElement('a');link.href='/b';"
                + "image.parentNode.insertBefore(link,image);link.appendChild(image);"
                + "return image.parentElement.tagName;})()")
            compare(wrapper, "A")
            var menu = menuFor(page, 65, 175)
            compare(menu.hasLink, true)
            compare(menu.hasImage, true)
            menuRows(menu, ["feriaPageMenuOpenLink","feriaPageMenuCopyLinkAddress",
                "feriaPageMenuOpenImage","feriaPageMenuSaveImage",
                "feriaPageMenuCopyImageAddress","feriaPageMenuBack",
                "feriaPageMenuForward","feriaPageMenuReload"])
            var layout = checkMenuStructure(menu, [
                "feriaPageMenuOpenLink","feriaPageMenuCopyLinkAddress",
                "feriaPageMenuLinkImageDivider",
                "feriaPageMenuOpenImage","feriaPageMenuSaveImage",
                "feriaPageMenuCopyImageAddress","feriaPageMenuBeforeNavigationDivider",
                "feriaPageMenuBack","feriaPageMenuForward","feriaPageMenuReload"],
                "linked image has three groups and exactly two separators")
            console.log("DAILY_ASSERT_MENU_IMAGE_LINK_STRUCTURE " + layout.join(","))
        }
        function test_context_requestPositionUsed() {
            var id = open("/a", false)
            var page = deck.browserForSession(id)
            var requestedX = 340
            var requestedY = 64
            var menu = menuFor(page, requestedX, requestedY)
            verify(Math.abs(menu.x - requestedX) <= 4,
                "menu x must follow the browser request, not mouse cursor")
            verify(Math.abs(menu.y - requestedY) <= 4,
                "menu y must follow the browser request, not mouse cursor")
            console.log("DAILY_ASSERT_MENU_POSITION request=" + requestedX + "," + requestedY
                + " opened=" + menu.x + "," + menu.y)
        }
        function test_context_edgePositionClampedInsideBrowser() {
            var id = open("/a", false)
            var page = deck.browserForSession(id)
            var x = page.width - 25
            var y = page.height - 25
            var menu = menuFor(page, x, y)
            verify(menu.x >= -1 && menu.y >= -1, "menu must not start outside browser")
            verify(menu.x + menu.width <= page.width + 2, "menu must fit right edge")
            verify(menu.y + menu.height <= page.height + 2, "menu must fit bottom edge")
            console.log("DAILY_ASSERT_MENU_EDGE request=" + x + "," + y
                + " bounds=" + menu.x + "," + menu.y
                + "," + menu.width + "," + menu.height
                + " browser=" + page.width + "," + page.height)
        }

    }
}
