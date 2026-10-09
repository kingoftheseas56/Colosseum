import QtQuick
import QtQuick.Window
import "../../qml/feria" as Feria

Window {
    id: root
    width: 1000; height: 680; visible: true
    property int stage: 0
    property int ticks: 0
    property var checks: []
    property var backend: null
    property var controls: null
    property string oldDocument: ""
    property string oldSession: ""
    property bool busy: false
    property bool websiteChecked: false
    property var observations: []
    function check(name,pass) {
        checks.push({name:name,passed:!!pass}); console.log(pass?"PASS":"FAIL",name)
        if (!pass) finish(false)
        return pass
    }
    function finish(pass) {
        timer.stop()
        runtimeProbe.finish(evidenceDirectory+"/outcomes.json",{passed:pass,checks:checks,stage:stage,qtBrowser:true})
    }
    function find(item,name) {
        if (!item) return null
        if(item.objectName===name)return item
        for(var child of item.children || []) {var result=find(child,name);if(result)return result}
        return null
    }
    function click(id, next) {
        busy=true
        var label = id === "toggle" ? (root.stage===1 ? "Play" : "Pause") : ""
        controls.runJavaScript("(()=>{if(document.body.dataset.playerReady!=='true')return null;var e=document.getElementById("+JSON.stringify(id)+");if(!e||e.disabled||("+JSON.stringify(label)+"&&e.textContent!=="+JSON.stringify(label)+"))return null;var r=e.getBoundingClientRect();return {x:r.x+r.width/2,y:r.y+r.height/2}})()",function(point){
            if (!point) {busy=false;return}
            runtimeProbe.click(point.x,point.y)
            stage=next; busy=false
        })
    }
    Feria.FeriaQtWebEngine {
        id: browser
        anchors.fill: parent
        sourceUrl: fixtureUrl+"/website"
        profilePath: evidenceDirectory+"/profile"
        providerId: "native-test"
        accountSessionId: "native-test-tab"
        observationEnabled: true
        resumePosition: 4
        onPlaybackObserved: function(sample) { if(browser.nativePlaybackActive)root.observations.push(sample) }
        onMessage: function(text) { console.log("PLAYER_MESSAGE",text) }
    }
    Timer {
        id: timer
        interval:100; repeat:true; running:true
        onTriggered: {
            if (++root.ticks>580) {root.check("runtime watchdog",false);return}
            if(root.busy)return
            if(root.ticks%30===0) {
                var debugPlayer=root.find(browser,"feriaNativePlayer")
                console.log("RUNTIME_STAGE",root.stage,"ACTIVE",browser.nativePlaybackActive,"STATE",debugPlayer?JSON.stringify(debugPlayer.currentState):"none")
            }
            if(root.stage===0 && browser.documentReady) {
                root.check("website playback entry dispatches",browser.playInFeria())
                root.stage=1
            } else if(root.stage===1 && browser.nativePlaybackActive) {
                var item=root.find(browser,"feriaNativePlayer")
                if(!item || !item.currentState.ready)return
                root.backend=item.backend;root.controls=root.find(item,"feriaNativeHtmlControls")
                root.oldDocument=item.currentState.document;root.oldSession=item.currentState.session
                if(!root.websiteChecked) {
                    root.check("website source decoded by existing mpv",item.currentState.duration>=35)
                    root.check("saved progress reaches native player",Math.abs(item.currentState.position-4)<.2)
                    root.websiteChecked=true
                }
                root.click("toggle",2)
            } else if(root.stage===2 && !root.backend.currentState.paused && root.backend.currentState.position>4.5
                    && browser.resumePosition===0 && root.observations.length>0) {
                root.check("HTML play advances native playback",true)
                root.check("native picture with HTML controls saved",runtimeProbe.capture(evidenceDirectory+"/website-native.png"))
                root.check("existing progress gate confirms native resume",root.observations[0].position>=4)
                root.click("toggle",3)
            } else if(root.stage===3 && root.backend.currentState.paused) {
                root.check("HTML pause controls native playback",true)
                root.check("finite seek accepted",root.backend.act(root.oldDocument,root.oldSession,"seek",3).ok)
                root.stage=4
            } else if(root.stage===4 && Math.abs(root.backend.currentState.position-3)<.2) {
                root.check("native seek reached expected frame",runtimeProbe.capture(evidenceDirectory+"/website-seek.png"))
                browser.activitySuspended=true
                root.stage=5
            } else if(root.stage===5) {
                root.check("hidden-player controls rejected",!root.backend.act(root.oldDocument,root.oldSession,"play",0).ok)
                browser.activitySuspended=false
                root.click("close",6)
            } else if(root.stage===6 && !browser.nativePlaybackActive) {
                root.check("HTML back releases native player",true)
                root.busy=true
                browser.evaluateJavaScript("(()=>{var m=document.querySelector('video');return {position:m.currentTime,paused:m.paused}})()",function(sample){
                    root.check("handback preserves native seek position",sample && Math.abs(sample.position-3)<.2 && sample.paused)
                    browser.resumePosition=25
                    root.stage=7; root.busy=false
                })
            } else if(root.stage===7 && browser.ready && !browser.loadActive) {
                root.check("direct URL playback entry dispatches",browser.playInFeria(fixtureUrl+"/ffv1-pcm.mkv"))
                root.stage=8
            } else if(root.stage===8 && browser.nativePlaybackActive) {
                var item=root.find(browser,"feriaNativePlayer")
                if(!item || !item.currentState.ready)return
                root.backend=item.backend
                root.check("direct FFV1 source decoded",item.currentState.duration>=11)
                root.check("different direct URL does not inherit website resume",item.currentState.position<2)
                root.check("old website session cannot control direct source",!root.backend.act(root.oldDocument,root.oldSession,"pause",0).ok)
                root.check("direct video picture saved",runtimeProbe.capture(evidenceDirectory+"/direct-native.png"))
                browser.sourceUrl=fixtureUrl+"/website?new-document=1"
                root.stage=9
            } else if(root.stage===9 && browser.documentReady && !browser.nativePlaybackActive) {
                root.check("navigation releases native surface",true)
                browser.prepareForClose(false)
                root.check("close cannot start a native source",!browser.playInFeria())
                root.finish(true)
            }
        }
    }
}
