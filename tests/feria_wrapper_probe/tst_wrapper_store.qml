// Reviewer integration fixture: instantiates each REAL engine wrapper and the real
// store. Script-result payloads are deterministic injections, not live site proof.
import QtQuick
import QtQuick.Window
import QtTest
TestCase {
    id: suite
    name: "FeriaWrapperStore"
    when: windowShown
    Window { id: stage; visible:true; width:800; height:600 }
    property var browser: null
    property var gate: null
    property real nowMs: 0
    property var account: feriaTestStore
    function deliver(frame) {
        deliverResult(gate.poll(), frame)
    }
    function deliverResult(label, frame) {
        if (browser.objectName === "feriaWebView2") browser.scriptResult(label, JSON.stringify(frame))
        else browser.receiveMediaResult(label, frame)
    }
    function test_pending_restore_data() {
        return [{tag:"QtWebEngine",file:"FeriaQtWebEngine.qml"},
                {tag:"WebView2",file:"FeriaWebView2.qml"}]
    }
    function test_pending_restore(row) {
        var sample={href:"https://music.youtube.com/watch?v=abcdefghijk",title:"Track",kind:"audio",
            providerMediaId:"abcdefghijk",identityProvider:"ytmusic",position:600,duration:1000,
            paused:false,readyState:4,rate:1,mediaGeneration:"fixture:1",sourceKey:"ytmusic\nabcdefghijk"}
        account.clearHistory();account.beginVisit({pk:"ytmusic"});verify(account.observe(sample))
        var component=Qt.createComponent("../../qml/feria/"+row.file)
        compare(component.status,Component.Ready,component.errorString())
        browser=component.createObject(stage.contentItem,{width:800,height:600,sourceUrl:"about:blank",
            providerId:"ytmusic",profilePath:feriaTestRoot+"/"+row.tag})
        verify(browser!==null)
        gate=findChild(browser,"feriaMediaSession");verify(gate!==null)
        gate.clock=function(){return suite.nowMs}
        browser.captureProtectionChanged.connect(function(blocked){account.setMediaCaptureBlocked(blocked)})
        browser.playbackObserved.connect(function(value){account.observe(value)})
        account.beginVisit({pk:"ytmusic",resumePending:true});nowMs=0;gate.requestResume(600,sample.href)
        sample.position=20;sample.readyState=1
        deliver({outcome:"observation",sample:sample})
        compare(account.continueItems[0].position,600)
        nowMs=16000;sample.readyState=4
        deliver({outcome:"observation",sample:sample})
        compare(account.continueItems[0].position,600)
        verify(!account.observe(sample),"the real store must reject a raw generic leak while protected")
        browser.cancelMediaResume(true)
        deliver({outcome:"observation",sample:sample})
        compare(account.continueItems[0].position,20)
        for (var reason of ["ad", "transition", "identity-conflict", "malformed-clock"]) {
            deliver({outcome:"hold",reason:reason})
            verify(!account.observe(sample), "hold must reach native store through " + row.tag)
        }
        deliver({outcome:"error",reason:"fixture-failure"})
        verify(!account.observe(sample))
        sample.position=40
        deliver({outcome:"observation",sample:sample})
        compare(account.continueItems[0].position,40)
        // Both actual transport entry points accept healthy in-flight results
        // after a later poll, while a newer hold still suppresses older data.
        var older = gate.poll(), newer = gate.poll()
        sample.position = 50
        deliverResult(older, {outcome:"observation",sample:sample})
        compare(account.continueItems[0].position, 50)
        deliverResult(newer, {outcome:"hold",reason:"transition"})
        sample.position = 20
        deliverResult(older, {outcome:"observation",sample:sample})
        verify(!account.observe(sample))
        compare(account.continueItems[0].position, 50)
        browser.destroy();browser=null;gate=null
    }
    function cleanup() {if(browser)browser.destroy();browser=null;gate=null}
}
