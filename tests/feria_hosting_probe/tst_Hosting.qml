import QtQuick
import QtQuick.Window
import QtTest
import QtWebEngine
import "../../qml/feria" as Feria

Item {
    id: root
    width: 960; height: 640
    Item { id: embedded; x: 15; y: 25; width: 800; height: 500 }
    Item { id: fullSize; anchors.fill: parent; visible: false }
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
        name: "FeriaHosting"
        when: windowShown
        property int serial: 0
        function init() {
            deck.displayed = true
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
            tryVerify(function() { var page = deck.browserForSession(id)
                return page && page.documentReady && page.title === "Fixture " + path }, 15000)
            waitForRendering(deck.browserForSession(id))
            return id
        }
        function js(page, source) {
            var done = false, result
            page.evaluateJavaScript(source, function(value) { result = value; done = true })
            tryVerify(function() { return done }, 5000)
            return result
        }
        function clickElement(page, selector) {
            var point = js(page, "(()=>{let r=document.querySelector(" + JSON.stringify(selector)
                + ").getBoundingClientRect();return {x:r.x+r.width/2,y:r.y+r.height/2}})()")
            verify(point && point.x > 0 && point.y > 0)
            page.forceActiveFocus()
            waitForRendering(page)
            console.log("HOST_CLICK", selector, String(page.url), page.visible, point.x, point.y)
            mouseClick(page, point.x, point.y)
            console.log("HOST_CLICK_DONE", selector)
        }
        function test_sameLivePageMovesWithoutReload() {
            var id = open("/a"), page = deck.browserForSession(id)
            var profile = page.profile
            js(page, "window.sessionMarker=812;document.body.insertAdjacentHTML('beforeend','<input id=kept>');document.querySelector('#kept').value='unsaved';history.pushState({},'', '#kept')")
            var width = page.width, height = page.height
            verify(typeof deck.moveToDesktop === "function", "Same-page host transfer is required")
            verify(deck.moveToDesktop(id))
            var host = deck.entries[id].hostWindow
            verify(host && host.visible)
            compare(deck.browserForSession(id), page)
            compare(js(page, "window.sessionMarker"), 812)
            compare(js(page, "document.querySelector('#kept').value"), "unsaved")
            compare(page.profile, profile)
            verify(deck.returnToEmbedded(id))
            tryCompare(page, "width", width)
            tryCompare(page, "height", height)
            compare(deck.browserForSession(id), page)
            compare(js(page, "window.sessionMarker"), 812)
            verify(String(page.url).endsWith("#kept"))
            fullSize.visible = true
            verify(deck.presentFullSize(id, fullSize))
            tryCompare(page, "width", fullSize.width)
            compare(js(page, "window.sessionMarker"), 812)
            verify(deck.returnToEmbedded(id))
            tryCompare(page, "width", width)
            tryCompare(page, "height", height)
        }
        function test_realWindowHasIndependentCanonicalOwner() {
            var a = open("/a"), page = deck.browserForSession(a), seen = false
            function context(r) { r.accepted = true; seen = true }
            page.contextMenuRequested.connect(context)
            mouseClick(page, 45, 80, Qt.RightButton)
            tryVerify(function() { return seen })
            page.triggerWebAction(WebEngineView.OpenLinkInNewWindow)
            tryVerify(function() { return Sessions.browsers().length === 2 }, 10000)
            var b = Sessions.browsers().filter(function(r) { return r.id !== a })[0].id
            tryVerify(function() { return deck.entries[b] && deck.entries[b].hostWindow
                && deck.browserForSession(b).title === "Fixture /b" }, 10000)
            var destination = deck.browserForSession(b)
            js(destination, "window.sessionMarker=94")
            Sessions.close(a)
            tryVerify(function() { return !deck.entries[a] })
            compare(js(destination, "window.sessionMarker"), 94)
            deck.entries[b].hostWindow.close()
            tryVerify(function() { return !deck.entries[b] }, 10000)
        }
        function test_dialogAndNestedWindowKeepOriginProfile() {
            var a = open("/a", "mangaplus", true), page = deck.browserForSession(a)
            js(page, "window.sessionMarker=483;document.querySelector('button').onclick=()=>window.open('/post','dialog','width=420,height=300')")
            clickElement(page, "button")
            tryVerify(function() { return Sessions.browsers().length === 2 }, 10000)
            var b = Sessions.browsers().filter(function(r) { return r.id !== a })[0].id
            tryVerify(function() { return deck.entries[b] && deck.entries[b].hostWindow
                && deck.browserForSession(b).title === "POST result" }, 10000)
            var dialog = deck.browserForSession(b)
            console.log("DIALOG_ADOPTED", b, dialog.visible, dialog.title)
            compare(dialog.profile, page.profile)
            verify(dialog.profile.offTheRecord)
            compare(js(dialog, "window.openerMarker"), 483)
            Sessions.close(a)
            tryVerify(function() { return !deck.entries[a] })
            console.log("DIALOG_OPENER_RETIRED")
            compare(js(dialog, "window.openerMarker"), 483)
            // A nested request belongs to this exact dialog/profile, not activeId.
            js(dialog, "document.body.innerHTML='<button id=nested style=\"width:180px;height:40px\">Nested</button>';document.querySelector('#nested').onclick=()=>window.open('/nested','_blank')")
            clickElement(dialog, "#nested")
            tryVerify(function() { return Sessions.browsers().length === 2 }, 10000)
            var c = Sessions.browsers().filter(function(r) { return r.id !== b })[0].id
            tryVerify(function() { return deck.browserForSession(c) && deck.browserForSession(c).title === "Fixture /nested" }, 10000)
            compare(deck.browserForSession(c).profile, dialog.profile)
            verify(Sessions.get(c).target.privateBrowsing)
        }
        function test_beforeUnloadRejectThenAcceptExactSpace() {
            var a = open("/a"), page = deck.browserForSession(a)
            clickElement(page, "h1") // real sticky user activation for beforeunload
            js(page, "window.onbeforeunload=()=> 'unsaved'")
            var b = open("/b")
            verify(typeof deck.requestClose === "function", "Close transaction is required")
            verify(deck.requestClose(a))
            tryVerify(function() { return deck.closeConsent(a) !== null }, 10000)
            var ticket = deck.closeConsent(a)
            compare(ticket.sessionId, a)
            verify(!deck.requestClose(a), "Duplicate close must not issue a second engine action")
            verify(deck.answerClose(a, ticket.id, false))
            tryCompare(deck.entries[a].closeController, "pending", false)
            verify(deck.entries[a] && deck.entries[b])
            compare(Sessions.activeId, b)
            verify(!deck.answerClose(a, ticket.id, true), "Stale answer cannot close the page")
            verify(deck.requestClose(a))
            tryVerify(function() { return deck.closeConsent(a) !== null }, 10000)
            verify(deck.answerClose(a, deck.closeConsent(a).id, true))
            tryVerify(function() { return !deck.entries[a] }, 10000)
            verify(deck.entries[b])
            compare(Sessions.activeId, b)
        }
        function test_nativeCloseCanBeCancelled() {
            var id = open("/a"), page = deck.browserForSession(id)
            verify(typeof deck.moveToDesktop === "function", "Desktop close host is required")
            verify(deck.moveToDesktop(id))
            clickElement(page, "h1")
            js(page, "window.onbeforeunload=()=> 'unsaved'")
            var host = deck.entries[id].hostWindow
            host.close()
            tryVerify(function() { return deck.closeConsent(id) !== null }, 10000)
            verify(host.visible)
            verify(page.x > 0 && page.width < host.width, "Consent reserves space outside the website")
            verify(deck.answerClose(id, deck.closeConsent(id).id, false))
            tryCompare(deck.entries[id].closeController, "pending", false)
            verify(host.visible && deck.entries[id])
            host.close()
            tryVerify(function() { return deck.closeConsent(id) !== null }, 10000)
            verify(deck.answerClose(id, deck.closeConsent(id).id, true))
            tryVerify(function() { return !deck.entries[id] }, 10000)
        }
        function test_reopenIsAppScopedFreshAndPrivateExcluded() {
            var a = open("/a", "mangaplus"), b = open("/b", "youtube")
            var c = open("/c", "mangaplus", true)
            js(deck.browserForSession(a), "window.sessionMarker=555")
            verify(typeof deck.requestClose === "function", "Acknowledged close is required")
            verify(deck.requestClose(a)); tryVerify(function() { return !deck.entries[a] })
            verify(deck.requestClose(b)); tryVerify(function() { return !deck.entries[b] })
            verify(deck.requestClose(c)); tryVerify(function() { return !deck.entries[c] })
            compare(deck.closedTabs.length, 2)
            var reopened = deck.reopenClosedSpace("mangaplus")
            verify(reopened.length > 0 && reopened !== a && reopened !== b && reopened !== c)
            tryVerify(function() { return deck.browserForSession(reopened) && deck.browserForSession(reopened).title === "Fixture /a" }, 10000)
            compare(js(deck.browserForSession(reopened), "window.sessionMarker"), 0)
            compare(deck.closedTabs.length, 1)
            compare(deck.closedTabs[0].providerId, "youtube")
            compare(deck.reopenClosedSpace("netflix"), "")
            compare(deck.closedTabs.length, 1)
        }
        function test_requestedGeometryIsWorkAreaBounded() {
            var id = open("/a")
            verify(typeof deck.moveToDesktop === "function", "Geometry-aware host is required")
            verify(deck.moveToDesktop(id))
            var host = deck.entries[id].hostWindow
            verify(deck.applyRequestedGeometry(id, Qt.rect(-100000,-100000,100000,100000)))
            var area = FeriaBrowserPolicy.windowWorkArea(host)
            verify(host.x >= area.x && host.y >= area.y)
            verify(host.x + host.width <= area.x + area.width)
            verify(host.y + host.height <= area.y + area.height)
        }
        function test_profileRetirementClosesAllHosts() {
            var a = open("/a"), b = open("/b", "mangaplus", true)
            verify(typeof deck.moveToDesktop === "function", "Retained host ownership is required")
            verify(deck.moveToDesktop(a))
            verify(deck.moveToDesktop(b))
            Fixture.switchProfile("retired-" + serial)
            tryCompare(deck, "quiescing", false, 15000)
            compare(Sessions.browsers().length, 0)
            compare(FeriaBrowserPolicy.runtimeViewCount, 0)
            compare(FeriaBrowserPolicy.runtimeProfileCount, 0)
            compare(deck.closedTabs.length, 0)
        }
    }
}
