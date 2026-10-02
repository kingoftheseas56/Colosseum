import QtQuick
import QtQuick.Window
import Colosseum.FeriaHost 1.0

Window {
    id: root
    width: 1100
    height: 760
    minimumWidth: 760
    minimumHeight: 520
    visible: true
    color: "#171717"
    title: "Feria A1 Host Probe"

    property bool overlayOpen: false
    property bool automationStarted: false
    property string lastStatus: "loading"
    property int pendingWebKey: 0
    property var pendingWebKeyDone: null
    property int webFocusAttempts: 0
    property int a2ContentSamples: 0
    property bool a2ResumeSawAd: false

    function later(ms, callback) {
        stepTimer.stop()
        stepTimer.interval = ms
        stepTimer.callback = callback
        stepTimer.start()
    }

    function recordGeometry(label) {
        const json = host.captureGeometry(label)
        probe.record("geometry", json)
        return json
    }

    function capture(label, popup) {
        return popup === true
            ? probe.capturePopup(label) : probe.capture(root, label)
    }

    function finishSoon() {
        later(800, function() { probe.quit() })
    }

    function startAutomation() {
        if (automationStarted)
            return
        automationStarted = true
        probe.record("automation-start", probe.automation)
        if (probe.automation === "geometry") {
            runGeometry()
        } else if (probe.automation === "overlay") {
            runOverlay()
        } else if (probe.automation === "focus") {
            runFocus()
        } else if (probe.automation === "profile-write") {
            runProfileWrite()
        } else if (probe.automation === "profile-read"
                   || probe.automation === "profile-fresh") {
            runProfileRead()
        } else if (probe.automation === "policy") {
            runPolicy()
        } else if (probe.automation === "netflix") {
            runNetflix()
        } else if (probe.automation === "a2-home") {
            runA2Home()
        } else if (probe.automation === "a2-shorts") {
            runA2Shorts()
        } else if (probe.automation === "a2-dwell") {
            runA2Dwell()
        } else if (probe.automation === "a2-watch") {
            runA2Watch()
        } else if (probe.automation === "a2-complete") {
            runA2Complete()
        } else if (probe.automation === "a2-resume") {
            runA2Resume()
        } else if (probe.automation === "a2-ad") {
            runA2Passive("ad", 90000)
        }
    }

    function a2State(label) {
        host.executeScript(label,
            "(()=>{const v=document.querySelector('video, audio');" +
            "return {href:location.href,time:v?v.currentTime:null," +
            "duration:v?v.duration:null,paused:v?v.paused:null," +
            "ended:v?v.ended:null,title:navigator.mediaSession.metadata?" +
            "navigator.mediaSession.metadata.title:null," +
            "adShowing:!!document.querySelector('#movie_player.ad-showing')," +
            "adInterrupting:!!document.querySelector('#movie_player.ad-interrupting')};})()")
    }

    function runA2Passive(label, waitMs) {
        a2State("a2-" + label + "-start")
        later(waitMs, function() {
            a2State("a2-" + label + "-end")
            capture("a2-" + label, false)
            finishSoon()
        })
    }

    function runA2Home() {
        host.executeScript("a2-home-scroll",
            "(()=>{window.scrollBy(0,650);return {scrollY:window.scrollY};})()")
        later(1000, function() {
            probe.hover(root, 550, 320)
            host.executeScript("a2-home-hover",
                "(()=>{const e=document.elementFromPoint(520,210);" +
                "const a=e&&e.closest('a[href^=\"/watch\"]');" +
                "return {tag:e?e.tagName:null,href:a?a.href:null};})()")
            later(8000, function() {
                a2HomeMediaScan("a2-home-hover-media-1")
                host.executeScript("a2-home-inline-play",
                    "(()=>{const vs=Array.from(document.querySelectorAll('video'));" +
                    "const v=vs.find(x=>{const r=x.getBoundingClientRect();" +
                    "return r.width>0&&r.height>0;});" +
                    "if(v){v.muted=true;v.play();}" +
                    "const r=v?v.getBoundingClientRect():null;" +
                    "return {count:vs.length,found:!!v," +
                    "rect:r?{x:r.x,y:r.y,width:r.width,height:r.height}:null};})()")
                later(3000, function() {
                    a2HomeMediaScan("a2-home-hover-media-2")
                    capture("a2-home-inline-playing", false)
                    probe.restoreCursor()
                    runA2Passive("home", 5000)
                })
            })
        })
    }

    function a2HomeMediaScan(label) {
        host.executeScript(label,
            "Array.from(document.querySelectorAll('video, audio')).map(v=>" +
            "{const r=v.getBoundingClientRect();return " +
            "{time:v.currentTime,duration:v.duration,paused:v.paused," +
            "ended:v.ended,rect:{x:r.x,y:r.y,width:r.width,height:r.height}}})")
    }

    function runA2Shorts() {
        host.executeScript("a2-shorts-play",
            "(()=>{const v=document.querySelector('video');" +
            "if(v){v.muted=true;v.play();}" +
            "return {found:!!v,paused:v?v.paused:null};})()")
        runA2Passive("shorts", 12000)
    }

    function runA2Dwell() {
        a2State("a2-dwell-start")
        later(300000, function() {
            a2State("a2-dwell-end")
            capture("a2-dwell-5min", false)
            finishSoon()
        })
    }

    function runA2Watch() {
        host.executeScript("a2-watch-play",
            "(()=>{const v=document.querySelector('video');" +
            "if(v){v.muted=true;v.play();}" +
            "return {found:!!v,paused:v?v.paused:null};})()")
        later(10000, function() {
            a2State("a2-watch-before-pause")
            host.executeScript("a2-watch-pause-seek",
                "(()=>{const v=document.querySelector('video');" +
                "if(!v)return {found:false};" +
                "v.pause();const before=v.currentTime;" +
                "v.currentTime=Math.min(v.duration-15,before+20);" +
                "return {before:before,after:v.currentTime,paused:v.paused};})()")
            later(1400, function() {
                a2State("a2-watch-after-seek")
                host.executeScript("a2-watch-resume",
                    "(()=>{const v=document.querySelector('video');" +
                    "if(v)v.play();return {found:!!v};})()")
                later(9000, function() {
                    a2State("a2-watch-after-resume")
                    capture("a2-watch", false)
                    finishSoon()
                })
            })
        })
    }

    function runA2Complete() {
        host.executeScript("a2-complete-play",
            "(()=>{const v=document.querySelector('video');" +
            "if(v){v.muted=true;v.play();}" +
            "return {found:!!v,paused:v?v.paused:null};})()")
        later(35000, function() {
            a2State("a2-complete-end")
            capture("a2-complete", false)
            finishSoon()
        })
    }

    function runA2Resume() {
        a2State("a2-resume-before-play")
        host.executeScript("a2-resume-play",
            "(()=>{const v=document.querySelector('video');" +
            "if(v){v.muted=true;v.play();}" +
            "return {found:!!v,startingTime:v?v.currentTime:null};})()")
        later(1400, function() {
            a2State("a2-resume-after-play")
            capture("a2-resume", false)
            finishSoon()
        })
    }

    function runGeometry() {
        recordGeometry("geometry-windowed")
        capture("geometry-windowed", false)
        root.width = 900
        root.height = 620
        later(1600, function() {
            recordGeometry("geometry-resized")
            capture("geometry-resized", false)
            root.showFullScreen()
            later(1800, function() {
                recordGeometry("geometry-fullscreen")
                capture("geometry-fullscreen", false)
                root.showNormal()
                root.width = 1100
                root.height = 760
                later(1800, function() {
                    recordGeometry("geometry-restored")
                    capture("geometry-restored", false)
                    finishSoon()
                })
            })
        })
    }

    function runOverlay() {
        recordGeometry("overlay-before")
        capture("overlay-before", false)
        host.suppressed = true
        overlayOpen = true
        later(1200, function() {
            recordGeometry("overlay-open")
            capture("overlay-open", false)
            overlayOpen = false
            host.suppressed = false
            later(1400, function() {
                recordGeometry("overlay-restored")
                capture("overlay-restored", false)
                finishSoon()
            })
        })
    }

    function runFocus() {
        runFocusCycle(1)
    }

    function requestWebKey(vk, done) {
        pendingWebKey = vk
        pendingWebKeyDone = done
        webFocusAttempts = 0
        tryWebFocus()
    }

    function tryWebFocus() {
        if (!pendingWebKey)
            return
        if (probe.webViewHasFocus(root)) {
            keyReadyTimer.start()
            return
        }
        if (webFocusAttempts >= 4) {
            const done = pendingWebKeyDone
            pendingWebKey = 0
            pendingWebKeyDone = null
            probe.record("web-focus-timeout")
            if (done)
                done(false)
            return
        }
        ++webFocusAttempts
        if (probe.activateForFocusProof(root))
            host.focusWebView()
        focusWaitTimer.start()
    }

    function sendPendingWebKey() {
        if (!pendingWebKey)
            return
        if (!probe.webViewHasFocus(root)) {
            tryWebFocus()
            return
        }
        const vk = pendingWebKey
        if (!probe.sendKey(vk, root)) {
            tryWebFocus()
            return
        }
        focusWaitTimer.stop()
        const done = pendingWebKeyDone
        pendingWebKey = 0
        pendingWebKeyDone = null
        if (done)
            done(true)
    }

    function runFocusCycle(index) {
        probe.record("focus-cycle-start", String(index))
        host.executeScript(
            "focus-cycle-" + index + "-play-prep",
            "(()=>{const v=document.querySelector('video');" +
            "if(v){v.muted=true;v.play();}" +
            "if(!window.__feriaFocusKeys){window.__feriaFocusKeys=[];" +
            "document.addEventListener('keydown',e=>" +
            "window.__feriaFocusKeys.push({key:e.key,target:e.target.tagName}),true);}" +
            "window.__feriaFocusKeys=[];" +
            "const p=document.querySelector('#movie_player');" +
            "if(p){p.setAttribute('tabindex','0');p.focus();}" +
            "return {videoFound:!!v,paused:v?v.paused:null," +
            "activeTag:document.activeElement?document.activeElement.tagName:null," +
            "activeId:document.activeElement?document.activeElement.id:null};})()")
        later(1200, function() {
            requestWebKey(75, function(keySent) {
                if (!keySent) {
                    probe.record("focus-cycle-input-blocked", String(index))
                    finishSoon()
                    return
                }
                later(900, function() {
                    host.executeScript(
                        "focus-cycle-" + index + "-after-k",
                        "(()=>{const v=document.querySelector('video');" +
                        "return {videoFound:!!v,paused:v?v.paused:null," +
                        "activeTag:document.activeElement?document.activeElement.tagName:null," +
                        "keys:window.__feriaFocusKeys||[]};})()")
                    later(400, function() {
                        requestWebKey(27, function(escapeSent) {
                            if (!escapeSent) {
                                probe.record("focus-cycle-escape-blocked", String(index))
                                finishSoon()
                                return
                            }
                            later(850, function() {
                            host.executeScript(
                                "focus-cycle-" + index + "-after-escape",
                                "(()=>{const v=document.querySelector('video');" +
                                "return v?{found:true,paused:v.paused,currentTime:v.currentTime}:" +
                                "{found:false};})()")
                            recordGeometry("focus-cycle-" + index + "-after-escape")
                            if (index === 3) {
                                capture("focus-cycle-3-after-escape", false)
                                finishSoon()
                            } else {
                                later(700, function() { runFocusCycle(index + 1) })
                            }
                            })
                        })
                    })
                })
            })
        })
    }

    function runProfileWrite() {
        host.executeScript(
            "profile-write",
            "(()=>{localStorage.setItem('colosseumFeriaA1'," +
            JSON.stringify(probe.marker) +
            ");return localStorage.getItem('colosseumFeriaA1');})()")
        later(1200, function() {
            host.executeScript(
                "profile-write-read",
                "localStorage.getItem('colosseumFeriaA1')")
            capture("profile-write", false)
            finishSoon()
        })
    }

    function runProfileRead() {
        const script = probe.automation === "profile-fresh"
            ? "(()=>{const text=(document.body&&document.body.innerText)||'';" +
              "return {marker:localStorage.getItem('colosseumFeriaA1')," +
              "signInText:/sign in/i.test(text.slice(0,5000))};})()"
            : "localStorage.getItem('colosseumFeriaA1')"
        host.executeScript(probe.automation, script)
        later(1200, function() {
            capture(probe.automation, false)
            finishSoon()
        })
    }

    function runPolicy() {
        probe.beginBrowserWatch()
        host.executeScript(
            "policy-offlist-attempt",
            "location.href='https://example.com/feria-a1-off-list';'attempted'")
        later(1800, function() {
            host.executeScript(
                "policy-popup-attempt",
                "(()=>{const w=window.open(" +
                "'https://www.youtube.com/watch?v=jNQXAC9IVRw'," +
                "'feriaA1Popup','width=720,height=480');" +
                "return {requested:!!w};})()")
            later(2600, function() {
                recordGeometry("policy-main")
                capture("policy-popup-owned-final", true)
                probe.endBrowserWatch()
                finishSoon()
            })
        })
    }

    function runNetflix() {
        const sample =
            "(()=>{const v=document.querySelector('video');" +
            "const text=(document.body&&document.body.innerText)||'';" +
            "return {href:location.href,title:document.title," +
            "hasVideo:!!v,currentTime:v?v.currentTime:null," +
            "paused:v?v.paused:null,passwordInput:!!document.querySelector(" +
            "'input[type=password]'),signInText:/sign in/i.test(text.slice(0,2500))};})()"
        host.executeScript("netflix-sample-1", sample)
        capture("netflix-state-1", false)
        later(11000, function() {
            host.executeScript("netflix-sample-2", sample)
            capture("netflix-state-2", false)
            finishSoon()
        })
    }

    Timer {
        id: stepTimer
        repeat: false
        property var callback: null
        onTriggered: {
            const cb = callback
            callback = null
            if (cb)
                cb()
        }
    }

    Timer {
        id: popupCaptureTimer
        interval: 1300
        repeat: false
        onTriggered: capture("policy-popup-owned", true)
    }

    Timer {
        id: focusWaitTimer
        interval: 500
        repeat: false
        onTriggered: tryWebFocus()
    }

    Timer {
        id: keyReadyTimer
        interval: 80
        repeat: false
        onTriggered: sendPendingWebKey()
    }

    Rectangle {
        id: header
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 58
        color: "#202020"

        Text {
            anchors.left: parent.left
            anchors.leftMargin: 18
            anchors.verticalCenter: parent.verticalCenter
            color: "#f2f2f2"
            font.pixelSize: 18
            text: "Feria A1 · native WebView2 inside QQuickWindow"
        }

        Text {
            anchors.right: parent.right
            anchors.rightMargin: 18
            anchors.verticalCenter: parent.verticalCenter
            color: "#bbbbbb"
            font.pixelSize: 12
            text: "PID " + probe.pid + " · " + probe.scaleLabel + "%"
        }
    }

    Rectangle {
        id: placeholder
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: header.bottom
        anchors.bottom: controls.top
        anchors.margins: 22
        color: "#2a1836"
        border.color: "#8a5ca8"
        border.width: 1

        Text {
            anchors.centerIn: parent
            color: "#d9b7ef"
            text: host.suppressed
                  ? "Native host hidden for QML overlay"
                  : "WebView2 child HWND placeholder"
        }

        FeriaHostItem {
            id: host
            anchors.fill: parent
            userDataFolder: probe.profilePath
            url: probe.initialUrl
            focus: true

            onNavigationBlocked: function(uri) {
                lastStatus = "blocked " + uri
                probe.record("navigation-blocked", uri)
            }
            onNavigationCompleted: function(uri, success) {
                lastStatus = (success ? "loaded " : "failed ") + uri
                probe.record("navigation-completed",
                             "success=" + success + " uri=" + uri)
                if (!automationStarted && success) {
                    if (probe.automation === "a2-dwell") {
                        host.executeScript("a2-dwell-early-pause",
                            "(()=>{setInterval(()=>{" +
                            "const v=document.querySelector('video');" +
                            "if(v&&!v.paused)v.pause();},50);" +
                            "return 'pause-armed';})()")
                    }
                    if (probe.automation !== "a2-watch"
                            && probe.automation !== "a2-complete"
                            && probe.automation !== "a2-resume") {
                        later(probe.automation.startsWith("a2-") ? 1500 : 4200,
                              function() { startAutomation() })
                    }
                }
            }
            onObserverMessage: function(jsonMessage) {
                probe.observe(jsonMessage)
                if (!automationStarted && (probe.automation === "a2-watch"
                                           || probe.automation === "a2-complete"
                                           || probe.automation === "a2-resume")) {
                    const sample = JSON.parse(jsonMessage)
                    const media = sample.media
                    if (probe.automation === "a2-resume"
                            && (sample.adShowing || sample.adInterrupting)) {
                        a2ResumeSawAd = true
                        a2ContentSamples = 0
                    }
                    if (sample.href.indexOf("/watch?v=") >= 0 && media
                            && !sample.adShowing
                            && !sample.adInterrupting && sample.title) {
                        if (probe.automation === "a2-resume") {
                            ++a2ContentSamples
                            if (media.currentTime > 0 && media.duration > 0
                                    && a2ContentSamples >= (a2ResumeSawAd ? 2 : 8))
                                startAutomation()
                            return
                        }
                        ++a2ContentSamples
                        if (a2ContentSamples >= 3)
                            startAutomation()
                    } else {
                        a2ContentSamples = 0
                    }
                }
            }
            onScriptResult: function(label, jsonResult) {
                probe.record("script-result",
                             "label=" + label + " result=" + jsonResult)
            }
            onPopupOpened: function(uri) {
                probe.record("popup-opened", uri)
                popupCaptureTimer.start()
            }
            onHostLog: function(message) {
                probe.record("host-log", message)
                if (message === "WEBVIEW_GOT_FOCUS" && pendingWebKey) {
                    focusWaitTimer.stop()
                    keyReadyTimer.start()
                }
            }
            onReturnedToQml: {
                probe.record("returned-to-qml")
            }
            onQmlFocusRestored: function(activeFocus) {
                probe.record("qml-focus-restored",
                             "activeFocus=" + activeFocus)
            }
            onGeometryCaptured: function(json) {
                probe.record("geometry-signal", json)
            }
        }

        Rectangle {
            id: overlay
            anchors.fill: parent
            visible: overlayOpen
            color: "#e61f1f1f"
            z: 10

            Rectangle {
                anchors.centerIn: parent
                width: 460
                height: 130
                radius: 10
                color: "#f4f0f7"
                border.color: "#4b285c"

                Text {
                    anchors.centerIn: parent
                    color: "#211526"
                    font.pixelSize: 22
                    text: "QML overlay owns this airspace"
                }
            }
        }
    }

    Rectangle {
        id: controls
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 72
        color: "#202020"

        Row {
            anchors.centerIn: parent
            spacing: 10

            Repeater {
                model: [
                    { label: "Overlay", action: "overlay" },
                    { label: "Web focus", action: "focus" },
                    { label: "Fullscreen", action: "fullscreen" },
                    { label: "Off-list", action: "offlist" }
                ]

                Rectangle {
                    required property var modelData
                    width: 112
                    height: 38
                    radius: 5
                    color: mouse.containsMouse ? "#4b3658" : "#362842"

                    Text {
                        anchors.centerIn: parent
                        color: "#ffffff"
                        text: modelData.label
                    }

                    MouseArea {
                        id: mouse
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: {
                            if (modelData.action === "overlay") {
                                host.suppressed = !host.suppressed
                                overlayOpen = host.suppressed
                            } else if (modelData.action === "focus") {
                                host.focusWebView()
                            } else if (modelData.action === "fullscreen") {
                                root.visibility === Window.FullScreen
                                    ? root.showNormal() : root.showFullScreen()
                            } else if (modelData.action === "offlist") {
                                host.navigate("https://example.com/")
                            }
                        }
                    }
                }
            }

            Text {
                width: 300
                elide: Text.ElideRight
                color: "#bbbbbb"
                verticalAlignment: Text.AlignVCenter
                text: lastStatus
            }
        }
    }
}
