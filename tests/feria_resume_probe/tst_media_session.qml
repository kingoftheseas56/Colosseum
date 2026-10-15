import QtQuick
import QtTest
import "../../qml/feria" as Feria
TestCase {
    id: suite
    name: "FeriaRealSessionStore"
    property var gate: null
    property real nowMs: 0
    property int emitted: 0
    property var account: feriaTestStore
    Component { id: gateComponent; Feria.FeriaMediaSession {} }
    function sample(position, generation) {
        return {href:"https://music.youtube.com/watch?v=abcdefghijk", providerMediaId:"abcdefghijk",
            identityProvider:"ytmusic", kind:"audio", title:"Track", duration:1000, position:position,
            paused:false, readyState:4, rate:1, mediaGeneration:generation || "doc:1", sourceKey:"ytmusic\nabcdefghijk"}
    }
    function init() {
        account.clearHistory();account.beginVisit({pk:"ytmusic"})
        verify(account.observe(sample(600)))
        account.beginVisit({pk:"ytmusic",resumePending:true})
        nowMs=0;emitted=0
        gate=gateComponent.createObject(suite,{providerId:"ytmusic",sourceUrl:sample(0).href,
            initialPosition:600,clock:function(){return suite.nowMs}})
        verify(gate!==null)
        gate.protectionChanged.connect(function(blocked){account.setMediaCaptureBlocked(blocked)})
        gate.observation.connect(function(value){emitted++;account.observe(value)})
    }
    function cleanup() {gate.destroy();gate=null}
    function deliver(value) {gate.acceptResult(gate.poll(),{outcome:"observation",sample:value})}
    function test_early_and_failed_restore_preserve_real_bookmark() {
        var early=sample(20);early.readyState=1;deliver(early)
        compare(emitted,0);compare(account.continueItems[0].position,600)
        nowMs=16000;deliver(sample(20))
        compare(emitted,0);verify(gate.state.blocked)
        verify(!account.observe(sample(20)),"even direct generic leakage is rejected by the real store gate")
        compare(account.continueItems[0].position,600)
        gate.cancelResume(true);deliver(sample(20))
        compare(emitted,1);compare(account.continueItems[0].position,20)
    }
    function test_later_settlement_and_out_of_order_callback() {
        var stale=gate.poll(), current=gate.poll()
        gate.acceptResult(current,{outcome:"observation",sample:sample(20)})
        gate.acceptResult(stale,{outcome:"observation",sample:sample(600)})
        compare(gate.state.tries,1);compare(emitted,0)
        compare(gate.state.confirmations,0)
        nowMs=2000;deliver(sample(602));compare(emitted,0)
        nowMs=4000;deliver(sample(604));compare(emitted,1)
        compare(account.continueItems[0].position,604)
    }
    function test_connector_holds_reach_real_store() {
        gate.cancelResume(true)
        for (var reason of ["ad", "transition", "identity-conflict", "malformed-clock"]) {
            gate.acceptResult(gate.poll(), {outcome:"hold",reason:reason})
            verify(!account.observe(sample(20)), "no raw generic persistence leak during " + reason)
            compare(account.continueItems[0].position,600)
        }
        gate.acceptResult(gate.poll(), {outcome:"error",reason:"fixture-failure"})
        verify(!account.observe(sample(20)))
        deliver(sample(40, "new-coherent-generation"))
        compare(account.continueItems[0].position,40)
    }
    function test_navigation_rejects_old_callback() {
        gate.navigationChanged() // initial engine navigation binds the request
        var old=gate.poll();gate.navigationChanged()
        gate.acceptResult(old,{outcome:"observation",sample:sample(20)})
        compare(emitted,0);compare(gate.state.target,0)
        compare(account.continueItems[0].position,600)
    }
    function test_slow_ordered_results_do_not_starve_capture() {
        gate.cancelResume(true)
        var label = gate.poll()
        for (var i = 1; i <= 7; ++i) {
            nowMs = i * 2000
            var next = gate.poll()
            nowMs = (i - 1) * 2000 + 2500
            gate.acceptResult(label, {outcome:"observation",sample:sample(50)})
            label = next
        }
        compare(emitted, 7)
        compare(account.continueItems[0].position, 50)
    }
    function test_newer_hold_supersedes_older_observation() {
        gate.cancelResume(true)
        var older = gate.poll(), newer = gate.poll()
        gate.acceptResult(newer, {outcome:"hold",reason:"identity-conflict"})
        gate.acceptResult(older, {outcome:"observation",sample:sample(20)})
        compare(emitted, 0)
        verify(!account.observe(sample(20)))
        compare(account.continueItems[0].position, 600)
    }
    function test_lost_callback_expires_and_later_request_recovers() {
        gate.cancelResume(true)
        var expired = gate.poll()
        nowMs = 12000
        gate.acceptResult(expired, {outcome:"observation",sample:sample(20)})
        compare(emitted, 0)
        var current = gate.poll()
        gate.acceptResult(current, {outcome:"observation",sample:sample(40)})
        compare(emitted, 1)
        compare(account.continueItems[0].position, 40)
    }
    function test_outstanding_requests_are_bounded() {
        gate.cancelResume(true)
        for (var i = 0; i < 100; ++i) gate.poll()
        verify(gate.pendingRequests !== undefined)
        verify(Object.keys(gate.pendingRequests).length <= 8)
    }
}
