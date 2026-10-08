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
        name: "FeriaMediaVisibilityDiagnostic"
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
        function snapshot(phase) {
            var result = evaluate("(function(){var ledger=window.__feriaPresentationMedia;return {visibility:document.visibilityState,events:window.events.slice(),media:['playing','manual','video'].map(function(id){var m=document.getElementById(id),r=ledger&&ledger.records.get(m);return {id:id,paused:m.paused,ended:m.ended,muted:m.muted,volume:m.volume,time:m.currentTime,duration:m.duration,ready:m.readyState,network:m.networkState,jsPauseCalls:m.jsPauseCalls,owned:!!r&&r.owned}}),ledger:ledger?{suspended:ledger.suspended,epoch:ledger.epoch,serial:ledger.serial,result:ledger.result||null}:null}})()")
            console.log("MEDIA_VISIBILITY_DIAGNOSTIC", JSON.stringify({phase:phase,visible:page.visible,
                activitySuspended:page.activitySuspended,presentationSerial:page.presentationSerial,
                lifecycleState:page.lifecycleState,recommendedState:page.recommendedState,
                recentlyAudible:page.recentlyAudible,dom:result}))
            return result
        }
        function inspect(muted) {
            evaluate("window.events=[];['playing','manual','video'].forEach(function(id){var m=document.getElementById(id),pause=m.pause;m.jsPauseCalls=0;m.pause=function(){++m.jsPauseCalls;return pause.apply(this,arguments)};['play','playing','pause','ended','emptied','waiting'].forEach(function(type){m.addEventListener(type,function(){events.push({id:id,type:type,time:m.currentTime,paused:m.paused,at:performance.now()})})})});document.getElementById('video').muted=" + (muted?"true":"false") + ";document.getElementById('playing').play();document.getElementById('video').play();true")
            tryVerify(function() { return evaluate("!document.getElementById('playing').paused && document.getElementById('playing').currentTime>0.05 && !document.getElementById('video').paused && document.getElementById('video').currentTime>0.05") }, 15000)
            snapshot(muted?"muted-visible-before":"audible-visible-before")
            page.visible = false; wait(250)
            snapshot(muted?"muted-hidden-250ms":"audible-hidden-250ms")
            wait(1000)
            snapshot(muted?"muted-hidden-1250ms":"audible-hidden-1250ms")
            page.visible = true; wait(250)
            snapshot(muted?"muted-visible-return":"audible-visible-return")
            compare(page.activitySuspended,false)
            compare(page.lifecycleState,WebEngineView.LifecycleState.Active)
        }
        function test_mutedVideoVisibility() { inspect(true) }
        function test_audibleVideoVisibility() { inspect(false) }
    }
}
