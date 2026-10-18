import QtQuick
import QtQuick.Window
import QtTest
import QtWebEngine
import "../../qml/feria" as Feria

// Real-engine cases for the native lifecycle contract (FeriaBrowserRuntime):
// the production deck and QtWebEngine wrapper, the real SessionStore, a
// loopback fixture and disposable profiles. No provider account is touched.
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
    TestCase {
        id: tests
        name: "FeriaRuntime"
        when: windowShown
        property int serial: 0
        readonly property var runtime: FeriaBrowserPolicy.runtime
        SignalSpy { id: phaseSpy; target: tests.runtime; signalName: "phaseChanged" }
        SignalSpy { id: settledSpy; target: Sessions.browserOperations(); signalName: "operationSettled" }

        function init() {
            failOnWarning(/TypeError|ReferenceError|incompatible arguments|Could not convert argument/)
            deck.startQuiescence(false)
            tryCompare(deck, "quiescing", false, 15000)
            Fixture.switchProfile("runtime-" + (++serial))
            tryCompare(deck, "quiescing", false, 15000)
            deck.displayed = true; deck.suppressed = false
            phaseSpy.clear(); settledSpy.clear()
        }
        function cleanup() {
            deck.startQuiescence(false)
            tryCompare(deck, "quiescing", false, 15000)
            // Views first, then profiles, and nothing left behind.
            compare(FeriaBrowserPolicy.runtimeViewCount, 0)
            compare(FeriaBrowserPolicy.runtimeProfileCount, 0)
        }
        function descriptor(path, extra) {
            return Object.assign({providerId: "mangaplus", providerName: "Fixture",
                url: FixtureUrl + path, startUrl: FixtureUrl + "/home", mode: "read"}, extra || {})
        }
        function settled(id, path, step) {
            tryVerify(function() { var p = deck.browserForSession(id)
                return p && p.documentReady && !p.loading && String(p.url) === FixtureUrl + path }, 15000,
                "Expected " + path + " to finish loading" + (step ? " after " + step : ""))
            return deck.browserForSession(id)
        }
        // Run one navigation command and wait for the document it produces.
        // The wrapper's URL changes synchronously when a command is issued, so
        // "finished" must include a new navigation generation; otherwise the
        // next command would be issued under the previous load.
        function after(id, path, step, action) {
            var page = deck.browserForSession(id), generation = page.navigationGeneration
            verify(action(), step + " must be accepted")
            tryVerify(function() { return page.navigationGeneration > generation && page.documentReady
                && !page.loading && !page.replacementLoadPending && String(page.url) === FixtureUrl + path }, 15000,
                "Expected " + path + " to finish loading after " + step)
            return page
        }
        // The shell enables Back and Forward from the native history state;
        // they are issued once the engine reports it, as a user could.
        function goBack(id, path) {
            tryVerify(function() { return deck.navigationForSession(id).canGoBack }, 5000, "Back must become available")
            return after(id, path, "back", function() { return deck.back(id) })
        }
        function goForward(id, path) {
            tryVerify(function() { return deck.navigationForSession(id).canGoForward }, 5000, "Forward must become available")
            return after(id, path, "forward", function() { return deck.forward(id) })
        }
        function open(path, extra, fresh) {
            var id = deck.open(descriptor(path, extra), fresh !== false, false, true)
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
        function phases(id) {
            var seen = []
            for (var i = 0; i < phaseSpy.count; ++i)
                if (phaseSpy.signalArguments[i][0] === id) seen.push(phaseSpy.signalArguments[i][1])
            return seen
        }
        function outcomes() {
            var seen = []
            for (var i = 0; i < settledSpy.count; ++i) {
                var result = Sessions.browserOperations().operationResult(settledSpy.signalArguments[i][0])
                seen.push(result.kind + ":" + result.outcome)
            }
            return seen
        }

        function test_identitySurvivesNavigationRedirectAndDuplicateTabs() {
            var a = open("/a")
            var twin = open("/a")                 // explicit New at the same URL
            verify(a !== twin)
            compare(Sessions.browsers().length, 2)
            verify(deck.browserForSession(a) !== deck.browserForSession(twin))
            compare(phases(a).slice(0, 2), ["creating", "ready"])
            // Default open keeps open-or-switch: the provider's last-used space.
            compare(deck.open(descriptor("/a"), false, false, true), twin)
            compare(Sessions.browsers().length, 2)

            var lease = runtime.lease(a), page = deck.browserForSession(a)
            var generation = runtime.instance(a).engineGeneration
            // The server redirects /redirect to /b.
            after(a, "/b", "redirect", function() { return deck.navigateSession(a, FixtureUrl + "/redirect", 0, null, null) })
            compare(page.title, "Fixture /b")
            after(a, "/c", "typed address", function() { return deck.navigateInput(a, FixtureUrl + "/c") })
            goBack(a, "/b")
            goForward(a, "/c")
            after(a, "/c", "reload", function() { return deck.reload(a) })

            // Same activity, same page allocation, same engine view throughout.
            compare(Sessions.get(a).id, a)
            verify(runtime.leaseCurrent(lease))
            compare(runtime.lease(a).pageGeneration, lease.pageGeneration)
            compare(deck.browserForSession(a), page)
            compare(runtime.instance(a).engineGeneration, generation)
            compare(runtime.phase(a), "ready")
            // The twin never moved.
            compare(String(deck.browserForSession(twin).url), FixtureUrl + "/a")
            compare(Sessions.get(twin).target.url, FixtureUrl + "/a")
            // Every operation the commands admitted has reached its one
            // terminal; nothing is left pending against this activity.
            compare(Object.keys(Sessions.browserOperations().pendingOperation(a)).length, 0)
            verify(outcomes().indexOf("navigate:succeeded") >= 0)
            console.log("navigation outcomes:", outcomes().join(" "))
        }

        function test_stopAndRefusedNavigationLeaveThePageUsable() {
            var a = open("/a"), page = deck.browserForSession(a)
            verify(!deck.navigateSession(a, "file:///C:/Windows/win.ini", 0, null, null))
            verify(!deck.navigateInput(a, "javascript:alert(1)"))
            compare(String(page.url), FixtureUrl + "/a")
            verify(!deck.stop(a), "Nothing is loading")
            verify(deck.navigateSession(a, FixtureUrl + "/slow", 0, null, null))
            tryVerify(function() { return deck.navigationForSession(a).loading }, 5000)
            verify(deck.stop(a))
            tryVerify(function() { return !page.loading }, 10000)
            compare(deck.entries[a].errorText, "")
            compare(runtime.phase(a), "ready")
            verify(outcomes().indexOf("navigate:cancelled") >= 0 || outcomes().indexOf("stop:cancelled") >= 0)
            after(a, "/b", "navigation after stop", function() { return deck.navigateSession(a, FixtureUrl + "/b", 0, null, null) })
        }

        function test_parkAndHostMovesKeepOneLiveInstance() {
            var a = open("/a"), page = deck.browserForSession(a)
            js(page, "window.sessionMarker=4242")
            var generation = runtime.instance(a).engineGeneration, views = FeriaBrowserPolicy.runtimeViewCount
            Sessions.switchTo("")                 // parked: the world behind is shown
            verify(!page.visible)
            verify(deck.activate(a))
            compare(Sessions.activeId, a)
            compare(deck.browserForSession(a), page)

            verify(deck.moveToDesktop(a, Qt.rect(140, 140, 700, 520), false, null, ""))
            var host = deck.entries[a].hostWindow, hostLease = host.lease
            verify(runtime.leaseCurrent(hostLease))
            after(a, "/b", "navigation from the detached window",
                  function() { return deck.hostCommand(hostLease, "navigate", FixtureUrl + "/b") })
            verify(deck.returnToEmbedded(a))
            // The window's lease died with the window; the activity did not.
            verify(!runtime.leaseCurrent(hostLease))
            verify(!deck.hostCommand(hostLease, "reload", ""))
            compare(runtime.execute(hostLease, "close", {}).reason, "stale-lease")
            verify(Sessions.get(a).id === a)

            compare(deck.browserForSession(a), page)
            compare(FeriaBrowserPolicy.runtimeViewCount, views)
            compare(runtime.instance(a).engineGeneration, generation)
            compare(phases(a), ["creating", "ready"])   // never recreated
            goBack(a, "/a")
            compare(runtime.phase(a), "ready")
        }

        function test_profileSwitchRejectsStaleCommandsAndCallbacks() {
            var a = open("/a"), page = deck.browserForSession(a)
            var lease = runtime.lease(a)
            var effect = false
            verify(page.evaluateJavaScript("(()=>{var until=Date.now()+600;while(Date.now()<until){};return true})()",
                function() { effect = true }))

            Fixture.switchProfile("runtime-other-" + serial)

            // The old owner is quiesced before the new account is admitted.
            compare(Sessions.get(a).id, undefined)
            verify(!runtime.leaseCurrent(lease))
            var names = ["navigate", "back", "forward", "reload", "stop", "close", "activate"]
            for (var i = 0; i < names.length; ++i) {
                var result = runtime.execute(lease, names[i], {url: FixtureUrl + "/b"})
                verify(!result.accepted, names[i] + " must be rejected for the old account")
                compare(result.reason, "unknown-space")
            }
            verify(!deck.navigateSession(a, FixtureUrl + "/b", 0, null, null))
            tryCompare(deck, "quiescing", false, 15000)
            wait(700)                             // let the old callback come due
            compare(effect, false, "A callback from the old account must not act")
            compare(FeriaBrowserPolicy.runtimeViewCount, 0)

            var b = open("/b")
            var fresh = runtime.lease(b)
            verify(fresh.accountEpoch > lease.accountEpoch)
            var forged = Object.assign({}, fresh, {accountEpoch: lease.accountEpoch})
            compare(runtime.execute(forged, "close", {}).reason, "stale-lease")
            compare(Sessions.get(b).id, b)
            compare(js(deck.browserForSession(b), "1+1"), 2)
        }

        function test_lateScriptResultCannotActOnReplacementDocument() {
            var a = open("/a"), page = deck.browserForSession(a)
            var oldEffect = false
            verify(page.evaluateJavaScript("(()=>{var until=Date.now()+500;while(Date.now()<until){};return location.pathname})()",
                function() { oldEffect = true }))
            verify(runtime.pendingWork(page) > 0)
            after(a, "/b", "navigation under a running script",
                  function() { return deck.navigateSession(a, FixtureUrl + "/b", 0, null, null) })
            tryVerify(function() { return runtime.pendingWork(page) === 0 }, 5000)
            compare(oldEffect, false)
            compare(js(page, "location.pathname"), "/b")
        }

        function test_closeRacingNavigationSettlesOnceAndKeepsTheSpace() {
            var a = open("/a"), page = deck.browserForSession(a)
            var destroyedWithWork = -1
            page.safeToDestroy.connect(function() { destroyedWithWork = runtime.pendingWork(page) })
            settledSpy.clear()
            verify(deck.closeTab(a))
            compare(runtime.phase(a), "close-requested")
            // A navigation arrives before the engine has answered the close.
            after(a, "/b", "navigation racing the close",
                  function() { return deck.navigateSession(a, FixtureUrl + "/b", 0, null, null) })
            compare(Sessions.get(a).id, a)
            compare(deck.browserForSession(a), page)
            compare(runtime.phase(a), "ready")
            var closes = outcomes().filter(function(row) { return row.indexOf("request-close:") === 0 })
            compare(closes, ["request-close:cancelled"])   // one terminal, and not a close
            compare(destroyedWithWork, -1)

            // Retry: with nothing racing it, the same close completes.
            settledSpy.clear()
            verify(deck.closeTab(a))
            tryVerify(function() { return Sessions.get(a).id === undefined }, 10000)
            tryVerify(function() { return destroyedWithWork === 0 }, 10000)
            tryCompare(FeriaBrowserPolicy, "runtimeViewCount", 0, 10000)
            compare(runtime.phase(a), "absent")
            compare(outcomes().filter(function(row) { return row.indexOf("request-close:") === 0 }),
                    ["request-close:succeeded"])
            compare(phases(a).slice(-2), ["retiring", "absent"])
        }

        function test_retiringViewDrainsItsWorkBeforeDestruction() {
            var a = open("/a"), page = deck.browserForSession(a)
            var destroyedWithWork = -1, effect = false
            page.safeToDestroy.connect(function() { destroyedWithWork = runtime.pendingWork(page) })
            verify(page.evaluateJavaScript("(()=>{var until=Date.now()+600;while(Date.now()<until){};return true})()",
                function() { effect = true }))
            Sessions.close(a)                     // the canonical record goes first
            compare(page.closingSession, true)
            compare(runtime.phase(a), "retiring")
            verify(runtime.pendingWork(page) > 0, "The engine still owes this view a callback")
            compare(FeriaBrowserPolicy.runtimeViewCount, 1)
            compare(destroyedWithWork, -1)
            verify(!page.evaluateJavaScript("1"), "A retiring view admits no new work")
            // Destruction waits for the callback, and the callback cannot act.
            tryVerify(function() { return destroyedWithWork === 0 }, 10000)
            tryCompare(FeriaBrowserPolicy, "runtimeViewCount", 0, 10000)
            compare(effect, false)
            compare(runtime.phase(a), "absent")
        }

        function test_failedCreationIsClosableAndLeavesNoOrphan() {
            verify(!FeriaBrowserPolicy.webView2Available, "This fixture build has no WebView2 adapter")
            // An engine this build cannot provide: the space exists canonically
            // but no engine view is ever created for it.
            var blocked = deck.open(descriptor("/a", {engineId: "webview2"}), true, false, true)
            verify(blocked.length > 0)
            compare(deck.browserForSession(blocked), null)
            compare(runtime.phase(blocked), "failed")
            compare(runtime.instance(blocked).reason, "engine-unavailable")
            verify(deck.entries[blocked].errorText.length > 0)
            verify(!deck.back(blocked))
            verify(!deck.recover(blocked), "The engine is still unavailable")
            compare(FeriaBrowserPolicy.runtimeViewCount, 0)

            verify(deck.closeTab(blocked))
            compare(Sessions.get(blocked).id, undefined)
            tryVerify(function() { return !deck.entries[blocked] }, 5000)
            compare(runtime.phase(blocked), "absent")
            compare(Sessions.browsers().length, 0)
            verify(outcomes().indexOf("request-close:succeeded") >= 0)
        }

        function test_rendererLossKeepsIdentityWithNewEngineGeneration() {
            var a = open("/a"), page = deck.browserForSession(a)
            var lease = runtime.lease(a), generation = runtime.instance(a).engineGeneration
            tryVerify(function() { return page.renderProcessPid > 0 }, 5000)
            var effect = false
            verify(page.evaluateJavaScript("(()=>{var until=Date.now()+750;while(Date.now()<until){};return true})()",
                function() { effect = true }))
            verify(Fixture.terminateOnlyRenderer(page), "Only this fixture's own renderer may be terminated")
            tryCompare(page, "rendererLost", true, 10000)
            compare(runtime.phase(a), "failed")
            compare(runtime.instance(a).reason, "renderer-lost")
            compare(runtime.pendingWork(page), 0)
            verify(runtime.instance(a).engineGeneration > generation)
            verify(!page.evaluateJavaScript("1"), "A lost renderer admits no work")
            verify(!deck.back(a))

            // The canonical activity is exactly as it was.
            compare(Sessions.browsers().length, 1)
            verify(runtime.leaseCurrent(lease))
            compare(runtime.lease(a).pageGeneration, lease.pageGeneration)

            after(a, "/a", "recovery", function() { return deck.recover(a) })
            compare(page.rendererLost, false)
            compare(runtime.phase(a), "ready")
            compare(deck.browserForSession(a), page)
            compare(effect, false, "The dead renderer's callback must never act")
            compare(js(page, "location.pathname"), "/a")
        }

        function test_popupIsAdoptedIntoItsOwnInstance() {
            var a = open("/a"), opener = deck.browserForSession(a)
            js(opener, "window.sessionMarker=77")
            var before = Sessions.browsers().length
            opener.Window.window.requestActivate()
            tryVerify(function() { return Fixture.focusWindowMatches(opener.Window.window) }, 5000)
            opener.forceActiveFocus(); waitForRendering(opener, 1000)
            var point = js(opener, "(()=>{let r=document.querySelector('form button').getBoundingClientRect();return {x:r.x+r.width/2,y:r.y+r.height/2}})()")
            mouseClick(opener, point.x, point.y)   // a real gesture: POST into a new tab
            tryVerify(function() { return Sessions.browsers().length === before + 1 }, 10000)
            var child = Sessions.activeId
            verify(child !== a)
            tryVerify(function() { var p = deck.browserForSession(child)
                return p && p.title === "POST result" && !p.loading }, 15000)
            var page = deck.browserForSession(child)
            tryVerify(function() { return page.documentReady }, 10000)
            // The original request was adopted, not replayed as a URL.
            compare(js(page, "window.postBody"), "marker=original-post-body")
            compare(js(page, "window.openerMarker"), 77)
            compare(page.profile, opener.profile)
            // Its own activity, its own bound instance, observed by real events.
            compare(deck.entries[child].adoptionState, "observed")
            compare(runtime.phase(child), "ready")
            verify(runtime.instance(child).bound)
            verify(runtime.lease(child).pageGeneration !== runtime.lease(a).pageGeneration)
            compare(runtime.phase(a), "ready")
        }

        function test_permissionPromptIsBoundToTheDocumentThatAsked() {
            var a = open("/a"), page = deck.browserForSession(a), requests = page.browserRequests
            // 127.0.0.1 is a trustworthy origin, so the engine really asks.
            verify(page.evaluateJavaScript("navigator.geolocation.getCurrentPosition("
                + "function(){window.geoResult='granted'},function(e){window.geoResult='denied:'+e.code});true"))
            tryVerify(function() { return requests.current !== null }, 10000, "The engine must raise a permission prompt")
            var prompt = requests.current
            compare(prompt.kind, "permission")
            verify(prompt.work > 0, "A prompt from a real engine view holds a native ticket")
            verify(runtime.workValid(prompt.work))

            // The document that asked is replaced while the prompt is open.
            after(a, "/b", "navigation under an open prompt",
                  function() { return deck.navigateSession(a, FixtureUrl + "/b", 0, null, null) })
            compare(requests.current, null)
            verify(!runtime.workValid(prompt.work))
            // A late Allow for the old document grants nothing to the new one.
            verify(!requests.answer(prompt.id, true))
            compare(requests.pending.length, 0)
        }

        function test_websiteScriptHasNoPathToBrowserControl() {
            var a = open("/a"), page = deck.browserForSession(a)
            var surface = js(page, "[typeof qt, typeof QWebChannel, typeof FeriaBrowserPolicy, typeof Sessions,"
                + " typeof FeriaAccount, typeof chrome === 'object' && chrome !== null && 'webview' in chrome].join('|')")
            compare(surface, "undefined|undefined|undefined|undefined|undefined|false")
        }

        function test_privateSpaceIsIsolatedAndLeavesNoRestoreRecord() {
            var regular = open("/a"), page = deck.browserForSession(regular)
            js(page, "document.cookie='feria_regular=1; max-age=86400; path=/'; localStorage.setItem('feria_regular','1')")
            var secret = open("/b", {privateBrowsing: true}), hidden = deck.browserForSession(secret)
            verify(hidden.profile !== page.profile)
            verify(hidden.profile.offTheRecord)
            verify(runtime.lease(secret).profileId.endsWith("/private"))
            verify(runtime.lease(regular).profileId.endsWith("/ordinary"))
            compare(js(hidden, "document.cookie + '|' + localStorage.getItem('feria_regular')"), "|null")
            js(hidden, "document.cookie='feria_private=1; max-age=86400; path=/'; localStorage.setItem('feria_private','1')")
            compare(js(page, "document.cookie.indexOf('feria_private') + '|' + localStorage.getItem('feria_private')"), "-1|null")

            // Closing the private space records nothing reopenable.
            verify(deck.closeTab(secret))
            tryVerify(function() { return Sessions.get(secret).id === undefined }, 10000)
            compare(Sessions.closedBrowserRecords("mangaplus").length, 0)
            // The saved tab snapshot never names a private space.
            deck.persistTabs()
            var saved = deck.tabDescriptors()
            compare(saved.length, 1)
            compare(saved[0].privateBrowsing === true, false)
            tryVerify(function() { return deck.privateProfile === null }, 10000,
                      "The private profile goes with its last space")
        }
    }
}
