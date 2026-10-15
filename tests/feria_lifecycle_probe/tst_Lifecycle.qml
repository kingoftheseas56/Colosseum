import QtQuick
import QtTest
import QtWebEngine
import "../../qml/feria" as Feria
import "../../qml/feria/FeriaJavaScript.js" as JavaScript

Item {
    id: surface
    width: 960; height: 640
    property var page: null
    WebEngineProfile { id: testProfile; offTheRecord: true }
    Component {
        id: pageFactory
        Feria.FeriaQtWebEngine {
            width: surface.width; height: surface.height
            sourceUrl: FixtureUrl + "/music"
            profilePath: ""
            sharedProfile: testProfile
            observationEnabled: false
            freezeBackground: false
        }
    }
    SignalSpy { id: completedSpy; target: surface.page; signalName: "completed" }
    SignalSpy { id: clearedSpy; target: surface.page; signalName: "localSessionCleared" }
    TestCase {
        name: "FeriaLifecycle"
        when: windowShown
        function init() {
            surface.page = pageFactory.createObject(surface)
            verify(surface.page !== null)
            tryVerify(function() { return page.documentReady && !page.loading }, 15000)
            completedSpy.clear()
            clearedSpy.clear()
        }
        function cleanup() {
            page.prepareForClose(false)
            tryCompare(page, "pendingJavaScript", 0, 5000)
            page.destroy()
            surface.page = null
            wait(50)
        }
        function evaluate(script) {
            var done = false, result
            verify(page.evaluateJavaScript(script, function(value) { result = value; done = true }))
            tryVerify(function() { return done }, 5000)
            return result
        }
        function playingPair() {
            evaluate("window.manual=document.getElementById('audio').cloneNode(true); manual.id='manual'; document.body.appendChild(manual); document.getElementById('audio').play(); true")
            tryVerify(function() { return evaluate("!document.getElementById('audio').paused && document.getElementById('audio').currentTime > 0.05") }, 10000)
        }
        function test_transitionResumesOnlyItsPlayingMedia() {
            playingPair()
            verify(page.activitySuspended !== undefined, "Presentation suspension interface must exist")
            page.activitySuspended = true
            tryVerify(function() { return evaluate("document.getElementById('audio').paused") }, 5000)
            compare(evaluate("manual.paused"), true)
            page.activitySuspended = false
            tryVerify(function() { return evaluate("!document.getElementById('audio').paused") }, 5000)
            compare(evaluate("manual.paused"), true, "Already manually paused media must remain paused")
        }
        function test_laterManualPauseRevokesAutomaticResume() {
            playingPair()
            verify(page.activitySuspended !== undefined)
            page.activitySuspended = true
            tryVerify(function() { return evaluate("document.getElementById('audio').paused") }, 5000)
            evaluate("document.getElementById('audio').pause(); true")
            page.activitySuspended = false
            wait(300)
            compare(evaluate("document.getElementById('audio').paused"), true)
        }
        function test_pageInteractionRevokesAutomaticResume() {
            playingPair()
            page.activitySuspended = true
            tryVerify(function() { return evaluate("document.getElementById('audio').paused") }, 5000)
            // Custom provider controls may hold a pre-hook native pause method.
            evaluate("document.body.dispatchEvent(new Event('pointerdown',{bubbles:true})); true")
            page.activitySuspended = false
            wait(300)
            compare(evaluate("document.getElementById('audio').paused"), true)
        }
        function test_sameOriginFramePlayingMediaIsScoped() {
            evaluate("window.child=document.createElement('iframe');child.src='/music';document.body.appendChild(child);true")
            tryVerify(function() { return evaluate("!!child.contentDocument && !!child.contentDocument.getElementById('audio')") }, 10000)
            evaluate("child.contentDocument.getElementById('audio').play();true")
            tryVerify(function() { return evaluate("child.contentDocument.getElementById('audio').currentTime>0.05") }, 10000)
            page.activitySuspended = true
            tryVerify(function() { return evaluate("child.contentDocument.getElementById('audio').paused") }, 5000)
            page.activitySuspended = false
            tryVerify(function() { return evaluate("!child.contentDocument.getElementById('audio').paused") }, 5000)
            compare(evaluate("document.getElementById('audio').paused"), true)
        }
        function test_rapidTransitionsHonorLatestIntent() {
            playingPair()
            page.activitySuspended = true
            page.activitySuspended = false
            page.activitySuspended = true
            tryVerify(function() { return evaluate("document.getElementById('audio').paused") }, 5000)
            page.activitySuspended = false
            tryVerify(function() { return evaluate("!document.getElementById('audio').paused") }, 5000)
        }
        function test_dispatchedTransitionsHonorLatestIntent() {
            playingPair()
            // Exercise commands already queued in the renderer, beyond the
            // same-turn Qt.callLater coalescing path.
            page.activitySuspended = true
            page.applyPresentationActivity()
            page.activitySuspended = false
            page.applyPresentationActivity()
            page.activitySuspended = true
            page.applyPresentationActivity()
            wait(300)
            compare(evaluate("document.getElementById('audio').paused"), true)
        }
        function test_replacementDocumentCannotResumeOldMedia() {
            playingPair()
            page.activitySuspended = true
            tryVerify(function() { return evaluate("document.getElementById('audio').paused") }, 5000)
            var generation = page.navigationGeneration
            page.url = FixtureUrl + "/a"
            tryVerify(function() { return page.navigationGeneration > generation && !page.loading && page.documentReady && !page.replacementLoadPending }, 10000)
            page.activitySuspended = false
            wait(300)
            compare(evaluate("document.getElementById('audio').paused"), true)
        }
        function test_laterPlayingMediaIsHeldWhileSuspended() {
            page.activitySuspended = true
            wait(100)
            evaluate("window.later=document.getElementById('audio').cloneNode(true);document.body.appendChild(later);true")
            tryVerify(function() { return evaluate("later.readyState>=3") }, 5000)
            evaluate("later.play().catch(function(){});true")
            wait(300)
            compare(evaluate("later.paused"), true)
            page.activitySuspended = false
            tryVerify(function() { return evaluate("!later.paused") }, 5000)
            compare(evaluate("document.getElementById('audio').paused"), true)
        }
        function test_navigationWhileSuspendedHoldsNewDocumentMedia() {
            playingPair()
            page.activitySuspended = true
            tryVerify(function() { return evaluate("document.getElementById('audio').paused") }, 5000)
            var generation = page.navigationGeneration
            page.url = FixtureUrl + "/a"
            tryVerify(function() { return page.navigationGeneration > generation && !page.loading && page.documentReady && !page.replacementLoadPending }, 10000)
            evaluate("document.getElementById('audio').play().catch(function(){});true")
            wait(300)
            compare(evaluate("document.getElementById('audio').paused"), true)
            page.activitySuspended = false
            tryVerify(function() { return evaluate("!document.getElementById('audio').paused") }, 5000)
        }
        function test_closeBarrierInhibitsFreeze() {
            verify(page.lifecycleBlocked !== undefined, "Close barrier must inhibit lifecycle freeze")
            page.lifecycleBlocked = true
            page.freezeDelay = 50
            page.freezeBackground = true
            page.visible = false
            wait(250)
            compare(page.lifecycleState, WebEngineView.LifecycleState.Active)
            page.lifecycleBlocked = false
            tryCompare(page, "lifecycleState", WebEngineView.LifecycleState.Frozen, 10000)
            page.visible = true
            tryCompare(page, "lifecycleState", WebEngineView.LifecycleState.Active, 5000)
        }
        function test_suspensionKeepsEligibleFreezeTimerRunning() {
            page.freezeDelay = 50
            page.freezeBackground = true
            page.visible = false
            page.activitySuspended = true
            tryCompare(page, "lifecycleState", WebEngineView.LifecycleState.Frozen, 10000)
            page.visible = true
            page.activitySuspended = false
            tryCompare(page, "lifecycleState", WebEngineView.LifecycleState.Active, 5000)
        }
        function test_terminalInvalidatesProvisionalReadiness() {
            page.url = FixtureUrl + "/slow"
            tryCompare(page, "replacementLoadPending", true, 5000)
            var before = page.navigationGeneration
            page.renderProcessTerminated(1, 9)
            verify(page.navigationGeneration > before, "Terminal must fence queued DOM readiness")
            wait(700)
            compare(page.documentReady, false)
            compare(page.rendererLost, true)
            for (var args of completedSpy.signalArguments) verify(args[1] !== true)
        }
        function test_normalRendererRetirementIsSilent() {
            var before = page.navigationGeneration
            page.renderProcessTerminated(0, 0)
            compare(page.rendererLost, false)
            compare(page.documentReady, true)
            compare(page.navigationGeneration, before)
            compare(completedSpy.count, 0)
        }
        function test_terminalFencesAlreadyDispatchedCallback() {
            var called = false
            verify(page.evaluateJavaScript("true", function() { called = true }))
            page.renderProcessTerminated(1, 9)
            tryCompare(page, "pendingJavaScript", 0, 5000)
            compare(called, false)
            compare(page.rendererLost, true)
        }
        function test_terminalDoesNotAdmitGenerationSideEffectScripts() {
            tryCompare(page, "pendingJavaScript", 0, 5000)
            // navigationGeneration changes synchronously invalidate media state.
            // Its script request must be denied before that signal can fire.
            page.renderProcessTerminated(1, 9)
            compare(page.pendingJavaScript, 0)
            compare(page.rendererLost, true)
        }
        function test_retiredTicketCannotConsumeFreshPendingWork() {
            tryCompare(page, "pendingJavaScript", 0, 5000)
            var oldEffect = false
            verify(page.evaluateJavaScript("(()=>{var until=Date.now()+500;while(Date.now()<until){};return true})()", function() { oldEffect = true }))
            var oldTicket = page.javascriptTickets[page.javascriptTicketSerial]
            var lateResult = JavaScript.resultCallback(oldTicket)
            page.renderProcessTerminated(1, 9)
            compare(page.pendingJavaScript, 0)
            compare(oldTicket.owner, null)
            compare(oldTicket.callback, null)
            var generation = page.navigationGeneration
            page.url = FixtureUrl + "/a"
            tryVerify(function() { return page.navigationGeneration > generation && !page.rendererLost && !page.loading }, 10000)
            var freshEffect = false
            verify(page.evaluateJavaScript("(()=>{var until=Date.now()+300;while(Date.now()<until){};return true})()", function() { freshEffect = true }))
            var pending = page.pendingJavaScript
            verify(pending > 0)
            lateResult(true)
            lateResult(true)
            compare(page.pendingJavaScript, pending)
            tryVerify(function() { return freshEffect }, 5000)
            tryCompare(page, "pendingJavaScript", 0, 5000)
            compare(oldEffect, false)
        }
        function test_liveCloseStillDrainsPhysicalCallbacks() {
            tryCompare(page, "pendingJavaScript", 0, 5000)
            var effect = false
            verify(page.evaluateJavaScript("(()=>{var until=Date.now()+300;while(Date.now()<until){};return true})()", function() { effect = true }))
            page.prepareForClose(false)
            compare(page.closingSession, true)
            verify(page.pendingJavaScript > 0, "Live close must retain its callback barrier")
            tryCompare(page, "pendingJavaScript", 0, 5000)
            compare(effect, false)
        }
        function test_partialCleanupContinuesAndReportsFailure() {
            // Simulate an origin whose storage primitives cannot all finish.
            // The remaining origin must still be visited, with a final false.
            page.clearingSession = true
            page.clearDomains = []
            page.clearOrigins = [FixtureUrl]
            page.acceptCleanupResult("partial")
            tryCompare(clearedSpy, "count", 1, 10000)
            compare(clearedSpy.signalArguments[0][0], false)
            compare(page.clearOrigins.length, 0)
            compare(page.clearingSession, false)
            page.clearLocalSession([], [])
            compare(clearedSpy.count, 2)
            compare(clearedSpy.signalArguments[1][0], true)
        }
        function test_onlyRealSuccessfulLoadResolvesTerminal() {
            verify(page.rendererLost !== undefined)
            var realLoads = 0
            function success(location) { ++realLoads }
            page.mainDocumentLoaded.connect(success)
            page.renderProcessTerminated(1, 9)
            page.url = FixtureUrl + "/slow"
            tryCompare(page, "replacementLoadPending", true, 5000)
            page.stopLoad()
            wait(500)
            compare(realLoads, 0)
            compare(page.rendererLost, true)
            page.url = FixtureUrl + "/a"
            tryVerify(function() { return realLoads === 1 && !page.rendererLost }, 15000)
            page.mainDocumentLoaded.disconnect(success)
        }
    }
}
