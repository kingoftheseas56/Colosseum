import QtQuick
import QtQuick.Window
import "../../qml/feria"
import "../../qml/feria/FeriaAppMode.js" as AppMode

Window {
    id:window
    width:1280; height:800; visible:true
    property bool finished:false
    property int phase:0
    function finish(ok,detail) {
        if (finished) return
        finished = true; deadline.stop(); poll.stop(); browser.active = false
        browserReporter.report(smokeUrls[0],ok,detail)
        Qt.callLater(function() { browserReporter.finish() })
    }
    function execute(label,script) {
        if (smokeEngine === "webview2") browser.item.executeScript(label,script)
        else browser.item.runJavaScript(script,function(result) { window.result(label,result) })
    }
    function result(label,value) {
        if (label === "mode-ready") {
            if (!value || phase !== 0) return
            phase = -1
            poll.stop()
            // This rule exercises the production installer on an isolated local document.
            // Production provider origins are verified separately by the JS test.
            var profile = {id:"fixture",domains:["127.0.0.1"],kind:"watch",css:"main{min-height:80vh}"}
            execute("mode-installed",AppMode.scriptForProfile(profile,true,true,"#f2c94c"))
        } else if (label === "mode-installed") {
            if (!value || value.adapted !== true) { finish(false,"App adapter did not install"); return }
            execute("mode-check",`(function(){
                var a=document.getElementById('first'),b=document.getElementById('second'),input=document.getElementById('typing');
                a.focus(); var arrow=new KeyboardEvent('keydown',{key:'ArrowRight',bubbles:true,cancelable:true}); a.dispatchEvent(arrow);
                var focused=document.activeElement===b && arrow.defaultPrevented;
                input.focus(); var typing=new KeyboardEvent('keydown',{key:'ArrowLeft',bubbles:true,cancelable:true}); input.dispatchEvent(typing);
                var footer=getComputedStyle(document.querySelector('footer')).display==='none';
                var consent=getComputedStyle(document.getElementById('consent')).display !== 'none';
                return {ok:focused && !typing.defaultPrevented && footer && consent,focus:focused,typing:!typing.defaultPrevented,footer:footer,consent:consent};
            })()`)
        } else if (label === "mode-check") {
            if (!value || !value.ok) { finish(false,JSON.stringify(value)); return }
            execute("mode-restored",AppMode.scriptForProfile({id:"fixture",domains:["127.0.0.1"]},false,false,"#f2c94c"))
        } else if (label === "mode-restored") {
            execute("mode-restore-check","!document.getElementById('feria-app-layout') && getComputedStyle(document.querySelector('footer')).display !== 'none'")
        } else if (label === "mode-restore-check") {
            if (value !== true) { finish(false,"Original site layout was not restored"); return }
            execute("mode-arm-fullscreen","window.__fixtureKeys=[]; document.addEventListener('keydown',function(e){window.__fixtureKeys.push(e.key);if(e.key.toLowerCase() === 'f') document.documentElement.requestFullscreen().catch(function(e){window.__fixtureFullscreenError=String(e)})}); true")
        } else if (label === "mode-arm-fullscreen") {
            window.requestActivate()
            if (smokeEngine === "webview2") browser.item.focusWebView()
            else browser.item.forceActiveFocus()
            gesture.start()
        } else if (label === "mode-fullscreen-check") {
            if (!value || !value.page || browser.item.fullScreenActive !== true) { finish(false,"Fullscreen state did not reach the app shell: " + JSON.stringify(value) + " wrapper=" + browser.item.fullScreenActive); return }
            if (!browserReporter.sendKey(27,browser.item)) { finish(false,"Could not send Escape to owned test window"); return }
            phase = 2; fullscreenCheck.restart()
        } else if (label === "mode-exit-fullscreen-check") {
            if (!value || value.page || browser.item.fullScreenActive !== false || exited) { finish(false,"Fullscreen did not exit cleanly: " + JSON.stringify(value)); return }
            if (!browserReporter.sendKey(121,browser.item)) { finish(false,"Could not send app menu key"); return }
            menuCheck.start()
        }
    }
    Component.onCompleted: {
        browser.setSource(smokeEngine === "webview2" ? "../../qml/feria/FeriaWebView2.qml" : "../../qml/feria/FeriaQtWebEngine.qml",{
            sourceUrl:smokeUrls[0],profilePath:smokeProfileRoot,observationEnabled:false
        })
        poll.start(); deadline.start()
    }
    Loader { id:browser; anchors.fill:parent }
    property bool exited:false
    property bool appMenuRequested:false
    Connections {
        target:browser.item
        function onExitRequested() { window.exited = true }
        function onOptionsRequested() { window.appMenuRequested = true }
    }
    Connections {
        target:smokeEngine === "webview2" ? browser.item : null
        function onScriptResult(label,json) {
            if (label.indexOf("mode-") !== 0) return
            try { window.result(label,JSON.parse(json)) } catch (e) { window.finish(false,"Invalid adapter result: " + json) }
        }
    }
    Timer { id:poll; interval:300; repeat:true; onTriggered:if (browser.item && browser.item.ready) window.execute("mode-ready","!!document.getElementById('first')") }
    Timer { id:deadline; interval:30000; onTriggered:window.finish(false,"App adapter timed out") }
    Timer { id:gesture; interval:800; onTriggered: {
        if (!browserReporter.sendKey(70,browser.item)) { window.finish(false,"Could not send fullscreen gesture to owned test window"); return }
        window.phase = 1; fullscreenCheck.start()
    } }
    Timer { id:fullscreenCheck; interval:1000; onTriggered: {
        window.execute(window.phase === 1 ? "mode-fullscreen-check" : "mode-exit-fullscreen-check",
            "({page:!!document.fullscreenElement,keys:window.__fixtureKeys,error:window.__fixtureFullscreenError || ''})")
    } }
    Timer { id:menuCheck; interval:800; onTriggered:window.finish(window.appMenuRequested,"App layout, arrows, input, restore, fullscreen/Escape and F10 in " + smokeEngine) }
}
