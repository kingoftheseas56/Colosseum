import QtQuick
import QtTest
import QtWebEngine
import "../../../qml/feria" as Feria

Item {
    id: surface
    width: 960; height: 640
    property var page: null
    WebEngineProfile { id: profile; offTheRecord: true }
    Component {
        id: pageFactory
        Feria.FeriaQtWebEngine {
            width: surface.width; height: surface.height
            sourceUrl: FixtureUrl + "/media"
            profilePath: ""
            sharedProfile: profile
            observationEnabled: false
            freezeBackground: false
        }
    }
    TestCase {
        name: "FeriaOwnedMedia16"
        when: windowShown
        function init() {
            page = pageFactory.createObject(surface)
            verify(page !== null)
            tryVerify(function() { return page.documentReady && !page.loading }, 15000)
        }
        function cleanup() {
            page.prepareForClose(false)
            tryCompare(page, "pendingJavaScript", 0, 5000)
            page.destroy(); page = null; wait(50)
        }
        function evaluate(script) {
            var done = false, result
            verify(page.evaluateJavaScript(script, function(value) { result = value; done = true }))
            tryVerify(function() { return done }, 5000)
            return result
        }
        function startMedia() {
            evaluate("window.playing=document.getElementById('playing');window.manual=document.getElementById('manual');window.video=document.getElementById('video');playing.play();video.play();true")
            tryVerify(function() { return evaluate("!playing.paused && playing.currentTime>0.05 && !video.paused && video.currentTime>0.05") }, 15000)
            compare(evaluate("manual.paused"), true)
        }
        function suspend() {
            page.activitySuspended = true
            tryVerify(function() { return page.presentationResult.suspended === true && page.presentationResult.outcome !== "pending" }, 5000)
        }
        function resume() {
            page.activitySuspended = false
            tryVerify(function() { return page.presentationResult.suspended === false && page.presentationResult.outcome !== "pending" }, 5000)
        }
        function frame() {
            evaluate("window.child=document.createElement('iframe');child.src='/media-frame';document.body.appendChild(child);true")
            tryVerify(function() { return evaluate("!!child.contentDocument && !!child.contentDocument.getElementById('frameAudio')") }, 10000)
            evaluate("window.oldDoc=child.contentDocument;window.oldMedia=oldDoc.getElementById('frameAudio');window.oldResumeCalls=0;oldMedia.play=function(){++window.oldResumeCalls;return HTMLMediaElement.prototype.play.call(this)};oldMedia.play();true")
            tryVerify(function() { return evaluate("oldMedia.currentTime>0.05 && !oldMedia.paused") }, 10000)
            evaluate("oldResumeCalls=0;true")
        }
        function test_realAudioVideoOwnedOnlyAndInactiveVisibility() {
            startMedia()
            // Hidden/inactive view alone is not an app/world-away reason.
            page.visible = false; wait(250)
            compare(evaluate("!playing.paused && !video.paused"), true)
            page.visible = true
            suspend()
            compare(evaluate("playing.paused && video.paused && manual.paused"), true)
            resume()
            compare(evaluate("!playing.paused && !video.paused && manual.paused"), true)
            compare(page.presentationResult.outcome, "succeeded")
            compare(page.presentationResult.coverage.limited, true)
            verify(Fixture.captureWindow(surface, "category16-owned-audio-video.png"))
        }
        function test_explicitPauseAndPageInputRevokeOwnership() {
            startMedia(); suspend()
            evaluate("playing.pause();document.body.dispatchEvent(new Event('pointerdown',{bubbles:true}));true")
            resume(); wait(150)
            compare(evaluate("playing.paused && video.paused && manual.paused"), true)
        }
        function test_retiredIframeNeverInvokesOwnedPlay() {
            frame(); suspend()
            compare(evaluate("oldMedia.paused"), true)
            evaluate("child.remove();true")
            resume()
            compare(evaluate("oldResumeCalls"), 0, "Detached iframe documents must not be resumed")
            compare(page.presentationResult.outcome, "invalidated")
        }
        function test_navigatedIframeNeverInvokesOldDocumentPlay() {
            frame(); suspend()
            evaluate("child.src='/media-frame-replacement';true")
            tryVerify(function() { return evaluate("child.contentDocument!==oldDoc && !!child.contentDocument && !!child.contentDocument.getElementById('frameAudio')") }, 10000)
            resume()
            compare(evaluate("oldResumeCalls"), 0)
            compare(evaluate("child.contentDocument.getElementById('frameAudio').paused"), true)
        }
        function test_sameSourceReloadAndReplacementRevokeOwnership() {
            startMedia(); suspend()
            evaluate("playing.load();window.replacement=video.cloneNode(true);video.replaceWith(replacement);true")
            tryVerify(function() { return evaluate("playing.readyState>=3 && replacement.readyState>=3") }, 10000)
            resume(); wait(150)
            compare(evaluate("playing.paused && replacement.paused"), true)
        }
        function test_newMainDocumentDoesNotInheritReceipt() {
            startMedia(); suspend()
            var generation = page.navigationGeneration
            page.url = FixtureUrl + "/media?new-document=1"
            tryVerify(function() { return page.navigationGeneration>generation && page.documentReady && !page.loading && !page.replacementLoadPending }, 15000)
            resume()
            compare(evaluate("Array.from(document.querySelectorAll('audio,video')).every(function(m){return m.paused})"), true)
        }
        function test_playRejectionHasObservedBlockedResult() {
            startMedia()
            // Deterministic rejection uses a real decoded element, with only
            // play's result replaced before the presentation owner captures it.
            evaluate("playing.play=function(){return Promise.reject(new DOMException('fixture autoplay refusal','NotAllowedError'))};true")
            suspend(); resume()
            compare(page.presentationResult.outcome, "blocked")
            compare(evaluate("playing.paused"), true)
            compare(evaluate("video.paused"), false)
        }
        function test_crossOriginFrameIsExplicitlyUninspected() {
            var otherOrigin = String(FixtureUrl).replace("127.0.0.1", "localhost")
            if (otherOrigin === String(FixtureUrl)) otherOrigin = String(FixtureUrl).replace("localhost", "127.0.0.1")
            verify(otherOrigin !== String(FixtureUrl))
            evaluate("window.remote=document.createElement('iframe');remote.src=" + JSON.stringify(otherOrigin + "/media-frame") + ";document.body.appendChild(remote);true")
            wait(500); suspend()
            verify(page.presentationResult.coverage.inaccessibleFrames > 0)
            compare(page.presentationResult.coverage.limited, true)
        }
        function test_lateFrameAndClosedShadowRemainExplicitlyLimited() {
            suspend()
            frame()
            compare(evaluate("oldMedia.paused"), false, "Late undiscovered frame is outside bounded observation")
            evaluate("window.host=document.createElement('div');document.body.appendChild(host);window.closedRoot=host.attachShadow({mode:'closed'});window.closedAudio=document.getElementById('playing').cloneNode(true);closedRoot.appendChild(closedAudio);closedAudio.play();true")
            tryVerify(function() { return evaluate("closedAudio.currentTime>0.05 && !closedAudio.paused") }, 10000)
            compare(page.presentationResult.coverage.limited, true)
            verify(page.presentationResult.coverage.exclusions.indexOf("undiscovered late roots") >= 0)
            verify(page.presentationResult.coverage.exclusions.indexOf("closed shadow roots") >= 0)
            // These are capability exclusions, not universal-pause successes.
        }
        function test_pendingResumeCannotAuthorizeFreeze() {
            startMedia()
            evaluate("playing.play=function(){return new Promise(function(){})};true")
            suspend(); page.activitySuspended = false
            tryVerify(function() { return page.presentationResult.outcome === "pending" }, 5000)
            page.freezeDelay = 50; page.freezeBackground = true; page.visible = false
            wait(300)
            compare(page.lifecycleState, WebEngineView.LifecycleState.Active)
            compare(page.presentationResult.outcome, "pending")
        }
        function test_rendererLossInvalidatesPendingResumeObservation() {
            startMedia()
            evaluate("playing.play=function(){return new Promise(function(){})};true")
            suspend()
            page.activitySuspended = false
            tryVerify(function() { return page.presentationResult.outcome === "pending" }, 5000)
            verify(Fixture.terminateOnlyRenderer(page))
            tryCompare(page, "rendererLost", true, 10000)
            compare(page.presentationResult.outcome, "invalidated")
            compare(page.presentationResult.reason, "renderer-lost")
            compare(page.pendingJavaScript, 0)
        }
    }
}
