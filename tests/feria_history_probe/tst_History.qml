import QtQuick
import QtQuick.Window
import QtTest
import QtWebEngine
import "../../qml/feria" as Feria

// Real-engine cases for browser set 2: visit recording and history/bookmark
// management through the production deck, QtWebEngine wrapper, browser menu
// and the real FeriaAccountStore. Loopback fixture and disposable profiles.
Item {
    id: root
    width: 1060; height: 760
    Item { id: embedded; x: 25; y: 125; width: 820; height: 580 }
    Feria.FeriaSessionDeck {
        id: deck
        parent: embedded
        anchors.fill: parent
        sessions: Sessions; accountStore: FeriaAccount; policy: FeriaBrowserPolicy
        displayed: true
    }
    Feria.FeriaBrowserMenu {
        id: menu
        parent: root
        deck: deck
        accountStore: FeriaAccount
        providerId: "mangaplus"
        currentTitle: deck.activeBrowser ? deck.activeBrowser.title : ""
        currentUrl: deck.activeBrowser ? String(deck.activeBrowser.url) : ""
        // What the host view does with a chosen bookmark or history row.
        onNavigateRequested: (url) => deck.navigateInput(Sessions.activeId, url)
    }
    TestCase {
        id: tests
        name: "FeriaHistory"
        when: windowShown
        property int serial: 0
        readonly property var runtime: FeriaBrowserPolicy.runtime
        SignalSpy { id: commandSpy; target: tests.runtime; signalName: "commandFinished" }
        SignalSpy { id: settledSpy; target: Sessions.browserOperations(); signalName: "operationSettled" }
        SignalSpy { id: quiescedSpy; target: deck; signalName: "quiesced" }
        SignalSpy { id: cleanedSpy; signalName: "finished" }
        Component { id: cleanerFactory; Feria.FeriaSessionCleaner {} }

        function init() {
            failOnWarning(/TypeError|ReferenceError|incompatible arguments|Could not convert argument/)
            deck.startQuiescence(false)
            tryCompare(deck, "quiescing", false, 15000)
            Fixture.switchProfile("history-" + (++serial))
            tryCompare(deck, "quiescing", false, 15000)
            deck.displayed = true; deck.suppressed = false
            commandSpy.clear(); settledSpy.clear()
        }
        function cleanup() {
            menu.close()
            tryCompare(menu, "visible", false)
            deck.startQuiescence(false)
            tryCompare(deck, "quiescing", false, 15000)
            compare(FeriaBrowserPolicy.runtimeViewCount, 0)
            compare(FeriaBrowserPolicy.runtimeProfileCount, 0)
        }
        function descriptor(path, extra) {
            return Object.assign({providerId: "mangaplus", providerName: "Fixture",
                url: FixtureUrl + path, startUrl: FixtureUrl + "/home", mode: "read"}, extra || {})
        }
        // The engine reports a finished load one event-loop turn after it stops
        // loading; a visit exists only once that report has been handled.
        function settled(id, path) {
            tryVerify(function() { var p = deck.browserForSession(id)
                return p && p.documentReady && !p.loading && !p.replacementLoadPending
                    && String(p.url) === FixtureUrl + path }, 15000,
                "Expected " + path + " to finish loading")
            return deck.browserForSession(id)
        }
        function after(id, path, step, action) {
            var page = deck.browserForSession(id), generation = page.navigationGeneration
            verify(action(), step + " must be accepted")
            tryVerify(function() { return page.navigationGeneration > generation && page.documentReady
                && !page.loading && !page.replacementLoadPending && String(page.url) === FixtureUrl + path }, 15000,
                "Expected " + path + " to finish loading after " + step)
            return page
        }
        function open(path, extra) {
            var id = deck.open(descriptor(path, extra), true, false, true)
            verify(id.length > 0)
            settled(id, path)
            return id
        }
        function js(page, source) {
            var done = false, result
            verify(page.evaluateJavaScript(source, function(value) { result = value; done = true }))
            tryVerify(function() { return done }, 5000)
            return result
        }
        function paths(rows) {
            return rows.map(function(row) { return String(row.url).replace(FixtureUrl, "") }).join(" ")
        }
        function history() { return paths(FeriaAccount.browserHistory) }
        function outcomes() {
            var seen = []
            for (var i = 0; i < settledSpy.count; ++i) {
                var result = Sessions.browserOperations().operationResult(settledSpy.signalArguments[i][0])
                seen.push(result.kind + ":" + result.outcome)
            }
            return seen
        }
        function control(name) {
            var result = findChild(menu, name)
            for (var listName of ["feriaBrowserTabsList", "feriaBrowserBookmarksList", "feriaBrowserHistoryList"]) {
                if (result) break
                var list = findChild(menu, listName)
                if (!list) continue
                for (var i = 0; i < list.count && !result; ++i) {
                    var row = list.itemAtIndex(i)
                    if (row) result = findChild(row, name)
                }
            }
            verify(result !== null, name + " exists")
            return result
        }
        function show(section) {
            menu.openSection(section)
            tryCompare(menu, "opened", true)
            waitForRendering(menu.contentItem)
        }

        function test_visitsAcrossRedirectReloadBackAndForward() {
            var a = open("/a")
            tryCompare(FeriaAccount.browserHistory, "length", 1)
            compare(FeriaAccount.browserHistory[0].title, "Fixture /a")
            // The server answers /redirect with a 302 to /b: only /b was visited.
            after(a, "/b", "redirect", function() { return deck.navigateSession(a, FixtureUrl + "/redirect", 0, null, null) })
            compare(history(), "/b /a")
            after(a, "/b", "reload", function() { return deck.reload(a) })
            compare(history(), "/b /a", "a reload refreshes its row")
            after(a, "/c", "typed address", function() { return deck.navigateInput(a, FixtureUrl + "/c") })
            compare(history(), "/c /b /a")
            var idOfB = FeriaAccount.browserHistory[1].id

            // Tab history (Back/Forward) and visit history are separate: going
            // back revisits /b, which lifts its one row; nothing is duplicated.
            settledSpy.clear()
            tryVerify(function() { return deck.navigationForSession(a).canGoBack }, 5000)
            after(a, "/b", "back", function() { return deck.back(a) })
            compare(history(), "/b /c /a")
            compare(FeriaAccount.browserHistory[0].id, idOfB)
            console.log("ledger outcomes for Back:", outcomes().join(" "))
            settledSpy.clear()
            tryVerify(function() { return deck.navigationForSession(a).canGoForward }, 5000)
            after(a, "/c", "forward", function() { return deck.forward(a) })
            compare(history(), "/c /b /a")
            console.log("ledger outcomes for Forward:", outcomes().join(" "))
            // The tab can still go back: its own stack was never rewritten.
            tryVerify(function() { return deck.navigationForSession(a).canGoBack }, 5000)

            // A second tab at an address already visited adds no row.
            open("/a")
            tryCompare(FeriaAccount.browserHistory, "length", 3)
            compare(history(), "/a /c /b")
        }

        function test_lateTitleNamesItsOwnVisitOnly() {
            var a = open("/a"), page = deck.browserForSession(a), entry = deck.entries[a]
            tryCompare(FeriaAccount.browserHistory, "length", 1)
            var document = page.navigationGeneration
            js(page, "setTimeout(function(){ document.title = 'Settled late' }, 250); true")
            tryVerify(function() { return FeriaAccount.browserHistory[0].title === "Settled late" }, 5000,
                "a title set after the load must reach the recorded visit")
            compare(FeriaAccount.browserHistory.length, 1)

            // The page itself moves on, so the tab keeps its visit token; only
            // the document is new.
            var generation = page.navigationGeneration
            js(page, "setTimeout(function(){ location.href = '/b' }, 50); true")
            tryVerify(function() { return page.navigationGeneration > generation && page.documentReady
                && !page.loading && !page.replacementLoadPending && String(page.url) === FixtureUrl + "/b" }, 15000)
            tryCompare(FeriaAccount.browserHistory, "length", 2)
            compare(history(), "/b /a")
            verify(!FeriaAccount.updateBrowserVisitTitle(a, entry.token, document, "From the old document"))
            compare(FeriaAccount.browserHistory[1].title, "Settled late")
            compare(FeriaAccount.browserHistory[0].title, "Fixture /b")
            js(page, "document.title = 'B renamed'; true")
            tryVerify(function() { return FeriaAccount.browserHistory[0].title === "B renamed" }, 5000)
            compare(FeriaAccount.browserHistory[1].title, "Settled late")
        }

        function test_privateTabsLeaveNoHistoryButCanBookmark() {
            var normal = open("/a")
            tryCompare(FeriaAccount.browserHistory, "length", 1)
            var id = open("/private", {privateBrowsing: true}), page = deck.browserForSession(id)
            compare(page.profile.offTheRecord, true)
            after(id, "/private-next", "private navigation",
                  function() { return deck.navigateInput(id, FixtureUrl + "/private-next") })
            js(page, "document.title = 'Private retitled'; true")
            wait(300)
            after(id, "/private-next", "private reload", function() { return deck.reload(id) })
            compare(history(), "/a", "private pages never reach history")
            verify(!FeriaAccount.recordBrowserVisit(id, deck.entries[id].token, FixtureUrl + "/private", "Forced", 99))

            // Saving a bookmark is an explicit act and stays allowed.
            compare(Sessions.activeId, id)
            show("bookmarks")
            verify(control("feriaBrowserPrivacy").text.indexOf("Private") >= 0)
            mouseClick(control("feriaBrowserBookmarkCurrent"))
            compare(paths(FeriaAccount.browserBookmarks), "/private-next")
            compare(history(), "/a")
        }

        function test_accountSwitchIsolatesAndPersists() {
            var first = "history-" + serial
            var a = open("/a"), token = deck.entries[a].token
            tryCompare(FeriaAccount.browserHistory, "length", 1)
            var generation = FeriaAccount.browserGeneration
            verify(FeriaAccount.setBrowserBookmark(FixtureUrl + "/a", "First account", true, generation))
            show("history")
            compare(menu.rowsGeneration, generation)

            Fixture.switchProfile(first + "-other")
            tryCompare(menu, "visible", false)
            tryCompare(deck, "quiescing", false, 15000)
            verify(FeriaAccount.browserGeneration !== generation)
            compare(FeriaAccount.browserHistory.length, 0)
            compare(FeriaAccount.browserBookmarks.length, 0)
            var b = open("/b")
            tryCompare(FeriaAccount.browserHistory, "length", 1)
            verify(FeriaAccount.setBrowserBookmark(FixtureUrl + "/b", "Second account", true, FeriaAccount.browserGeneration))

            // Everything still holding the first account's identity is refused.
            verify(!FeriaAccount.removeBrowserHistoryEntry(generation, FixtureUrl + "/b"))
            verify(!FeriaAccount.clearBrowserHistory(generation))
            verify(!FeriaAccount.removeBrowserBookmark(generation, FixtureUrl + "/b"))
            verify(!FeriaAccount.setBrowserBookmark(FixtureUrl + "/c", "Stale", true, generation))
            compare(FeriaAccount.editBrowserBookmark(generation, FixtureUrl + "/b", FixtureUrl + "/c", "Stale").reason, "stale-account")
            verify(!FeriaAccount.recordBrowserVisit(a, token, FixtureUrl + "/c", "Stale", 99))
            verify(!FeriaAccount.updateBrowserVisitTitle(a, token, 1, "Stale"))
            compare(history(), "/b")
            compare(paths(FeriaAccount.browserBookmarks), "/b")
            compare(FeriaAccount.browserBookmarks[0].title, "Second account")

            // Back to the first account: read again from its own file.
            deck.startQuiescence(false)
            tryCompare(deck, "quiescing", false, 15000)
            Fixture.switchProfile(first)
            tryCompare(deck, "quiescing", false, 15000)
            compare(history(), "/a")
            compare(paths(FeriaAccount.browserBookmarks), "/a")
            compare(FeriaAccount.browserBookmarks[0].title, "First account")
        }

        // Site-data control: signing out of one site, the way the shell does
        // it (close the browsers, clean, reopen), removes that site's cookies
        // and storage and nothing else.
        function test_signOutClearsOneSiteAndNothingElse() {
            var site = FixtureUrl, other = FixtureUrl.replace("127.0.0.1", "localhost")
            var mark = "document.cookie='marker=kept; max-age=86400; path=/';"
                + "localStorage.setItem('marker','kept'); document.cookie + '|' + localStorage.getItem('marker')"
            var read = "document.cookie + '|' + localStorage.getItem('marker')"
            function openOther() {
                var id = deck.open(descriptor("/b", {url: other + "/b"}), true, false, true)
                verify(id.length > 0, "the second site must be allowed to open")
                tryVerify(function() { var p = deck.browserForSession(id)
                    return p && p.documentReady && !p.loading && !p.replacementLoadPending
                        && String(p.url) === other + "/b" }, 15000, "Expected the second site to finish loading")
                return deck.browserForSession(id)
            }
            compare(js(deck.browserForSession(open("/a")), mark), "marker=kept|kept")
            compare(js(openOther(), mark), "marker=kept|kept")
            verify(FeriaAccount.setBrowserBookmark(site + "/a", "Kept", true, FeriaAccount.browserGeneration))
            tryCompare(FeriaAccount.browserHistory, "length", 2)
            var historyBefore = history(), generation = FeriaAccount.browserGeneration

            quiescedSpy.clear()
            deck.startQuiescence(true)
            tryVerify(function() { return quiescedSpy.count === 1 }, 20000, "browsers must close before cleanup")
            compare(FeriaBrowserPolicy.runtimeViewCount, 0)
            var cleaner = cleanerFactory.createObject(root, {profilePath: FeriaBrowserPolicy.storageRoot,
                engines: ["qtwebengine"], domains: ["127.0.0.1"], origins: [site]})
            cleanedSpy.target = cleaner
            tryVerify(function() { return cleanedSpy.count === 1 }, 70000, "cleanup must finish")
            // The cleaner reports "complete" only when it could also enumerate
            // service workers, which its blank page cannot do in this engine;
            // the shell words that outcome as incomplete. What it did clear is
            // checked below from the pages themselves.
            console.log("cleaner reported complete:", cleanedSpy.signalArguments[0][0])
            cleanedSpy.target = null
            cleaner.destroy()
            deck.finishCleanup()
            tryCompare(deck, "quiescing", false, 15000)

            compare(js(deck.browserForSession(open("/a")), read), "|null", "the signed-out site keeps nothing")
            compare(js(openOther(), read), "marker=kept|kept", "another site is untouched")
            // History, bookmarks and the account itself are not site data.
            compare(FeriaAccount.browserGeneration, generation)
            compare(history(), historyBefore)
            compare(paths(FeriaAccount.browserBookmarks), "/a")
        }

        function test_menuSearchesOpensAndRemovesHistory() {
            var a = open("/alpha")
            after(a, "/beta", "second page", function() { return deck.navigateInput(a, FixtureUrl + "/beta") })
            after(a, "/gamma", "third page", function() { return deck.navigateInput(a, FixtureUrl + "/gamma") })
            compare(history(), "/gamma /beta /alpha")

            show("history")
            var list = control("feriaBrowserHistoryList"), search = control("feriaBrowserHistorySearch")
            var empty = control("feriaBrowserHistoryEmpty")
            compare(list.count, 3)
            verify(!empty.visible)
            search.text = "FIXTURE /beta"                 // title, any case
            compare(list.count, 1)
            compare(control("feriaBrowserHistory_0").text, "Fixture /beta")
            search.text = "127.0.0.1 gamma"               // address words
            compare(list.count, 1)
            compare(control("feriaBrowserHistory_0").text, "Fixture /gamma")
            search.text = "nothing-like-this"
            compare(list.count, 0)
            verify(empty.visible)
            verify(empty.text.indexOf("No visited pages match") === 0, empty.text)
            compare(control("feriaBrowserClearHistory").enabled, true)

            // Remove one entry: the list and the store change at once.
            search.text = "alpha"
            compare(list.count, 1)
            mouseClick(control("feriaBrowserHistoryRemove_0"))
            compare(history(), "/gamma /beta")
            compare(list.count, 0)
            search.text = ""
            compare(list.count, 2)

            // Opening a row goes through the browser command door.
            search.text = "beta"
            commandSpy.clear()
            var page = deck.browserForSession(a), generation = page.navigationGeneration
            mouseClick(control("feriaBrowserHistory_0"))
            tryCompare(menu, "visible", false)
            tryVerify(function() { return page.navigationGeneration > generation && page.documentReady
                && !page.loading && !page.replacementLoadPending && String(page.url) === FixtureUrl + "/beta" }, 15000)
            verify(commandSpy.count >= 1)
            var finished = commandSpy.signalArguments[0][0]
            compare(finished.command, "navigate")
            compare(finished.accepted, true)
            compare(finished.spaceId, a)
            compare(history(), "/beta /gamma")

            show("history")
            compare(control("feriaBrowserHistorySearch").text, "", "a reopened menu starts unfiltered")
            mouseClick(control("feriaBrowserClearHistory"))
            compare(FeriaAccount.browserHistory.length, 0)
            compare(control("feriaBrowserHistoryList").count, 0)
            compare(control("feriaBrowserHistoryEmpty").text, "Pages visited in regular tabs appear here.")
            compare(control("feriaBrowserClearHistory").enabled, false)
        }

        function test_menuEditsSearchesAndRemovesBookmarks() {
            var a = open("/reading")
            var generation = FeriaAccount.browserGeneration
            show("bookmarks")
            var list = control("feriaBrowserBookmarksList"), empty = control("feriaBrowserBookmarksEmpty")
            compare(list.count, 0)
            compare(empty.text, "Save a page to find it here later.")
            verify(empty.visible)
            mouseClick(control("feriaBrowserBookmarkCurrent"))
            compare(list.count, 1)
            compare(control("feriaBrowserBookmarkCurrent").text, "Remove current bookmark")
            verify(FeriaAccount.setBrowserBookmark(FixtureUrl + "/shelf", "Shelf", true, generation))
            compare(list.count, 2, "a change in the store reaches the open menu")
            compare(paths(FeriaAccount.browserBookmarks), "/shelf /reading")

            // Edit: the fields start from the saved values.
            mouseClick(control("feriaBrowserBookmarkEdit_1"))
            var title = control("feriaBrowserBookmarkEditTitle"), address = control("feriaBrowserBookmarkEditAddress")
            var editor = control("feriaBrowserBookmarkEditor"), error = control("feriaBrowserBookmarkEditError")
            verify(editor.visible)
            verify(!list.visible)
            compare(title.text, "Fixture /reading")
            compare(address.text, FixtureUrl + "/reading")
            // A duplicate address is refused and nothing changes.
            address.text = FixtureUrl + "/shelf"
            mouseClick(control("feriaBrowserBookmarkEditSave"))
            verify(editor.visible)
            verify(error.visible)
            compare(error.text, "Another bookmark already uses this address.")
            compare(paths(FeriaAccount.browserBookmarks), "/shelf /reading")
            address.text = "javascript:alert(1)"
            mouseClick(control("feriaBrowserBookmarkEditSave"))
            compare(error.text, "Enter a web address, like example.com.")
            compare(paths(FeriaAccount.browserBookmarks), "/shelf /reading")
            // A valid edit saves, keeps the bookmark's place and shows at once.
            title.text = "Night reading"
            address.text = FixtureUrl + "/reading/night"
            mouseClick(control("feriaBrowserBookmarkEditSave"))
            verify(!editor.visible)
            verify(list.visible)
            compare(paths(FeriaAccount.browserBookmarks), "/shelf /reading/night")
            compare(control("feriaBrowserBookmark_1").text, "Night reading")
            compare(control("feriaBrowserBookmarkCurrent").text, "Bookmark current page",
                "the open page is no longer the bookmarked address")
            // Cancel leaves the bookmark alone.
            mouseClick(control("feriaBrowserBookmarkEdit_0"))
            control("feriaBrowserBookmarkEditTitle").text = "Discarded"
            mouseClick(control("feriaBrowserBookmarkEditCancel"))
            compare(FeriaAccount.browserBookmarks[0].title, "Shelf")

            var search = control("feriaBrowserBookmarkSearch")
            search.text = "night"
            compare(list.count, 1)
            compare(control("feriaBrowserBookmark_0").text, "Night reading")
            search.text = "no such bookmark"
            compare(list.count, 0)
            verify(empty.text.indexOf("No bookmarks match") === 0, empty.text)
            search.text = "shelf"
            mouseClick(control("feriaBrowserBookmarkRemove_0"))
            compare(paths(FeriaAccount.browserBookmarks), "/reading/night")
            compare(list.count, 0)
            search.text = ""
            compare(list.count, 1)

            // Opening a bookmark uses the same command door as the address bar.
            commandSpy.clear()
            var page = deck.browserForSession(a), before = page.navigationGeneration
            mouseClick(control("feriaBrowserBookmark_0"))
            tryCompare(menu, "visible", false)
            tryVerify(function() { return page.navigationGeneration > before && page.documentReady
                && !page.loading && !page.replacementLoadPending && String(page.url) === FixtureUrl + "/reading/night" }, 15000)
            compare(commandSpy.signalArguments[0][0].command, "navigate")
            compare(commandSpy.signalArguments[0][0].accepted, true)
        }
    }
}
