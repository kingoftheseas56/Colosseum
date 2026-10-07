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
                && deck.entries[id].browser.documentReady }, 15000)
            return id
        }
        function js(page, script) {
            var done = false, value
            page.evaluateJavaScript(script, function(result) { value = result; done = true })
            tryVerify(function() { return done }, 5000)
            return value
        }
        function test_stopAndReload() {
            var a = open("/a"), page = deck.entries[a].browser
            var nav = deck.navigationForSession(a)
            verify(!deck.stop(a))
            verify(deck.navigateInput(a, FixtureUrl + "/slow"))
            tryCompare(nav, "loading", true, 5000)
            verify(deck.stop(a))
            tryCompare(nav, "loading", false, 10000)
            verify(!deck.stop(a))
            verify(deck.navigateInput(a, FixtureUrl + "/a"))
            tryVerify(function() { return String(page.url).endsWith("/a") && page.documentReady }, 10000)
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
            compare(String(deck.entries[b].browser.url), FixtureUrl + "/b")
        }
        function test_nativeNavigationTargetsBackgroundSpace() {
            var a = open("/a"), page = deck.entries[a].browser
            js(page, "location.href='/b'")
            tryVerify(function() { return String(page.url).endsWith("/b") && page.documentReady }, 10000)
            var b = open("/other")
            var nav = deck.navigationForSession(a)
            verify(nav !== null)
            tryCompare(nav, "canGoBack", true)
            verify(deck.back(a))
            tryVerify(function() { return String(page.url).endsWith("/a") && page.documentReady }, 10000)
            compare(Sessions.activeId, b)
            compare(String(deck.entries[b].browser.url), FixtureUrl + "/other")
            tryCompare(nav, "canGoForward", true)
            verify(deck.forward(a))
            tryVerify(function() { return String(page.url).endsWith("/b") && page.documentReady }, 10000)
            verify(!deck.back("missing"))
        }
        function test_historyOffsetAndStaleToken() {
            var a = open("/a"), page = deck.entries[a].browser
            js(page, "location.href='/b'")
            tryVerify(function() { return String(page.url).endsWith("/b") && page.documentReady }, 10000)
            js(page, "location.href='/c'")
            tryVerify(function() { return String(page.url).endsWith("/c") && page.documentReady }, 10000)
            var nav = deck.navigationForSession(a)
            tryVerify(function() { return nav.backCount >= 2 })
            var token = nav.historyRevision
            verify(!deck.historyOffset(a, -999, token))
            verify(deck.historyOffset(a, -2, token))
            tryVerify(function() { return String(page.url).endsWith("/a") && page.documentReady }, 10000)
            verify(!deck.historyOffset(a, 1, token), "stale chooser token must be rejected")
            verify(deck.historyOffset(a, 2, nav.historyRevision))
            tryVerify(function() { return String(page.url).endsWith("/c") && page.documentReady }, 10000)
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
