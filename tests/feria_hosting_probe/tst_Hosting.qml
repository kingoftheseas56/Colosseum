import QtQuick
import QtQuick.Window
import QtTest
import QtWebEngine
import "../../qml/feria" as Feria

Item {
    id: root
    width: 1060; height: 760
    Item { id: embedded; x: 25; y: 75; width: 820; height: 580 }
    Item { id: fullSize; anchors.fill: parent; visible: false }
    Feria.FeriaSessionDeck {
        id: deck
        parent: embedded
        anchors.fill: parent
        sessions: Sessions; accountStore: FeriaAccount; policy: FeriaBrowserPolicy
        displayed: true
    }
    TestCase {
        name: "FeriaHosting"
        when: windowShown
        property int serial: 0
        function init() {
            deck.startQuiescence(false)
            tryCompare(deck, "quiescing", false, 15000)
            Fixture.switchProfile("hosting-" + (++serial))
            tryCompare(deck, "quiescing", false, 15000)
            fullSize.visible = false
        }
        function cleanup() {
            deck.startQuiescence(false)
            tryCompare(deck, "quiescing", false, 15000)
        }
        function open(path, provider, privateMode) {
            var id = Sessions.openBrowser({providerId: provider || "mangaplus", providerName: "Fixture",
                url: FixtureUrl + path, startUrl: FixtureUrl + "/home", mode: "read",
                privateBrowsing: !!privateMode}, true)
            verify(id.length > 0)
            tryVerify(function() { var p = deck.browserForSession(id); return p && p.documentReady }, 15000)
            return id
        }
        function js(page, source) {
            var done = false, result
            verify(page.evaluateJavaScript(source, function(value) { result = value; done = true }))
            tryVerify(function() { return done }, 5000)
            return result
        }
        function click(page, selector) {
            var point = js(page, "(()=>{let r=document.querySelector(" + JSON.stringify(selector)
                + ").getBoundingClientRect();return {x:r.x+r.width/2,y:r.y+r.height/2}})()")
            page.forceActiveFocus(); waitForRendering(page)
            mouseClick(page, point.x, point.y)
        }
        function typeText(text) {
            for (var i=0; i<text.length; ++i) keyClick(text.toUpperCase().charCodeAt(i), Qt.NoModifier)
        }
        function test_appMaxPreservesSameDocumentAndInput() {
            var id = open("/hosting"), p = deck.browserForSession(id), profile = p.profile
            click(p, "#edit"); typeText("kept")
            js(p, "window.sessionMarker=841;history.pushState({},'', '#kept');document.querySelector('audio').play()")
            var generation = p.navigationGeneration, w = p.width, h = p.height
            verify(typeof deck.maximizeSession === "function", "App Max must move the live page into a separate same-window host")
            for (var i=0; i<3; ++i) {
                verify(deck.maximizeSession(id, fullSize))
                compare(deck.browserForSession(id), p); compare(p.profile, profile)
                compare(p.Window.window, root.Window.window)
                verify(p.width > w); compare(p.navigationGeneration, generation)
                compare(js(p, "document.querySelector('#edit').value"), "kept")
                compare(js(p, "window.sessionMarker"), 841)
                compare(js(p, "document.querySelector('audio').paused"), false)
                verify(!p.fullScreenActive)
                verify(deck.restoreSession(id))
                tryCompare(p, "width", w); tryCompare(p, "height", h)
            }
            click(p, "#edit"); typeText("-input")
            compare(js(p, "document.querySelector('#edit').value"), "kept-input")
        }
        function test_nativeHostsAreExactAndDisposable() {
            var a = open("/hosting"), p = deck.browserForSession(a)
            var b = open("/b")
            verify(typeof deck.moveToDesktop === "function", "Independent canonical desktop hosts required")
            verify(deck.moveToDesktop(a))
            var host = deck.entries[a].hostWindow
            verify(host && host.visible)
            compare(p.Window.window, host)
            Sessions.switchTo(b)
            verify(deck.navigateInput(a, FixtureUrl + "/a"))
            tryCompare(p, "title", "Fixture /a", 10000)
            compare(String(deck.browserForSession(b).url), FixtureUrl + "/b")
            js(p, "window.sessionMarker=717")
            verify(deck.returnToEmbedded(a))
            wait(200)
            compare(deck.browserForSession(a), p)
            compare(js(p, "window.sessionMarker"), 717)
            compare(p.Window.window, root.Window.window)
        }
        function test_nativeCloseRejectRetryAndNeighbor() {
            var a = open("/hosting"), p = deck.browserForSession(a)
            click(p, "#guard")
            verify(typeof deck.moveToDesktop === "function", "Native close must use exact-page veto-capable close")
            verify(deck.moveToDesktop(a))
            var host = deck.entries[a].hostWindow, b = open("/b")
            host.close()
            tryVerify(function() { return deck.entries[a] && deck.entries[a].closeConsent }, 5000)
            var consent = deck.entries[a].closeConsent
            compare(consent.identity.spaceId, a)
            verify(deck.answerClose(a, consent.attempt, false))
            tryVerify(function() { return !deck.entries[a].closePending }, 5000)
            verify(host.visible); verify(Sessions.get(a).id === a); verify(Sessions.get(b).id === b)
            compare(Sessions.closedBrowserRecords("mangaplus").length, 0)
            host.close()
            tryVerify(function() { return deck.entries[a].closeConsent }, 5000)
            verify(deck.answerClose(a, deck.entries[a].closeConsent.attempt, true))
            tryVerify(function() { return !deck.entries[a] }, 10000)
            verify(deck.browserForSession(b)); compare(Sessions.closedBrowserRecords("mangaplus").length, 1)
        }
        function test_siteDialogCanonicalGeometryAndOriginalPost() {
            var a = open("/hosting"), p = deck.browserForSession(a)
            js(p, "window.sessionMarker=192")
            click(p, "#dialog")
            tryVerify(function() { return Sessions.browsers().length === 2 }, 10000)
            var b = Sessions.browsers().filter(function(r) { return r.id !== a })[0].id
            tryVerify(function() { return deck.browserForSession(b) && deck.browserForSession(b).documentReady }, 10000)
            var e = deck.entries[b]
            verify(e.hostWindow && e.hostWindow.visible, "Site dialog requires independent canonical native host")
            compare(e.browser.profile, p.profile)
            verify(e.hostWindow.width >= 320 && e.hostWindow.height >= 240)
            compare(js(e.browser, "window.openerMarker"), 192)
            compare(js(e.browser, "window.postBody"), "marker=original-post-body")
            deck.closeTab(b)
            tryVerify(function() { return !deck.entries[b] }, 10000)
        }
        function test_reopenAppScopedFreshIdentity() {
            var a = open("/hosting", "mangaplus"), b = open("/b", "ytmusic")
            verify(typeof deck.reopenClosedSpace === "function", "Reopen must be app-scoped and two-phase")
            deck.closeTab(a); tryVerify(function() { return !deck.entries[a] }, 10000)
            deck.closeTab(b); tryVerify(function() { return !deck.entries[b] }, 10000)
            var fresh = deck.reopenClosedSpace("mangaplus")
            verify(fresh && fresh !== a && fresh !== b)
            tryVerify(function() { return deck.browserForSession(fresh) && deck.browserForSession(fresh).documentReady }, 15000)
            compare(Sessions.get(fresh).target.providerId, "mangaplus")
            compare(Sessions.closedBrowserRecords("mangaplus").length, 0)
            compare(Sessions.closedBrowserRecords("ytmusic").length, 1)
        }
    }
}
