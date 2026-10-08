import QtQuick
import QtQuick.Window
import QtTest
import QtWebEngine
import "../../qml/feria" as Feria

Item {
    id: root
    width: 1060; height: 760
    Item { id: globalChrome; x: 0; y: 0; width: parent.width; height: 110 }
    Item { id: embedded; x: 25; y: 125; width: 820; height: 580 }
    Item { id: fullSize; anchors.top: globalChrome.bottom; anchors.left: parent.left
        anchors.right: parent.right; anchors.bottom: parent.bottom; visible: false }
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
            failOnWarning(/TypeError|ReferenceError|incompatible arguments|Could not convert argument|Cannot anchor/)
            deck.startQuiescence(false)
            tryCompare(deck, "quiescing", false, 15000)
            Fixture.switchProfile("hosting-" + (++serial))
            tryCompare(deck, "quiescing", false, 15000)
            fullSize.visible = false
            deck.displayed = true; deck.suppressed = false
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
            tryVerify(function() { var p = deck.browserForSession(id)
                return p && p.documentReady && String(p.url) === FixtureUrl + path
                    && p.title === "Fixture " + path }, 15000)
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
            verify(point && point.x > 0 && point.y > 0, "Expected DOM target must exist: " + selector)
            tryVerify(function() { return page.visible && page.width > point.x && page.height > point.y }, 5000)
            if (page.Window.window) {
                page.Window.window.requestActivate()
                tryVerify(function() { return Fixture.focusWindowMatches(page.Window.window) }, 5000)
            }
            page.forceActiveFocus(); waitForRendering(page, 1000)
            mouseClick(page, point.x, point.y)
            if (selector === "#guard")
                tryVerify(function() { return js(page, "typeof window.onbeforeunload") === "function" }, 5000,
                    "Real guard click must install the document's beforeunload handler")
        }
        function typeText(text) {
            for (var i=0; i<text.length; ++i) keyClick(text.toUpperCase().charCodeAt(i), Qt.NoModifier)
        }
        function test_appMaxPreservesSameDocumentAndInput() {
            var id = open("/hosting"), p = deck.browserForSession(id), profile = p.profile
            click(p, "#edit"); typeText("kept")
            js(p, "window.sessionMarker=841;history.pushState({retained:841},'', '#kept');window.scrollTo(0,500);document.querySelector('audio').play();true")
            var generation = p.navigationGeneration, w = p.width, h = p.height
            var ex = deck.entries[id].x, ey = deck.entries[id].y
            var historyLength = js(p, "history.length")
            verify(typeof deck.maximizeSession === "function", "App Max must move the live page into a separate same-window host")
            for (var i=0; i<3; ++i) {
                verify(deck.maximizeSession(id, fullSize))
                compare(deck.browserForSession(id), p); compare(p.profile, profile)
                compare(p.Window.window, root.Window.window)
                verify(p.width > w); compare(p.navigationGeneration, generation)
                compare(js(p, "document.querySelector('#edit').value"), "kept")
                compare(js(p, "window.sessionMarker"), 841)
                compare(js(p, "history.length"), historyLength)
                compare(js(p, "history.state.retained"), 841)
                compare(js(p, "location.hash"), "#kept")
                compare(js(p, "Math.round(window.scrollY)"), 500)
                compare(js(p, "document.activeElement.id"), "edit")
                compare(js(p, "document.querySelector('audio').paused"), false)
                verify(!p.fullScreenActive)
                verify(deck.restoreSession(id))
                tryCompare(p, "width", w); tryCompare(p, "height", h)
                compare(deck.entries[id].x, ex); compare(deck.entries[id].y, ey)
            }
            js(p, "history.back()")
            tryVerify(function() { return js(p, "location.hash") === "" }, 5000)
            js(p, "history.forward()")
            tryVerify(function() { return js(p, "history.state && history.state.retained") === 841 }, 5000)
            click(p, "#edit"); typeText("-input")
            compare(js(p, "document.querySelector('#edit').value"), "kept-input")
        }
        function test_nativeHostsAreExactAndDisposable() {
            var a = open("/hosting"), p = deck.browserForSession(a)
            var originWindow = p.Window.window
            var b = open("/b")
            verify(typeof deck.moveToDesktop === "function", "Independent canonical desktop hosts required")
            verify(deck.moveToDesktop(a))
            var host = deck.entries[a].hostWindow
            verify(host && host.visible)
            compare(p.Window.window, host)
            verify(Fixture.sameScreen(originWindow, host), "Native fixture compares real QScreen identity, not QML wrapper types")
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
        function selectStableNeighbor(id) {
            Sessions.switchTo(id)
            root.Window.window.requestActivate()
            tryCompare(Sessions, "activeId", id)
            tryVerify(function() { return Fixture.focusWindowMatches(root.Window.window) }, 5000)
            wait(100)
            compare(Sessions.activeId, id)
        }
        function test_nativeCloseRejectRetryAndNeighbor() {
            var a = open("/hosting"), p = deck.browserForSession(a)
            click(p, "#guard")
            verify(typeof deck.moveToDesktop === "function", "Native close must use exact-page veto-capable close")
            verify(deck.moveToDesktop(a))
            var host = deck.entries[a].hostWindow, b = open("/b")
            rejectionSource = p; rejectionSpy.clear()
            selectStableNeighbor(b)
            host.close()
            tryVerify(function() { return deck.entries[a] && deck.entries[a].closeConsent }, 5000)
            var consent = deck.entries[a].closeConsent
            compare(consent.identity.spaceId, a)
            selectStableNeighbor(b)
            host.close()
            compare(deck.entries[a].closeConsent.attempt, consent.attempt, "Duplicate close coalesces")
            verify(!deck.returnToEmbedded(a), "Pending close fences transfer")
            verify(deck.answerClose(a, consent.attempt, false))
            tryVerify(function() { return !deck.entries[a].closePending }, 5000)
            compare(rejectionSpy.count, 1, "Pinned Qt rejection acknowledgement must originate from this view")
            for (var retry = 0; retry < 2; ++retry) {
                selectStableNeighbor(b)
                host.close()
                tryVerify(function() { return deck.entries[a].closeConsent }, 5000)
                verify(deck.answerClose(a, deck.entries[a].closeConsent.attempt, false))
                tryVerify(function() { return !deck.entries[a].closePending }, 5000)
            }
            compare(rejectionSpy.count, 3)
            verify(host.visible); verify(Sessions.get(a).id === a); verify(Sessions.get(b).id === b)
            compare(Sessions.closedBrowserRecords("mangaplus").length, 0)
            selectStableNeighbor(b)
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
            tryVerify(function() { var page = deck.browserForSession(b)
                return page && page.documentReady && String(page.url) === FixtureUrl + "/post"
                    && page.title === "POST result" }, 10000)
            var e = deck.entries[b]
            verify(e.hostWindow && e.hostWindow.visible, "Site dialog requires independent canonical native host")
            compare(e.browser.profile, p.profile)
            verify(e.hostWindow.width >= 320 && e.hostWindow.height >= 240)
            tryVerify(function() { return Fixture.hostFrameWithinAvailableScreen(e.hostWindow) }, 5000,
                "Actual native outer frame and controls stay inside available screen geometry")
            compare(js(e.browser, "window.openerMarker"), 192)
            compare(js(e.browser, "window.postBody"), "marker=original-post-body")
            deck.closeTab(b)
            tryVerify(function() { return !deck.entries[b] }, 10000)
        }
        function test_navigationInvalidatesConsentAndLateCloseAcknowledgement() {
            var a = open("/hosting"), p = deck.browserForSession(a)
            function closeEvent() { console.log("NAVIGATION_CLOSE_TRACE", "window-close", a) }
            function rejectEvent() { console.log("NAVIGATION_CLOSE_TRACE", "close-rejected", a) }
            function loadEvent(info) { console.log("NAVIGATION_CLOSE_TRACE", "load", info.status, String(info.url)) }
            p.windowCloseRequested.connect(closeEvent)
            p.closeRejected.connect(rejectEvent)
            p.loadingChanged.connect(loadEvent)
            click(p, "#guard")
            verify(deck.closeTab(a))
            tryVerify(function() { return deck.entries[a].closeConsent }, 5000)
            var old = deck.entries[a].closeConsent
            var oldCallback = deck.entries[a].closeController.closeAcknowledgement
            verify(typeof oldCallback === "function", "App-close callback must capture its original identity and attempt")
            verify(deck.navigateInput(a, FixtureUrl + "/b"))
            verify(!deck.answerClose(a, old.attempt, true))
            tryVerify(function() { return p.documentReady && p.title === "Fixture /b"
                || deck.entries[a].closeConsent !== null }, 10000)
            if (deck.entries[a].closeConsent) verify(deck.answerClose(a, deck.entries[a].closeConsent.attempt, true))
            tryCompare(p, "title", "Fixture /b", 10000)
            // A queued adapter callback retains the OLD attempt/identity. Do
            // not invent an extra untagged engine event after its verified
            // terminal barrier; Qt cannot distinguish that from a new site close.
            verify(!deck.answerClose(a, old.attempt, true))
            if (typeof oldCallback === "function") oldCallback()
            wait(100)
            compare(deck.browserForSession(a), p)
            compare(Sessions.closedBrowserRecords("mangaplus").length, 0)
            p.windowCloseRequested.disconnect(closeEvent)
            p.closeRejected.disconnect(rejectEvent)
            p.loadingChanged.disconnect(loadEvent)
        }
        function test_siteCloseUsesOriginalDocumentConsent() {
            var a = open("/hosting"), p = deck.browserForSession(a)
            js(p, "document.querySelector('#dialog').onclick=function(e){e.preventDefault();window.open('/hosting','site-close','width=520,height=420')}")
            click(p, "#dialog")
            tryVerify(function() { return Sessions.browsers().length === 2 }, 10000)
            var b = Sessions.browsers().filter(function(r) { return r.id !== a })[0].id
            tryVerify(function() { var page = deck.browserForSession(b)
                return page && page.documentReady && String(page.url) === FixtureUrl + "/hosting"
                    && page.title === "Fixture /hosting" }, 10000)
            var child = deck.browserForSession(b)
            click(child, "#guard")
            js(child, "document.querySelector('#dialog').onclick=function(e){e.preventDefault();window.close()}")
            click(child, "#dialog")
            tryVerify(function() { return deck.entries[b].closeConsent }, 5000)
            compare(deck.entries[b].closeConsent.kind, "unload")
            verify(!deck.returnToEmbedded(b), "Held external unload consent must fence host transfer")
            Sessions.switchTo(a)
            verify(deck.answerClose(b, deck.entries[b].closeConsent.attempt, false))
            tryVerify(function() { return !deck.entries[b].closeController.busy }, 5000)
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
            var oldCallback = deck.entries[a].closeController.closeAcknowledgement
            verify(deck.navigateInput(a, FixtureUrl + "/b"))
            verify(!deck.closeTab(a), "In-flight cancelled attempt blocks a second app close")
            tryVerify(function() { return p.title === "Fixture /b" || deck.entries[a].closeConsent }, 10000)
            if (deck.entries[a].closeConsent) verify(deck.answerClose(a, deck.entries[a].closeConsent.attempt, true))
            tryCompare(p, "title", "Fixture /b", 10000)
            verify(deck.closeTab(a))
            var freshAttempt = deck.entries[a].closeController.attempt
            oldCallback() // queued OLD adapter callback, not an invented extra engine terminal
            compare(deck.entries[a].closeController.attempt, freshAttempt)
            compare(Sessions.closedBrowserRecords("mangaplus").length, 0)
            tryVerify(function() { return !deck.entries[a] }, 10000)
        }
        function test_pendingConsentAcrossAccountRetirement() {
            var a = open("/hosting"), p = deck.browserForSession(a)
            click(p, "#guard")
            verify(deck.closeTab(a))
            tryVerify(function() { return deck.entries[a].closeConsent }, 5000)
            var oldConsent = deck.entries[a].closeConsent
            Fixture.switchProfile("retired-account-" + (++serial))
            tryCompare(deck, "quiescing", false, 15000)
            tryVerify(function() { return !deck.entries[a] }, 10000)
            var fresh = open("/b")
            verify(!deck.answerClose(a, oldConsent.attempt, true))
            verify(deck.browserForSession(fresh))
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
            tryVerify(function() { return replayed }, 10000, "Replay comes from the same actual Qt request dispatch")
            p.newWindowRequested.disconnect(duplicate)
            compare(Sessions.browsers().length, 2, "Repeated void-openIn request must not leave an orphan entry/host")
            var b = Sessions.browsers().filter(function(r) { return r.id !== a })[0].id
            tryVerify(function() { var child = deck.browserForSession(b)
                return child && child.documentReady && child.title === "Fixture /hosting"
                    && String(child.url) === FixtureUrl + "/hosting" }, 10000)
            compare(js(deck.browserForSession(b), "window.opener.sessionMarker"), 291)
            compare(deck.entries[a].liveAdoptedChildren, 1)
            verify(deck.closeTab(b))
            tryVerify(function() { return !deck.entries[b] }, 10000, "Accepted child close retires its canonical entry")
            tryCompare(deck.entries[a], "liveAdoptedChildren", 0, 10000,
                "Actual child destruction releases its creation dependency")
        }
        function test_nestedAdoptionKeepsLiveOriginsAndReleasesLeafFirst() {
            var a = open("/hosting"), first = deck.browserForSession(a)
            js(first, "window.sessionMarker=101;document.querySelector('#dialog').onclick=function(e){e.preventDefault();window.open('/hosting','nested-first','width=520,height=420')}")
            click(first, "#dialog")
            tryVerify(function() { return Sessions.browsers().length === 2 }, 10000)
            var b = Sessions.browsers().filter(function(r) { return r.id !== a })[0].id
            tryVerify(function() { var p=deck.browserForSession(b); return p && p.documentReady && p.title === "Fixture /hosting" }, 10000)
            var second = deck.browserForSession(b)
            compare(second.profile, first.profile)
            compare(js(second, "opener.sessionMarker"), 101)
            js(second, "window.sessionMarker=202;document.querySelector('#dialog').onclick=function(e){e.preventDefault();window.open('/hosting','nested-second','width=520,height=420')}")
            click(second, "#dialog")
            tryVerify(function() { return Sessions.browsers().length === 3 }, 10000)
            var c = Sessions.browsers().filter(function(r) { return r.id !== a && r.id !== b })[0].id
            tryVerify(function() { var p=deck.browserForSession(c); return p && p.documentReady && p.title === "Fixture /hosting" }, 10000)
            var third = deck.browserForSession(c)
            compare(third.profile, first.profile)
            compare(js(third, "opener.sessionMarker"), 202)
            click(third, "#edit"); typeText("nested-input")
            compare(js(third, "document.querySelector('#edit').value"), "nested-input")
            compare(deck.entries[a].liveAdoptedChildren, 1)
            compare(deck.entries[b].liveAdoptedChildren, 1)
            verify(deck.closeTab(c)); tryVerify(function() { return !deck.entries[c] }, 10000)
            tryCompare(deck.entries[b], "liveAdoptedChildren", 0, 10000)
            verify(deck.closeTab(b)); tryVerify(function() { return !deck.entries[b] }, 10000)
            tryCompare(deck.entries[a], "liveAdoptedChildren", 0, 10000)
            verify(deck.closeTab(a)); tryVerify(function() { return !deck.entries[a] }, 10000)
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
        function test_externalStayWaitsForEngineBeforeAppRetry() {
            var a = open("/hosting"), p = deck.browserForSession(a)
            js(p, "document.querySelector('#dialog').onclick=function(e){e.preventDefault();window.open('/hosting','external-retry','width=520,height=420')}")
            click(p, "#dialog")
            tryVerify(function() { return Sessions.browsers().length === 2 }, 10000)
            var b = Sessions.browsers().filter(function(r) { return r.id !== a })[0].id
            tryVerify(function() { var q = deck.browserForSession(b)
                return q && q.documentReady && q.title === "Fixture /hosting" }, 10000)
            var child = deck.browserForSession(b), acknowledgements = 0
            function rejected() { ++acknowledgements }
            child.closeRejected.connect(rejected)
            click(child, "#guard")
            js(child, "document.querySelector('#dialog').onclick=function(e){e.preventDefault();window.close()}")
            click(child, "#dialog")
            tryVerify(function() { return deck.entries[b].closeConsent }, 5000)
            verify(deck.answerClose(b, deck.entries[b].closeConsent.attempt, false))
            if (acknowledgements === 0)
                verify(!deck.closeTab(b), "App retry cannot overlap external rejection still in flight")
            tryVerify(function() { return acknowledgements === 1 }, 5000)
            verify(deck.closeTab(b))
            tryVerify(function() { return deck.entries[b].closeConsent }, 5000)
            verify(deck.answerClose(b, deck.entries[b].closeConsent.attempt, false))
            tryVerify(function() { return acknowledgements === 2 }, 5000)
            child.closeRejected.disconnect(rejected)
        }
        function test_navigationStayAllowsLaterNavigation() {
            var a = open("/hosting"), p = deck.browserForSession(a), events = []
            function rejected() { events.push("close-rejected") }
            function loading(info) { events.push("load:" + info.status) }
            p.closeRejected.connect(rejected); p.loadingChanged.connect(loading)
            click(p, "#guard")
            verify(deck.navigateInput(a, FixtureUrl + "/b"))
            tryVerify(function() { return deck.entries[a].closeConsent }, 5000)
            verify(deck.answerClose(a, deck.entries[a].closeConsent.attempt, false))
            wait(200)
            console.log("NAVIGATION_REJECTION_EVENTS", JSON.stringify(events))
            verify(!deck.entries[a].closePending)
            verify(deck.entries[a].closeController.failure.indexOf("navigation cancellation") >= 0)
            verify(!deck.closeTab(a), "Navigation refusal lacks a safe same-document close ACK hook")
            verify(!deck.moveToDesktop(a), "Unacknowledged refusal fences host transfer")
            verify(deck.attachConsentSurface(globalChrome))
            var diagnosticPanel = childMatching(globalChrome, function(item) {
                return String(item).indexOf("FeriaCloseConsent") >= 0 && item.visible && item.diagnostic.length > 0
            })
            verify(diagnosticPanel !== null, "The supported limitation must be visible in reserved chrome")
            js(p, "history.pushState({},'', '#same-document')")
            verify(!deck.closeTab(a), "Fragment changes cannot clear a document barrier")
            var replacementCount = 0, brokenFinished = false, slowStarted = false, slowStopped = false
            function replaced(generation) { ++replacementCount }
            function failedLoad(info) {
                if (String(info.url) === FixtureUrl + "/slow") {
                    if (info.status === WebEngineView.LoadStartedStatus) slowStarted = true
                    if (info.status === WebEngineView.LoadStoppedStatus) slowStopped = true
                }
                if (String(info.url) === FixtureUrl + "/broken"
                    && (info.status === WebEngineView.LoadFailedStatus || info.status === WebEngineView.LoadStoppedStatus))
                    brokenFinished = true
            }
            p.documentReplaced.connect(replaced)
            p.loadingChanged.connect(failedLoad)
            verify(deck.navigateInput(a, FixtureUrl + "/broken"))
            tryVerify(function() { return deck.entries[a].closeConsent }, 5000)
            verify(deck.answerClose(a, deck.entries[a].closeConsent.attempt, true))
            tryVerify(function() { return brokenFinished }, 10000)
            compare(replacementCount, 0, "Failed provisional load cannot claim a replacement document")
            verify(!deck.closeTab(a))
            verify(deck.navigateInput(a, FixtureUrl + "/slow"))
            tryVerify(function() { return slowStarted || deck.entries[a].closeConsent }, 10000)
            if (deck.entries[a].closeConsent) verify(deck.answerClose(a, deck.entries[a].closeConsent.attempt, true))
            tryVerify(function() { return slowStarted && p.loading }, 5000)
            verify(deck.stop(a))
            tryVerify(function() { return slowStopped }, 10000)
            compare(replacementCount, 0, "Explicitly stopped provisional load cannot release close quarantine")
            verify(!deck.closeTab(a))
            verify(deck.navigateInput(a, FixtureUrl + "/b"))
            tryVerify(function() { return p.title === "Fixture /b" || deck.entries[a].closeConsent }, 10000)
            if (deck.entries[a].closeConsent) verify(deck.answerClose(a, deck.entries[a].closeConsent.attempt, true))
            tryCompare(p, "title", "Fixture /b", 10000)
            compare(replacementCount, 1)
            compare(deck.entries[a].closeController.failure, "")
            verify(deck.closeTab(a), "Verified new document permits a fresh close")
            tryVerify(function() { return !deck.entries[a] }, 10000)
            // The retired view and its per-test signal closures are destroyed.
        }
        function test_newSiteCloseAfterCancelledCloseNavigation() {
            var a = open("/hosting"), p = deck.browserForSession(a)
            js(p, "document.querySelector('#dialog').onclick=function(e){e.preventDefault();window.open('/hosting','cancel-then-site','width=520,height=420')}")
            click(p, "#dialog")
            tryVerify(function() { return Sessions.browsers().length === 2 }, 10000)
            var b = Sessions.browsers().filter(function(r) { return r.id !== a })[0].id
            tryVerify(function() { var q=deck.browserForSession(b); return q && q.documentReady && q.title === "Fixture /hosting" }, 10000)
            var child = deck.browserForSession(b)
            click(child, "#guard")
            verify(deck.closeTab(b))
            tryVerify(function() { return deck.entries[b].closeConsent }, 5000)
            verify(deck.navigateInput(b, FixtureUrl + "/b"))
            tryVerify(function() { return child.title === "Fixture /b" || deck.entries[b].closeConsent }, 10000)
            if (deck.entries[b].closeConsent) verify(deck.answerClose(b, deck.entries[b].closeConsent.attempt, true))
            tryCompare(child, "title", "Fixture /b", 10000)
            js(child, "document.querySelector('button').onclick=function(){window.close()}")
            click(child, "button")
            tryVerify(function() { return !deck.entries[b] }, 10000)
            compare(deck.browserForSession(a), p)
        }
        function test_metadataDoesNotStealNativeWindowFocus() {
            var a = open("/hosting"), p = deck.browserForSession(a)
            verify(deck.moveToDesktop(a))
            var host = deck.entries[a].hostWindow
            Sessions.switchTo(a)
            host.requestActivate()
            tryVerify(function() { return Fixture.focusWindowMatches(host) }, 5000)
            root.Window.window.requestActivate()
            tryVerify(function() { return Fixture.focusWindowMatches(root.Window.window) }, 5000)
            Sessions.updateBrowser(a, String(p.url), "Background metadata")
            wait(150)
            verify(Fixture.focusWindowMatches(root.Window.window), "Metadata updates must not raise another window")
        }
        function test_allPagesHiddenConsentUsesReservedChrome() {
            var a = open("/hosting"), p = deck.browserForSession(a)
            click(p, "#guard")
            if (typeof deck.attachConsentSurface === "function") verify(deck.attachConsentSurface(globalChrome))
            Sessions.switchTo("")
            deck.displayed = false; deck.suppressed = true
            verify(deck.closeTab(a))
            tryVerify(function() { return deck.entries[a].closeConsent }, 5000)
            var panel = childMatching(globalChrome, function(item) {
                return String(item).indexOf("FeriaCloseConsent") >= 0 && item.visible
            })
            verify(panel !== null, "Consent must remain visible in caller-reserved chrome when every page is hidden")
            compare(panel.consent.identity.spaceId, a)
            verify(globalChrome.y + globalChrome.height <= embedded.y, "Reserved chrome must not cover website")
            var stay = childMatching(panel, function(item) { return item.text === "Stay" })
            verify(stay !== null)
            mouseClick(stay, stay.width/2, stay.height/2)
            tryVerify(function() { return !deck.entries[a].closePending }, 5000)
            compare(deck.browserForSession(a), p)
        }
        function test_rendererTerminalCancelsBarrierAndReloads() {
            if (Qt.platform.os !== "linux") skip("Scoped renderer SIGTERM fixture is Linux-only")
            var a = open("/hosting"), p = deck.browserForSession(a), terminalCount = 0
            function terminated(status, code) { ++terminalCount; console.log("OWNED_RENDERER_TERMINATED", status, code) }
            p.renderProcessTerminated.connect(terminated)
            click(p, "#guard")
            compare(js(p, "window.sessionMarker=818;window.sessionMarker"), 818)
            verify(deck.closeTab(a))
            tryVerify(function() { return deck.entries[a].closeConsent }, 5000)
            verify(Fixture.terminateOnlyRenderer(p), "Only this fixture's verified renderer child may be terminated")
            tryVerify(function() { return terminalCount === 1 }, 10000)
            compare(deck.entries[a].closeConsent, null)
            compare(Sessions.get(a).browserCloseAttempt, "")
            verify(deck.reload(a))
            tryVerify(function() { return p.documentReady && p.title === "Fixture /hosting"
                && String(p.url) === FixtureUrl + "/hosting" }, 15000)
            compare(js(p, "window.sessionMarker"), 0)
            verify(deck.closeTab(a), "A terminal renderer cannot leave close quarantined after reload")
            tryVerify(function() { return !deck.entries[a] }, 10000)
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
        function test_stoppedReopenRetainsOriginalRecord() {
            var a = open("/reopen-slow")
            verify(deck.closeTab(a)); tryVerify(function() { return !deck.entries[a] }, 10000)
            compare(Sessions.closedBrowserRecords("mangaplus").length, 1)
            var recordId = Sessions.closedBrowserRecords("mangaplus")[0].recordId
            var fresh = deck.reopenClosedSpace("mangaplus")
            verify(fresh && fresh !== a)
            tryVerify(function() { var q=deck.browserForSession(fresh); return q && q.loading }, 10000)
            var page = deck.browserForSession(fresh), stopped = false
            function loading(info) { if (info.status === WebEngineView.LoadStoppedStatus) stopped = true }
            page.loadingChanged.connect(loading)
            verify(deck.stop(fresh))
            tryVerify(function() { return stopped }, 10000)
            compare(Sessions.closedBrowserRecords("mangaplus").length, 1,
                "A stopped provisional reopen cannot consume the user's original closed record")
            compare(Sessions.closedBrowserRecords("mangaplus")[0].recordId, recordId)
            tryVerify(function() { return !deck.entries[fresh] }, 10000,
                "Stopped reconstruction rolls back its new placeholder")
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
