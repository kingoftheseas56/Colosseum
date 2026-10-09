import QtQuick
import QtQuick.Window
import "../../qml/feria" as Feria

Window {
    id: root
    width:1000; height:680; visible:true
    title:"Feria — actual YouTube playback test"
    property int stage:0
    property int ticks:0
    property var checks:[]
    property var nativePlayer:null
    property var observations:[]
    property real beforePause:0
    function check(name,pass) {
        checks.push({name:name,passed:!!pass});console.log(pass?"PASS":"FAIL",name)
        if(!pass)finish(false)
        return pass
    }
    function finish(pass) {
        timer.stop()
        runtimeProbe.finish(evidenceDirectory+"/outcomes.json",{passed:pass,checks:checks,watchUrl:fixtureUrl,stage:stage})
    }
    function find(item,name) {
        if(!item)return null
        if(item.objectName===name)return item
        for(var child of item.children||[]){var found=find(child,name);if(found)return found}
        return null
    }
    Feria.FeriaQtWebEngine {
        id: browser
        anchors.fill:parent
        sourceUrl:fixtureUrl
        profilePath:evidenceDirectory+"/profile"
        providerId:"youtube"
        accountSessionId:"live-youtube-tab"
        onMessage:function(text){console.log("PLAYER_MESSAGE",text);if(root.stage===1)root.check("YouTube source available",false)}
        onPlaybackObserved:function(sample){if(browser.nativePlaybackActive)root.observations.push(sample)}
    }
    Timer {
        id:timer
        interval:100;repeat:true;running:true
        onTriggered:{
            if(++root.ticks>1400){root.check("live YouTube watchdog",false);return}
            if(root.stage===0 && browser.documentReady){
                // Resolve the actual watch page, including its browser-owned blob source.
                root.check("actual YouTube page handoff dispatches",browser.playInFeria())
                root.stage=1
            } else if(root.stage===1 && browser.nativePlaybackActive){
                var player=root.find(browser,"feriaNativePlayer")
                if(!player)return
                if(player.currentState.error){console.log("NATIVE_ERROR",player.currentState.error);root.check("YouTube stream loads",false);return}
                if(!player.currentState.ready || player.currentState.audioTracks<1)return
                root.nativePlayer=player
                root.check("actual YouTube video and audio tracks loaded",player.currentState.duration>15 && player.currentState.audioTracks>0)
                root.check("first actual YouTube picture captured",runtimeProbe.capture(evidenceDirectory+"/youtube-first.png"))
                var state=player.currentState
                root.check("YouTube native play accepted",player.backend.act(state.document,state.session,"play",0).ok)
                root.beforePause=state.position
                root.stage=2
            } else if(root.stage===2 && root.nativePlayer.currentState.position>root.beforePause+2 && root.observations.length>0){
                var state=root.nativePlayer.currentState
                root.check("actual YouTube advances and uses existing progress owner",root.observations[0].href.indexOf("youtube.com/watch?v=")>0)
                root.check("second actual YouTube picture captured",runtimeProbe.capture(evidenceDirectory+"/youtube-playing.png"))
                root.nativePlayer.backend.act(state.document,state.session,"pause",0)
                root.nativePlayer.backend.act(state.document,state.session,"seek",10)
                root.stage=3
            } else if(root.stage===3 && root.nativePlayer.currentState.paused && Math.abs(root.nativePlayer.currentState.position-10)<.3){
                root.check("actual YouTube seek changes picture",runtimeProbe.capture(evidenceDirectory+"/youtube-seek.png"))
                browser.suppressed=true
                var state=root.nativePlayer.currentState
                root.check("background YouTube controls rejected",!root.nativePlayer.backend.act(state.document,state.session,"play",0).ok)
                browser.suppressed=false
                root.nativePlayer.closePlayer()
                root.check("actual YouTube handback releases native player",!browser.nativePlaybackActive)
                browser.prepareForClose(false)
                root.finish(true)
            }
        }
    }
}
