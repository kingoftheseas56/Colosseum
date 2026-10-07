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
        id: tests
        property var rejectionSource: null
        SignalSpy { id: rejectionSpy; target: tests.rejectionSource; signalName: "closeRejected" }
        name: "FeriaHosting"
        when: windowShown
        property int serial: 0
        function init() {
            deck.startQuiescence(false)
            tryCompare(deck, "quiescing", false, 15000)
            Fixture.switchProfile("hosting-" + (++serial))
            tryCompare(deck, "quiescing", false, 15000)
            fullSize.visible = false
            embedded.width = 820; embedded.height = 580
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
            js(p, "window.sessionMarker=841;history.pushState({},'', '#kept');window.scrollTo(0,500);document.querySelector('audio').play();true")
            var generation = p.navigationGeneration, w = p.width, h = p.height
            verify(typeof deck.maximizeSession === "function", "App Max must move the live page into a separate same-window host")
            for (var i=0; i<3; ++i) {
                verify(deck.maximizeSession(id, fullSize))
                compare(deck.browserForSession(id), p); compare(p.profile, profile)
                compare(p.Window.window, root.Window.window)
                verify(p.width > w); compare(p.navigationGeneration, generation)
                compare(js(p, "document.querySelector('#edit').value"), "kept")
                compare(js(p, "window.sessionMarker"), 841)
                compare(js(p, "Math.round(window.scrollY)"), 500)
                compare(js(p, "document.activeElement.id"), "edit")
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
            host.requestActivate()
            click(p, "#edit"); typeText("native-input")
            compare(js(p, "document.querySelector('#edit').value"), "native-input")
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
            Sessions.switchTo(a)
            js(p, "document.body.insertAdjacentHTML('beforeend','<input id=after style=position:fixed;left:20px;top:20px>')")
            click(p, "#after"); typeText("after-host")
            compare(js(p, "document.querySelector('#after').value"), "after-host")
        }
        function test_nativeCloseRejectRetryAndNeighbor() {
            var a = open("/hosting"), p = deck.browserForSession(a)
            click(p, "#guard")
            verify(typeof deck.moveToDesktop === "function", "Native close must use exact-page veto-capable close")
            verify(deck.moveToDesktop(a))
            var host = deck.entries[a].hostWindow, b = open("/b")
            rejectionSource = p; rejectionSpy.clear()
            host.close()
            tryVerify(function() { return deck.entries[a] && deck.entries[a].closeConsent }, 5000)
            var consent = deck.entries[a].closeConsent
            compare(consent.identity.spaceId, a)
            host.close()
            compare(deck.entries[a].closeConsent.attempt, consent.attempt, "Duplicate close coalesces")
            verify(!deck.returnToEmbedded(a), "Pending close fences transfer")
            verify(deck.answerClose(a, consent.attempt, false))
            tryVerify(function() { return !deck.entries[a].closePending }, 5000)
            compare(rejectionSpy.count, 1, "Pinned Qt rejection acknowledgement must originate from this view")
            for (var retry = 0; retry < 2; ++retry) {
                host.close()
                tryVerify(function() { return deck.entries[a].closeConsent }, 5000)
                verify(deck.answerClose(a, deck.entries[a].closeConsent.attempt, false))
                tryVerify(function() { return !deck.entries[a].closePending }, 5000)
            }
            compare(rejectionSpy.count, 3)
            verify(host.visible); verify(Sessions.get(a).id === a); verify(Sessions.get(b).id === b)
            compare(Sessions.closedBrowserRecords("mangaplus").length, 0)
            host.close()
            tryVerify(function() { return deck.entries[a].closeConsent }, 5000)
            verify(!deck.answerClose(a, consent.attempt, true), "Old rejected attempt cannot approve retry")
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
        function test_navigationInvalidatesConsentAndLateCloseAcknowledgement() {
            var a = open("/hosting"), p = deck.browserForSession(a)
            click(p, "#guard")
            verify(deck.closeTab(a))
            tryVerify(function() { return deck.entries[a].closeConsent }, 5000)
            var old = deck.entries[a].closeConsent
            verify(deck.navigateInput(a, FixtureUrl + "/b"))
            verify(!deck.answerClose(a, old.attempt, true))
            tryVerify(function() { return p.documentReady && p.title === "Fixture /b"
                || deck.entries[a].closeConsent !== null }, 10000)
            if (deck.entries[a].closeConsent) verify(deck.answerClose(a, deck.entries[a].closeConsent.attempt, true))
            tryCompare(p, "title", "Fixture /b", 10000)
            // Fault injection of a late public callback after a REAL navigation.
            // The callback itself carries no Qt attempt token; it must be fenced.
            p.windowCloseRequested()
            wait(100)
            compare(deck.browserForSession(a), p)
            compare(Sessions.closedBrowserRecords("mangaplus").length, 0)
        }
        function test_siteCloseUsesOriginalDocumentConsent() {
            var a = open("/hosting"), p = deck.browserForSession(a)
            js(p, "document.querySelector('#dialog').onclick=function(e){e.preventDefault();window.open('/hosting','site-close','width=520,height=420')}")
            click(p, "#dialog")
            tryVerify(function() { return Sessions.browsers().length === 2 }, 10000)
            var b = Sessions.browsers().filter(function(r) { return r.id !== a })[0].id
            tryVerify(function() { return deck.browserForSession(b) && deck.browserForSession(b).documentReady }, 10000)
            var child = deck.browserForSession(b)
            click(child, "#guard")
            js(child, "document.querySelector('#dialog').onclick=function(e){e.preventDefault();window.close()}")
            click(child, "#dialog")
            tryVerify(function() { return deck.entries[b].closeConsent }, 5000)
            Sessions.switchTo(a)
            verify(deck.answerClose(b, deck.entries[b].closeConsent.attempt, false))
            tryVerify(function() { return !deck.entries[b].closePending }, 5000)
            compare(deck.browserForSession(a), p)
            verify(deck.closeTab(b))
            tryVerify(function() { return deck.entries[b].closeConsent }, 5000)
            verify(deck.answerClose(b, deck.entries[b].closeConsent.attempt, true))
            tryVerify(function() { return !deck.entries[b] }, 10000)
            compare(deck.browserForSession(a), p)
        }
        function test_invalidatedAttemptCannotAuthorizeNewClose() {
            var a = open("/hosting"), p = deck.browserForSession(a)
            click(p, "#guard")
            verify(deck.closeTab(a))
            // Invalidate while RequestClose is still in flight, before a dialog
            // or rejection acknowledgment reaches the adapter.
            verify(deck.navigateInput(a, FixtureUrl + "/b"))
            var nextWasAdmitted = deck.closeTab(a)
            p.windowCloseRequested() // Inject delayed A acknowledgement, not B consent.
            wait(50)
            verify(deck.browserForSession(a) === p,
                "Old acknowledgement must not settle fresh attempt; admitted=" + nextWasAdmitted)
            compare(Sessions.closedBrowserRecords("mangaplus").length, 0)
        }
        function childMatching(item, predicate) {
            if (predicate(item)) return item
            var children = item.children || []
            for (var i=0; i<children.length; ++i) {
                var found = childMatching(children[i], predicate)
                if (found) return found
            }
            return null
        }
        function test_hiddenEmbeddedConsentVisibleInSelectedNativeChrome() {
            var a = open("/hosting"), p = deck.browserForSession(a)
            click(p, "#guard")
            var b = open("/b")
            verify(deck.moveToDesktop(b))
            var host = deck.entries[b].hostWindow
            host.requestActivate()
            verify(deck.closeTab(a))
            tryVerify(function() { return deck.entries[a].closeConsent }, 5000)
            compare(Sessions.activeId, b)
            var panel = childMatching(deck.entries[b], function(item) {
                return String(item).indexOf("FeriaCloseConsent") >= 0
            })
            verify(panel !== null)
            verify(panel.visible && panel.height > 0, "Hidden origin consent must be exposed in visible native chrome")
            compare(panel.consent.identity.spaceId, a)
            var stay = childMatching(panel, function(item) { return item.text === "Stay" })
            verify(stay !== null)
            mouseClick(stay, stay.width/2, stay.height/2)
            tryVerify(function() { return !deck.entries[a].closePending }, 5000)
            compare(deck.browserForSession(a), p)
            verify(deck.browserForSession(b))
        }
        function test_hiddenEmbeddedCloseHasSingleDialogOwner() {
            var a = open("/hosting"), p = deck.browserForSession(a)
            click(p, "#guard")
            var b = open("/b")
            verify(!p.visible)
            verify(deck.closeTab(a))
            tryVerify(function() { return deck.entries[a].closeConsent }, 5000)
            verify(deck.answerClose(a, deck.entries[a].closeConsent.attempt, false))
            tryVerify(function() { return !deck.entries[a].closePending }, 6000)
            compare(deck.entries[a].closeController.failure, "", "Inactive generic request handler must not pre-reject managed unload")
            compare(Sessions.get(a).browserCloseAttempt, "")
            compare(deck.browserForSession(a), p)
            verify(deck.browserForSession(b))
        }
        function test_originalRequestCannotBeAdoptedTwice() {
            var a = open("/hosting"), p = deck.browserForSession(a)
            js(p, "window.sessionMarker=291;document.querySelector('#dialog').onclick=function(e){e.preventDefault();window.open('/hosting','one-shot','width=520,height=420')}")
            var replayed = false
            function duplicate(request) { replayed = true; deck.openWindow(a, request) }
            p.newWindowRequested.connect(duplicate)
            click(p, "#dialog")
            p.newWindowRequested.disconnect(duplicate)
            verify(replayed, "Replay comes from the same actual Qt request dispatch")
            compare(Sessions.browsers().length, 2, "Repeated void-openIn request must not leave an orphan entry/host")
            var b = Sessions.browsers().filter(function(r) { return r.id !== a })[0].id
            tryVerify(function() { return deck.browserForSession(b) && deck.browserForSession(b).documentReady }, 10000)
            compare(js(deck.browserForSession(b), "window.opener.sessionMarker"), 291)
            compare(deck.entries[a].liveAdoptedChildren, 1)
            deck.closeTab(b)
            tryVerify(function() { return !deck.entries[b] && deck.entries[a].liveAdoptedChildren === 0 }, 10000)
        }
        function test_restoreReinstatesEmbeddedLayoutBinding() {
            var a = open("/hosting"), p = deck.browserForSession(a)
            verify(deck.maximizeSession(a, fullSize))
            verify(deck.restoreSession(a))
            embedded.width = 710; embedded.height = 470
            tryCompare(p, "width", embedded.width)
            tryCompare(p, "height", embedded.height)
        }
        function test_selectNeighborRestoresMaxWithoutReentrancy() {
            var a = open("/hosting"), p = deck.browserForSession(a)
            var b = open("/b")
            Sessions.switchTo(a)
            verify(deck.maximizeSession(a, fullSize))
            Sessions.switchTo(b)
            tryCompare(fullSize, "visible", false)
            compare(deck.entries[a].fullSizeHost, null)
            compare(deck.entries[a].savedPlacement, null)
            compare(p.Window.window, root.Window.window)
            Sessions.switchTo(a)
            compare(deck.browserForSession(a), p)
            compare(p.width, embedded.width)
        }
        function test_staleHostCommandsCannotTargetMovedPage() {
            var a = open("/hosting"), p = deck.browserForSession(a)
            verify(deck.moveToDesktop(a))
            var lease = Sessions.browserIdentity(a)
            verify(typeof deck.hostCommand === "function", "New host chrome must dispatch through a scoped host lease")
            verify(deck.hostCommand(lease, "navigate", FixtureUrl + "/a"))
            tryCompare(p, "title", "Fixture /a", 10000)
            // Host lease remains valid across documents, but dispatch obtains
            // fresh document identity rather than replaying its old generation.
            verify(deck.hostCommand(lease, "navigate", FixtureUrl + "/hosting"))
            tryCompare(p, "title", "Fixture /hosting", 10000)
            verify(deck.returnToEmbedded(a))
            verify(!deck.hostCommand(lease, "navigate", FixtureUrl + "/b"))
            verify(!deck.hostCommand(lease, "close", ""))
            verify(!deck.hostCommand(lease, "restore", ""))
            wait(100)
            compare(deck.browserForSession(a), p)
            compare(String(p.url), FixtureUrl + "/hosting")
        }
        function test_failedReopenRetainsRecordAndPrivateNeverRecords() {
            verify(typeof deck.reopenClosedSpace === "function", "Reopen requires failure-retaining runtime commit")
            var id = Sessions.openBrowser({providerId:"mangaplus", providerName:"Fixture", mode:"read",
                url:FixtureUrl + "/broken", startUrl:FixtureUrl + "/home"}, true)
            tryVerify(function() { return deck.entries[id] && deck.entries[id].errorText.length > 0 }, 15000)
            deck.closeTab(id)
            tryVerify(function() { return !deck.entries[id] }, 10000)
            compare(Sessions.closedBrowserRecords("mangaplus").length, 1)
            var record = Sessions.prepareBrowserReopen("mangaplus").recordId
            var failed = deck.reopenClosedSpace("mangaplus")
            verify(failed && failed !== id)
            tryVerify(function() { return !deck.entries[failed] }, 15000)
            compare(Sessions.prepareBrowserReopen("mangaplus").recordId, record)
            var privateId = open("/hosting", "ytmusic", true)
            deck.closeTab(privateId)
            tryVerify(function() { return !deck.entries[privateId] }, 10000)
            compare(Sessions.closedBrowserRecords("ytmusic").length, 0)
        }
        function test_reopenRedirectCommitsOnlyAfterLoadedDestination() {
            var a = open("/reopen-redirect?case=" + serial)
            deck.closeTab(a)
            tryVerify(function() { return !deck.entries[a] }, 10000)
            compare(Sessions.closedBrowserRecords("mangaplus").length, 1)
            var fresh = deck.reopenClosedSpace("mangaplus")
            verify(fresh && fresh !== a)
            tryVerify(function() { var page = deck.browserForSession(fresh)
                return page && page.documentReady && page.title === "Fixture /hosting" }, 15000)
            compare(Sessions.closedBrowserRecords("mangaplus").length, 0)
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
