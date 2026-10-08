import QtQuick
import QtTest
import QtWebEngine
import "../../qml/feria" as Feria

Item {
    width: 960; height: 640
    Feria.FeriaSessionDeck {
        id: deck
        anchors.fill: parent
        sessions: Sessions
        accountStore: FeriaAccount
        policy: FeriaBrowserPolicy
        displayed: true
    }
    TestCase {
        name: "FeriaFoundation"
        when: windowShown
        property int serial: 0
        function init() {
            deck.displayed = true
            deck.startQuiescence(false)
            tryCompare(deck, "quiescing", false, 10000)
            Fixture.switchProfile("foundation-" + (++serial))
            tryCompare(deck, "quiescing", false, 10000)
        }
        function cleanup() {
            deck.startQuiescence(false)
            tryCompare(deck, "quiescing", false, 10000)
        }
        function open(path, foreground) {
            var id = Sessions.openBrowser({providerId: "mangaplus", providerName: "Reading",
                url: FixtureUrl + path, startUrl: FixtureUrl + "/home", mode: "read"},
                true, false, foreground !== false)
            verify(id.length > 0)
            tryVerify(function() { return deck.entries[id] && deck.entries[id].browser
                && deck.entries[id].browser.documentReady && !deck.entries[id].browser.loading
                && String(deck.entries[id].browser.url) === FixtureUrl + path
                && deck.entries[id].browser.title === "Fixture " + path }, 15000)
            return id
        }
        function js(page, script) {
            var done = false, value
            if (typeof page.evaluateJavaScript === "function")
                page.evaluateJavaScript(script, function(result) { value = result; done = true })
            else
                page.runJavaScript(script, function(result) { value = result; done = true })
            tryVerify(function() { return done }, 5000)
            return value
        }
        function test_crossOriginAndSameDocumentHistory() {
            var a = open("/a"), page = deck.entries[a].browser
            var other = FixtureUrl.replace("127.0.0.1", "localhost") + "/cross"
            verify(deck.navigateInput(a, other))
            tryVerify(function() { return String(page.url) === other && page.title === "Fixture /cross" && page.documentReady && !page.loading }, 10000)
            var nav = deck.navigationForSession(a)
            tryCompare(nav, "canGoBack", true)
            verify(deck.back(a))
            tryVerify(function() { return String(page.url) === FixtureUrl + "/a" && page.title === "Fixture /a" && page.documentReady && !page.loading }, 10000)
            js(page, "window.sessionMarker=88; history.pushState({},'', '#one'); history.pushState({},'', '#two')")
            tryVerify(function() { return String(page.url).endsWith("#two") && nav.backCount >= 2 })
            verify(deck.historyOffset(a, -1, nav.historyRevision))
            tryVerify(function() { return String(page.url).endsWith("#one") })
            compare(js(page, "window.sessionMarker"), 88)
            verify(!nav.canGoForward || nav.forwardCount > 0)
        }
        function test_privateRequestProfileAndRetirementFence() {
            var a = open("/a")
            var b = deck.newSpace(a, true, true)
            tryVerify(function() { return deck.entries[b] && deck.entries[b].browser.documentReady }, 15000)
            var privateView = deck.entries[b].browser
            verify(privateView.profile.offTheRecord)
            mouseClick(privateView, 80, 220)
            tryVerify(function() { return Sessions.browsers().length === 3 }, 10000)
            var c = Sessions.activeId
            verify(c !== b && c !== a)
            tryVerify(function() { return deck.entries[c] && deck.entries[c].browser.documentReady }, 10000)
            compare(deck.entries[c].browser.profile, privateView.profile)
            verify(Sessions.get(c).target.privateBrowsing)
            Sessions.close(c)
            compare(deck.navigationForSession(c), null)
            verify(!deck.back(c))
            verify(!deck.navigateInput(c, FixtureUrl + "/b"))
        }
        function test_namedTargetAndNoopener() {
            var a = open("/a"), page = deck.entries[a].browser
            js(page, "document.querySelector('button').onclick=function(){window.open('/named','feria-target')}")
            mouseClick(page, 80, 220)
            tryVerify(function() { return Sessions.browsers().length === 2 }, 10000)
            var b = Sessions.browsers().filter(function(r) { return r.id !== a })[0].id
            tryVerify(function() { return deck.entries[b].browser.documentReady }, 10000)
            var target = deck.entries[b].browser
            compare(js(target, "window.name"), "feria-target")
            Sessions.switchTo(a)
            js(page, "document.querySelector('button').onclick=function(){window.open('/named-again','feria-target')}")
            mouseClick(page, 80, 220)
            tryVerify(function() { return target.title === "Fixture /named-again" && target.documentReady }, 10000)
            compare(Sessions.browsers().length, 2)
            compare(deck.entries[b].browser, target)
            Sessions.switchTo(a)
            js(page, "document.querySelector('button').onclick=function(){window.open('/isolated','_blank','noopener')}")
            mouseClick(page, 80, 220)
            tryVerify(function() { return Sessions.browsers().length === 3 }, 10000)
            var c = Sessions.browsers().filter(function(r) { return r.id !== a && r.id !== b })[0].id
            tryVerify(function() { return deck.entries[c].browser.documentReady }, 10000)
            compare(js(deck.entries[c].browser, "window.opener === null"), true)
        }
        function test_siteDialogHasCanonicalNativeDestination() {
            // 6–10 intentionally replaces the old raw opener-owned popup with
            // a canonical independent native host. The hosting suite adds POST,
            // geometry, exact-close, and lifetime regression assertions.
            var a = open("/a"), page = deck.entries[a].browser
            js(page, "document.querySelector('button').onclick=function(){window.open('/dialog','fixture-dialog','width=420,height=300')}")
            mouseClick(page, 80, 220)
            tryVerify(function() { return Sessions.browsers().length === 2 }, 10000)
            var b = Sessions.browsers().filter(function(r) { return r.id !== a })[0].id
            tryVerify(function() { return deck.entries[b] && deck.entries[b].hostWindow
                && deck.entries[b].browser.title === "Fixture /dialog" }, 10000)
            compare(deck.entries[b].browser.profile, page.profile)
            compare(page.popups.length, 0)
            deck.entries[b].hostWindow.close()
            tryVerify(function() { return !deck.entries[b] }, 5000)
        }
        function test_newWindowSurvives_data() {
            return [{tag: "minimize", closeOpener: false}, {tag: "close-opener", closeOpener: true}]
        }
        function test_newWindowSurvives(data) {
            var a = open("/a"), page = deck.entries[a].browser
            var contextSeen = false, destination = -1
            function context(request) { request.accepted = true; contextSeen = true }
            function requested(request) { destination = request.destination }
            page.contextMenuRequested.connect(context)
            page.newWindowRequested.connect(requested)
            mouseClick(page, 45, 80, Qt.RightButton)
            tryVerify(function() { return contextSeen }, 5000)
            page.triggerWebAction(WebEngineView.OpenLinkInNewWindow)
            tryVerify(function() { return destination === WebEngineNewWindowRequest.InNewWindow }, 10000)
            var target = null, id = ""
            tryVerify(function() {
                var records = Sessions.browsers().filter(function(r) { return r.id !== a })
                if (records.length) {
                    id = records[0].id
                    target = deck.entries[id] ? deck.entries[id].browser : null
                } else if (page.popups.length) target = page.popups[0].view
                return target && target.title === "Fixture /b"
            }, 10000)
            compare(target.profile, page.profile)
            js(target, "window.sessionMarker=9191")
            page.contextMenuRequested.disconnect(context)
            page.newWindowRequested.disconnect(requested)
            if (data.closeOpener) {
                Sessions.close(a)
                tryVerify(function() { return !deck.entries[a] }, 10000)
            } else {
                deck.displayed = false
                tryCompare(page, "suppressed", true)
            }
            wait(200) // allow deferred deletion; assertions are about survival
            verify(target !== null, "Committed destination survives opener lifecycle")
            verify(id.length > 0 && deck.entries[id], "Destination has independent canonical ownership")
            compare(deck.entries[id].browser, target)
            compare(js(target, "window.sessionMarker"), 9191)
            compare(String(target.url), FixtureUrl + "/b")
        }
        function test_normalizedNewSpaceStart() {
            var a = open("/a")
            deck.entries[a].descriptor.startUrl = FixtureUrl.replace("http://", "") + "/home"
            var b = deck.newSpace(a, false, true)
            verify(b.length > 0 && b !== a)
            tryVerify(function() { return deck.entries[b] && deck.entries[b].browser
                && deck.entries[b].browser.title === "Fixture /home" }, 15000)
            compare(String(deck.entries[b].browser.url), FixtureUrl + "/home")
        }
        function test_captureRealEngine() {
            if (!Fixture.evidenceRoot.length) skip("No explicit capture directory")
            var a = open("/a")
            waitForRendering(deck)
            verify(Fixture.captureWindow(deck, "foundation-engine.png"))
        }
        function test_stopAndReload() {
            var a = open("/a"), page = deck.entries[a].browser
            var nav = deck.navigationForSession(a)
            tryCompare(nav, "loading", false, 5000)
            verify(!deck.stop(a))
            verify(deck.navigateInput(a, FixtureUrl + "/slow"))
            tryCompare(nav, "loading", true, 5000)
            verify(deck.stop(a))
            tryCompare(nav, "loading", false, 10000)
            verify(!deck.stop(a))
            verify(deck.navigateInput(a, FixtureUrl + "/a"))
            tryVerify(function() { return String(page.url).endsWith("/a") && page.documentReady && !page.loading }, 10000)
            js(page, "window.sessionMarker=99")
            verify(deck.reload(a))
            tryVerify(function() { return page.documentReady && js(page, "window.sessionMarker") === 0 }, 10000)
        }
        function test_originalPostRequest() {
            var a = open("/a"), page = deck.entries[a].browser
            js(page, "window.sessionMarker=2718")
            mouseClick(page, 80, 280)
            tryVerify(function() { return Sessions.browsers().length === 2 }, 10000)
            var ids = Sessions.browsers().map(function(r) { return r.id })
            var b = ids.filter(function(id) { return id !== a })[0]
            tryVerify(function() { return deck.entries[b] && deck.entries[b].browser.documentReady }, 10000)
            var target = deck.entries[b].browser
            tryVerify(function() { return String(target.url) === FixtureUrl + "/post"
                && target.title === "POST result" && js(target, "window.postBody") !== undefined }, 10000)
            compare(js(target, "window.postBody"), "marker=original-post-body")
            compare(js(target, "window.openerMarker"), 2718)
            compare(target.profile, page.profile)
            compare(Sessions.activeId, b)
        }
        function test_backgroundLinkDoesNotSelect() {
            var a = open("/a"), page = deck.entries[a].browser
            // A real middle-click asks Chromium for a background destination.
            mouseClick(page, 45, 80, Qt.MiddleButton)
            tryVerify(function() { return Sessions.browsers().length === 2 }, 10000)
            compare(Sessions.activeId, a)
            var b = Sessions.browsers().filter(function(r) { return r.id !== a })[0].id
            tryVerify(function() { return deck.entries[b] && deck.entries[b].browser.documentReady }, 10000)
            tryVerify(function() { return String(deck.entries[b].browser.url) === FixtureUrl + "/b"
                && deck.entries[b].browser.title === "Fixture /b" }, 10000)
        }
        function test_nativeNavigationTargetsBackgroundSpace() {
            var a = open("/a"), page = deck.entries[a].browser
            // Toolbar Back follows Chromium's user-interaction skip policy.
            // Navigate with a real user click, not a renderer-script redirect.
            mouseClick(page, 45, 80)
            tryVerify(function() { return String(page.url).endsWith("/b") && page.documentReady && !page.loading }, 10000)
            var b = open("/other")
            var nav = deck.navigationForSession(a)
            verify(nav !== null)
            console.log("USER_NAV", page.canGoBack, nav.canGoBack, nav.backCount)
            tryCompare(nav, "canGoBack", true)
            verify(deck.back(a))
            tryVerify(function() { return String(page.url).endsWith("/a") && page.documentReady && !page.loading }, 10000)
            compare(Sessions.activeId, b)
            compare(String(deck.entries[b].browser.url), FixtureUrl + "/other")
            tryCompare(nav, "canGoForward", true)
            verify(deck.forward(a))
            tryVerify(function() { return String(page.url).endsWith("/b") && page.documentReady && !page.loading }, 10000)
            verify(!deck.back("missing"))
        }
        function test_scriptNavigationKeepsNativeSkipSemantics() {
            var a = open("/a"), page = deck.entries[a].browser
            js(page, "location.href='/b'")
            tryVerify(function() { return page.title === "Fixture /b" && page.documentReady && !page.loading }, 10000)
            var nav = deck.navigationForSession(a)
            tryVerify(function() { return nav.backCount > 0 })
            console.log("SCRIPT_NAV", page.canGoBack, nav.canGoBack, nav.backCount)
            compare(nav.canGoBack, page.canGoBack)
            // An explicit chooser may traverse an entry the toolbar skips.
            verify(deck.historyOffset(a, -1, nav.historyRevision))
            tryVerify(function() { return page.title === "Fixture /a" && page.documentReady && !page.loading }, 10000)
        }
        function test_historyOffsetAndStaleToken() {
            var a = open("/a"), page = deck.entries[a].browser
            js(page, "location.href='/b'")
            tryVerify(function() { return String(page.url).endsWith("/b") && page.documentReady && !page.loading }, 10000)
            js(page, "location.href='/c'")
            tryVerify(function() { return String(page.url).endsWith("/c") && page.documentReady && !page.loading }, 10000)
            var nav = deck.navigationForSession(a)
            tryVerify(function() { return nav.backCount >= 2 })
            var token = nav.historyRevision
            verify(!deck.historyOffset(a, -999, token))
            verify(deck.historyOffset(a, -2, token))
            tryVerify(function() { return String(page.url).endsWith("/a") && page.documentReady && !page.loading }, 10000)
            verify(!deck.historyOffset(a, 1, token), "stale chooser token must be rejected")
            verify(deck.historyOffset(a, 2, nav.historyRevision))
            tryVerify(function() { return String(page.url).endsWith("/c") && page.documentReady && !page.loading }, 10000)
        }
        function test_liveIdentityAndAppProjection() {
            var a = open("/a"), first = deck.entries[a].browser
            js(first, "window.sessionMarker=2718")
            var b = open("/b"), second = deck.entries[b].browser
            compare(Sessions.browserApps().length, 1)
            compare(Sessions.browserSpaces("mangaplus").length, 2)
            Sessions.updateBrowser(a, FixtureUrl + "/renamed", "A different skin label")
            Sessions.switchTo(a)
            compare(deck.entries[a].browser, first)
            compare(js(first, "window.sessionMarker"), 2718)
            compare(deck.entries[b].browser, second)
            compare(Sessions.browserApps()[0].selectedSpaceId, a)
            Sessions.activateBrowserApp("mangaplus")
            compare(Sessions.activeId, "")
            Sessions.activateBrowserApp("mangaplus")
            compare(Sessions.activeId, a)
        }
        function test_explicitNewUsesProviderStart() {
            var a = open("/a")
            var b = deck.newSpace(a, false, true)
            verify(a !== b && b.length > 0)
            tryVerify(function() { return deck.entries[b] && deck.entries[b].browser
                && deck.entries[b].browser.documentReady }, 15000)
            compare(String(deck.entries[b].browser.url), FixtureUrl + "/home")
        }
    }
}
