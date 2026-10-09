import QtQuick
import QtQuick.Window
import "../../qml/feria" as Feria

Window {
    id:root
    width:1000;height:680;visible:true
    title:"Feria — DRM browser-path verification"
    property bool busy:false
    property bool attemptedNative:false
    property bool nativeDeclined:false
    property var latest:null
    property real firstTime:-1
    property int ticks:0
    property var completion:null
    function finish() {
        timer.stop()
        var playback=latest && latest.video && latest.video.mediaKeys && latest.video.frames>0
            && firstTime>=0 && latest.video.time>firstTime+2
        runtimeProbe.capture(evidenceDirectory+"/drm-page.png")
        completion={
            passed:!!playback && nativeDeclined,browserPlayback:!!playback,nativeEncryptedHandoffDeclined:nativeDeclined,
            nativePlaybackActive:browser.nativePlaybackActive,state:latest,drmPath:"QtWebEngine EME; not mpv"}
        browser.prepareForClose(false)
    }
    Feria.FeriaQtWebEngine {
        id:browser
        anchors.fill:parent
        sourceUrl:fixtureUrl.split('#')[0]
        profilePath:evidenceDirectory+"/profile"
        providerId:"youtube"
        accountSessionId:"drm-verification-tab"
        settings.playbackRequiresUserGesture:false
        onMessage:function(text){console.log("DRM_HANDOFF_MESSAGE",text);if(root.attemptedNative && !browser.nativePlaybackActive)root.nativeDeclined=true}
        onSafeToDestroy:if(root.completion)runtimeProbe.finish(evidenceDirectory+"/outcomes.json",root.completion)
    }
    Timer {
        id:timer
        interval:1000;repeat:true;running:true
        onTriggered:{
            if(++root.ticks>=70){root.finish();return}
            if(!browser.documentReady || root.busy)return
            root.busy=true
            var production=fixtureUrl.indexOf('#production')>=0
            browser.evaluateJavaScript("(()=>{var b=document.getElementById('loadContentBtn');if(b&&!window.__feriaDrmLoaded){window.__feriaDrmLoaded=true;var select=document.getElementById('backendSelect');if(select&&"+production+")select.value='https://proxy.widevine.com/proxy';b.click()}var v=document.querySelector('video');if(v){v.muted=true;if(v.readyState>=2&&v.paused)v.play().catch(()=>{})}var log=document.getElementById('logBox');return {url:location.href,licenseLog:log?log.textContent:'',video:v?{time:v.currentTime,frames:v.getVideoPlaybackQuality().totalVideoFrames,mediaKeys:!!v.mediaKeys,readyState:v.readyState,paused:v.paused,error:v.error?{code:v.error.code,message:v.error.message}:null}:null}})()",function(state){
                root.busy=false;root.latest=state
                if(!state || !state.video)return
                console.log("DRM_STATE",JSON.stringify(state))
                if(state.video.mediaKeys && !root.attemptedNative){root.attemptedNative=true;browser.playInFeria()}
                if(state.video.mediaKeys && state.video.frames>0 && state.video.time>0){
                    if(root.firstTime<0)root.firstTime=state.video.time
                    else if(state.video.time>root.firstTime+3 && root.nativeDeclined)root.finish()
                }
                if(/LICENSE_REQUEST_FAILED|Shaka Error/.test(state.licenseLog) && root.nativeDeclined)root.finish()
            })
        }
    }
}
