import QtQuick
import QtTest
import "../../qml/feria" as Feria

// Real-engine WebView2 integration through the assembled Colosseum owners:
// SessionStore + shared operation seam + FeriaSessionDeck + FeriaWebView2
// wrapper + native FeriaProviderHost controller. Two retained spaces, local
// fixtures, trusted renderer input. No adapter doubles.
Item {
    width: 1000; height: 700
    Feria.FeriaSessionDeck {
        id: deck
        anchors.fill: parent
        sessions: Sessions
        accountStore: FeriaAccount
        policy: FeriaBrowserPolicy
        displayed: true
    }
    TestCase {
        name: "FeriaWebView2Close"
        when: windowShown
        property int serial: 0
        function init() {
            deck.displayed = true
            deck.startQuiescence(false)
            tryCompare(deck, "quiescing", false, 20000)
            Fixture.switchProfile("wv2-close-" + (++serial))
            tryCompare(deck, "quiescing", false, 20000)
        }
        function cleanup() {
            deck.startQuiescence(false)
            tryCompare(deck, "quiescing", false, 20000)
        }
        function ops() { return Sessions.browserOperations() }
        function openSpace(path, activate) {
            var id = Sessions.openBrowser({providerId: "mangaplus", providerName: "Reading",
                url: FixtureUrl + path, startUrl: FixtureUrl + "/plain",
                engineId: "webview2", mode: "read"}, true, false, activate !== false)
            verify(id.length > 0)
            var entry = deck.entries[id]
            tryVerify(function() { return entry && entry.browser && entry.browser.ready }, 30000)
            tryVerify(function() { return !entry.browser.loading
                && String(entry.browser.url) === FixtureUrl + path
                && entry.browser.title === "Fixture " + path }, 20000)
            return id
        }
        function browserOf(id) { return deck.entries[id] ? deck.entries[id].browser : null }
        function armGuard(id) {
            var browser = browserOf(id)
            verify(browser)
            // The arm button occupies (20,20)-(240,64) in viewport coordinates.
            Fixture.trustedClick(browser, 120, 42)
            tryVerify(function() { return browser.title === "guarded" }, 10000)
        }
        function draftValue(id) {
            var browser = browserOf(id)
            var done = false, value = null
            var handler = function(label, json) {
                if (label !== "draft-read") return
                value = json ? JSON.parse(json) : ""
                done = true
            }
            browser.scriptResult.connect(handler)
            browser.executeScript("draft-read",
                "document.getElementById('draft').value")
            tryVerify(function() { return done }, 5000)
            browser.scriptResult.disconnect(handler)
            return value
        }

        function test_engineSelectionIsWebView2() {
            verify(FeriaBrowserPolicy.webView2Available)
            var id = openSpace("/plain")
            var browser = browserOf(id)
            compare(browser.objectName, "feriaWebView2")
            verify(deck.entries[id].usesWebView2)
        }

        function test_nativeNavigationHistoryAndOutcomes() {
            var id = openSpace("/a")
            var browser = browserOf(id)
            verify(deck.navigateSession(id, FixtureUrl + "/b"))
            tryVerify(function() { return String(browser.url) === FixtureUrl + "/b"
                && browser.title === "Fixture /b" && !browser.loading }, 15000)
            tryVerify(function() { return browser.navCanGoBack && !browser.navCanGoForward }, 5000)
            verify(deck.back(id))
            tryVerify(function() { return String(browser.url) === FixtureUrl + "/a" }, 15000)
            // HistoryChanged can trail traversals on this engine; the host
            // re-queries on a short delay, so wait for the settled state.
            tryVerify(function() { return browser.navCanGoForward }, 5000)
            verify(deck.forward(id))
            tryVerify(function() { return String(browser.url) === FixtureUrl + "/b" }, 15000)
            // Redirect resolves to the destination through native state.
            verify(deck.navigateSession(id, FixtureUrl + "/redirect"))
            tryVerify(function() { return String(browser.url) === FixtureUrl + "/b"
                && browser.title === "Fixture /b" }, 15000)
            // A failed load reports an honest failure, never success.
            verify(deck.navigateSession(id, FixtureUrl + "/broken"))
            tryVerify(function() { return deck.entries[id].errorText.length > 0 }, 20000)
            // A stopped slow load leaves the shell consistent.
            verify(deck.navigateSession(id, FixtureUrl + "/slow"))
            tryVerify(function() { return browser.loading }, 5000)
            verify(deck.stop(id))
            tryVerify(function() { return !browser.loading }, 5000)
        }

        function test_stayPreservesDocumentAndImmediateRetry() {
            var id = openSpace("/guard")
            var browser = browserOf(id)
            armGuard(id)
            tryVerify(function() { return draftValue(id) === "unsaved work" }, 5000)
            var generation = Sessions.browserIdentity(id).documentGeneration
            var historyBack = browser.navCanGoBack
            verify(deck.closeTab(id))
            // Consent for the captured close operation appears in reserved chrome.
            var consent = null
            tryVerify(function() {
                consent = deck.entries[id] ? deck.entries[id].closeConsent : null
                return consent && consent.kind === "close"
            }, 10000)
            compare(consent.origin, String(FixtureUrl).substring(String("http://").length))
            // The document, URL and history are untouched while pending. (The
            // form cannot be script-read here: the renderer suspends JS while
            // the beforeunload deferral is retained; it is read after Stay.)
            compare(String(browser.url), FixtureUrl + "/guard")
            compare(browser.navCanGoBack, historyBack)
            compare(Sessions.browserIdentity(id).documentGeneration, generation)
            var token = consent.attempt
            verify(deck.answerClose(id, token, false))
            tryVerify(function() { return !deck.entries[id].closePending }, 10000)
            // Stay settled cancelled with the explicit deferral decision.
            var result = ops().operationResult(token)
            compare(result.outcome, "cancelled")
            compare(result.evidence, "explicit-stay-deferral-completed")
            compare(String(browser.url), FixtureUrl + "/guard")
            tryVerify(function() { return draftValue(id) === "unsaved work" }, 5000)
            compare(Sessions.browserIdentity(id).documentGeneration, generation)
            // Immediate retry on the same document gets a fresh consent.
            verify(deck.closeTab(id))
            var retry = null
            tryVerify(function() {
                retry = deck.entries[id] ? deck.entries[id].closeConsent : null
                return retry && retry.kind === "close"
            }, 10000)
            verify(retry.attempt !== token)
            verify(deck.answerClose(id, retry.attempt, false))
            tryVerify(function() { return !deck.entries[id].closePending }, 10000)
        }

        function test_leaveClosesSpaceWithCorrelatedDeparture() {
            var a = openSpace("/guard")
            var b = openSpace("/b")
            armGuard(a)
            var browserA = browserOf(a)
            var browserB = browserOf(b)
            verify(deck.navigateSession(b, FixtureUrl + "/plain"))
            tryVerify(function() { return String(browserB.url) === FixtureUrl + "/plain"
                && browserB.title === "Fixture /plain" && !browserB.loading }, 15000)
            var siblingUrl = String(browserB.url)
            var siblingTitle = browserB.title
            var descriptorUrl = FixtureUrl + "/guard"
            verify(deck.closeTab(a))
            var consent = null
            tryVerify(function() {
                consent = deck.entries[a] ? deck.entries[a].closeConsent : null
                return consent && consent.kind === "close"
            }, 10000)
            var token = consent.attempt
            verify(deck.answerClose(a, token, true))
            // The space closes only after the correlated internal navigation
            // completed and the original document actually departed.
            tryVerify(function() { return Sessions.get(a).id === undefined }, 20000)
            tryVerify(function() { return deck.entries[a] === undefined }, 20000)
            // The sibling's document and native history survived untouched.
            tryVerify(function() { return browserB && String(browserB.url) === siblingUrl
                && browserB.title === siblingTitle }, 5000)
            verify(browserB.navCanGoBack)
            // The typed result carries correlated departure evidence.
            var result = ops().operationResult(token)
            compare(result.outcome, "succeeded")
            compare(result.evidence, "correlated-navigation-departure")
            verify(result.nativeNavigationId > 0)
            // The reopen descriptor kept the original document, not about:blank.
            var records = Sessions.closedBrowserRecords("mangaplus")
            verify(records.length > 0)
            compare(records[0].descriptor.url, descriptorUrl)
            compare(records[0].descriptor.engineId, "webview2")
            // The internal unload never advanced the space as a browsed document.
            verify(Sessions.browserIdentity(a).spaceId === undefined)
        }

        function test_plainPageCloseDepartsWithoutConsent() {
            var id = openSpace("/plain")
            verify(deck.closeTab(id))
            // No beforeunload guard and no activation: the page departs without
            // any synthetic consent dialog.
            var sawConsent = false
            var poll = Qt.createQmlObject("import QtQuick; Timer { interval: 100; repeat: true; running: true }", this)
            poll.triggered.connect(function() {
                var entry = deck.entries[id]
                if (entry && entry.closeConsent) sawConsent = true
            })
            tryVerify(function() { return Sessions.get(id).id === undefined }, 20000)
            poll.running = false; poll.destroy()
            verify(!sawConsent)
        }

        function test_cancelledSiteNavigationThenImmediateClose() {
            var id = openSpace("/guard")
            armGuard(id)
            var browser = browserOf(id)
            // A trusted site navigation raises an external unload request.
            browser.dispatchTrustedClick(120, 162) // the /b link
            var consent = null
            tryVerify(function() {
                consent = deck.entries[id] ? deck.entries[id].closeConsent : null
                return consent && consent.kind === "unload"
            }, 10000)
            verify(deck.answerClose(id, consent.attempt, false))
            // Stay cancelled the site navigation; the original document stays.
            tryVerify(function() { return String(browser.url) === FixtureUrl + "/guard"
                && browser.title === "guarded" }, 10000)
            tryVerify(function() { return draftValue(id) === "unsaved work" }, 5000)
            // An immediate app close on the same document still works.
            verify(deck.closeTab(id))
            var close = null
            tryVerify(function() {
                close = deck.entries[id] ? deck.entries[id].closeConsent : null
                return close && close.kind === "close"
            }, 10000)
            verify(deck.answerClose(id, close.attempt, true))
            tryVerify(function() { return Sessions.get(id).id === undefined }, 20000)
            var result = ops().operationResult(close.attempt)
            compare(result.outcome, "succeeded")
        }

        function test_competingNavigationInvalidatesClose() {
            var id = openSpace("/guard")
            armGuard(id)
            verify(deck.closeTab(id))
            var consent = null
            tryVerify(function() {
                consent = deck.entries[id] ? deck.entries[id].closeConsent : null
                return consent && consent.kind === "close"
            }, 10000)
            var staleToken = consent.attempt
            // A deck-dispatched competing navigation invalidates the captured
            // close: the retained dialog is answered as an explicit Stay and
            // the operation settles invalidated, never succeeded. (On this
            // runtime the resumed programmatic navigation to the still-
            // guarded page can stall in a suppressed beforeunload; the
            // invalidation itself is what this case proves. See the delivery
            // gaps.)
            verify(deck.navigateSession(id, FixtureUrl + "/b"))
            tryVerify(function() { return !deck.entries[id].closePending }, 10000)
            var invalidated = ops().operationResult(staleToken)
            compare(invalidated.outcome, "invalidated")
            verify(!deck.answerClose(id, staleToken, true))
            tryVerify(function() { return Sessions.get(id).id !== undefined }, 2000)
        }

        function test_staleConsentAfterReplacementAndDuplicateAnswer() {
            var id = openSpace("/guard")
            armGuard(id)
            verify(deck.closeTab(id))
            var consent = null
            tryVerify(function() {
                consent = deck.entries[id] ? deck.entries[id].closeConsent : null
                return consent && consent.kind === "close"
            }, 10000)
            var staleToken = consent.attempt
            // Answer the close Stay first; the dismissed dialog means the
            // runtime needs a fresh user gesture before another http
            // navigation can raise beforeunload, so the link is clicked
            // afterwards. Its external unload consent is answered Leave.
            verify(deck.answerClose(id, staleToken, false))
            tryVerify(function() { return !deck.entries[id].closePending }, 10000)
            Fixture.trustedClick(browserOf(id), 120, 162)
            var replacement = null
            tryVerify(function() {
                replacement = deck.entries[id] ? deck.entries[id].closeConsent : null
                return replacement && replacement.kind === "unload"
            }, 10000)
            verify(deck.answerClose(id, replacement.attempt, true))
            tryVerify(function() { return String(browserOf(id).url) === FixtureUrl + "/b"
                && browserOf(id).title === "Fixture /b" }, 15000)
            // The stale token cannot answer or close the replacement document,
            // and the consumed external request cannot be answered twice.
            verify(!deck.answerClose(id, staleToken, true))
            verify(!deck.answerClose(id, replacement.attempt, true))
            tryVerify(function() { return Sessions.get(id).id !== undefined }, 2000)
            // A fresh close on the unguarded replacement document departs
            // without any synthetic consent and settles through the seam.
            verify(deck.closeTab(id))
            tryVerify(function() { return Sessions.get(id).id === undefined }, 20000)
            var records = Sessions.closedBrowserRecords("mangaplus")
            verify(records.length > 0)
            verify(records[0].descriptor.url === FixtureUrl + "/b")
        }

        function test_selectionChangeCannotRetargetPendingConsent() {
            var a = openSpace("/guard")
            var b = openSpace("/plain", false)
            armGuard(a)
            verify(deck.closeTab(a))
            var consent = null
            tryVerify(function() {
                consent = deck.entries[a] ? deck.entries[a].closeConsent : null
                return consent && consent.kind === "close"
            }, 10000)
            var token = consent.attempt
            // Selecting the sibling does not move or answer A's request.
            Sessions.switchTo(b)
            tryCompare(Sessions, "activeId", b)
            verify(deck.entries[a].closeConsent && deck.entries[a].closeConsent.attempt === token)
            // Answering through the sibling is refused; A's space is untouched.
            verify(!deck.answerClose(b, token, true))
            tryVerify(function() { return Sessions.get(a).id !== undefined }, 2000)
            Sessions.switchTo(a)
            verify(deck.answerClose(a, token, false))
            tryVerify(function() { return !deck.entries[a].closePending }, 10000)
        }

        function test_pendingDeferralTeardownDrainsSafely() {
            var id = openSpace("/guard")
            armGuard(id)
            verify(deck.closeTab(id))
            tryVerify(function() {
                return deck.entries[id] && deck.entries[id].closeConsent
            }, 10000)
            var token = deck.entries[id].closeConsent.attempt
            // Teardown with a retained native deferral must drain, not hang or
            // crash; the operation settles invalidated, never succeeded.
            Sessions.closeBrowserSessions()
            deck.startQuiescence(false)
            tryVerify(function() { return deck.quiescing === false }, 30000)
            tryVerify(function() { return Sessions.get(id).id === undefined }, 5000)
            var drained = ops().operationResult(token)
            verify(drained.outcome === "invalidated" || drained.outcome === undefined)
            verify(drained.outcome !== "succeeded")
        }

        function test_leaveRequiresCorrelatedDeparture() {
            // RED/GREEN control target: the departure evidence must carry the
            // correlated native navigation id. The unsafe-early-dispose run
            // (FERIA_UNSAFE_EARLY_DISPOSE=1) settles without one and must
            // fail exactly here.
            var id = openSpace("/guard")
            armGuard(id)
            verify(deck.closeTab(id))
            var consent = null
            tryVerify(function() {
                consent = deck.entries[id] ? deck.entries[id].closeConsent : null
                return consent && consent.kind === "close"
            }, 10000)
            var token = consent.attempt
            verify(deck.answerClose(id, token, true))
            tryVerify(function() { return Sessions.get(id).id === undefined }, 20000)
            var result = ops().operationResult(token)
            compare(result.outcome, "succeeded")
            verify(result.nativeNavigationId > 0,
                   "departure settled without a correlated native navigation id")
        }
    }
}
