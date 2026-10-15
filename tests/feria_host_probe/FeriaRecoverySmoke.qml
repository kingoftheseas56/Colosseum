// Explicit plain-host bootstrap. Mock wrapper contracts; no real provider/account claims.
import QtQuick
import QtQuick.Window
import QtTest
import "../../qml/feria"
import "../../qml/feria/FeriaRecoveryState.js" as Recovery

Window {
    id: window
    width: 1280; height: 800; visible: true
    flags: Qt.Window | Qt.FramelessWindowHint
    property bool completed: false
    property int assertions: 0
    property int switches: 0
    property int restartCount: 0
    property int noticeActions: 0

    QtObject {
        id: mockBrowser
        property bool ready: true
        property bool loadActive: false
        property real loadProgress: -1
        property bool navCanGoBack: false
        property bool navCanGoForward: false
        property int backs: 0
        property int forwards: 0
        property int reloads: 0
        property int stops: 0
        signal started(string location)
        signal completed(string location, bool success)
        signal failed(string reason)
        signal resumeUnavailable()
        signal playbackObserved(var sample)
        signal exitRequested()
        signal optionsRequested()
        function pageBack() { backs++ }
        function pageForward() { forwards++ }
        function reload() { reloads++ }
        function stopLoad() { stops++; loadActive = false; completed("https://fixture.invalid", true) }
    }
    FeriaRecoveryHostState {
        id: recovery
        active: true
        browser: mockBrowser
        engine: "webview2"
        qtAvailable: true
        webAvailable: true
        visitKey: "fixture"
        onSwitchRequested: window.switches++
        onRestartRequested: window.restartCount++
    }
    QtObject {
        id: titleController
        property int unit: 16
        property int marginX: 32
        property string selectedTitle: "fixture"
        property string region: "IN"
        property int titleActionIndex: 0
        property var activeApps: ["netflix"]
        property var saved: []
        property color dusk: "#151923"
        property color night: "#08090d"
        property color ink: "#f4f2ed"
        property color mist: "#bbbfc8"
        property color slate: "#9da4b5"
        property color gold: "#f2c94c"
        property string uiFont: "Sans Serif"
        property string displayFont: "Serif"
        property var status: ({status:"error",region:"IN",source:"JustWatch",offers:[]})
        property var fixtureOffers: [{pk:"netflix",url:"https://fixture.invalid/a",level:"t",availabilityConfirmed:true}]
        property int requests: 0
        function titleObj(id) { return {t:"Fixture title",k:"film",y:"2026",by:"Fixture",f:[],s:"No live provider accessed.",_trend:{title:"Fixture title",kind:"movie"}} }
        function offers(it) { return fixtureOffers }
        function relatedTitles(it) { return [] }
        function availabilityFor(it) { return status }
        function requestAvailability(it, force) {
            if (force !== true) throw new Error("Force refresh required")
            requests++
            status = {status:"loading",region:"IN",source:"JustWatch",offers:[]}
        }
        function artUrl(it, wide) { return "" }
        function toneFor(id) { return ["#303b50","#111722"] }
        function providerName(pk) { return pk }
        function activateTitleAction() {}
        function toggleSaved(id) {}
        function openHost(pk,id,mode,url) {}
        function openTitle(id) {}
        function shapeFor(it) { return "poster" }
    }
    PorticoDiscoveryTitleView { id: title; anchors.fill: parent; controller: titleController }
    FeriaRecoveryNotice {
        id: notice
        x: 32; y: 100; width: window.width - 64
        visible: false
        automationId: "fixtureNotice"
        message: "The saved page opened, but its saved position was not restored. Use the provider’s controls."
        actionText: "Dismiss"
        onActionRequested: window.noticeActions++
    }
    function find(item, name) {
        if (!item) return null
        if (item.objectName === name) return item
        for (var i=0;i<item.children.length;i++) {
            var result=find(item.children[i],name)
            if (result) return result
        }
        return null
    }
    TestCase {
        id: probe
        when: windowShown
        function check(ok, detail) {
            window.assertions++
            browserReporter.report("feria-recovery", !!ok, detail)
            verify(ok, detail)
        }
        function run() {
            wait(150)
            for (var engine of ["webview2","qtwebengine"]) {
                recovery.engine = engine
                recovery.bindBrowserSignals()
                recovery.resetVisit()
                mockBrowser.loadProgress = engine === "webview2" ? -1 : 42
                mockBrowser.navCanGoBack = false
                check(!recovery.navigate(true), engine + " disabled history does not navigate")
                mockBrowser.navCanGoBack = true
                var count = mockBrowser.backs
                check(recovery.navigate(true) && mockBrowser.backs === count + 1, engine + " Previous invokes once")
                check(recovery.progress === mockBrowser.loadProgress, engine + " progress is wrapper supplied")
                mockBrowser.loadActive = true
                mockBrowser.started("https://fixture.invalid")
                mockBrowser.completed("https://fixture.invalid", true)
                check(recovery.busy, engine + " DOM completion does not claim load ended")
                check(recovery.stop() && !mockBrowser.loadActive, engine + " Stop uses wrapper")
                mockBrowser.completed("https://fixture.invalid", false)
                check(recovery.error.length > 0, engine + " failure persists")
                count = mockBrowser.reloads
                check(recovery.retry() && !recovery.retry() && mockBrowser.reloads === count + 1,
                      engine + " retry invokes once while pending")
                recovery.cancel()
                mockBrowser.resumeUnavailable()
                var message = recovery.resumeMessage
                mockBrowser.completed("https://fixture.invalid", true)
                check(message.length > 0 && recovery.resumeMessage === message, engine + " resume notice persists")
                recovery.resetVisit()
                check(recovery.resumeMessage === "", engine + " new visit clears old feedback")
            }
            recovery.visitKey = "new fixture visit"
            mockBrowser.completed("https://old.invalid", false)
            mockBrowser.resumeUnavailable()
            check(!recovery.error.length && !recovery.resumeMessage.length, "Old visit signals are ignored before browser replacement")
            recovery.engine = "webview2"
            mockBrowser.failed("old engine")
            check(!recovery.error.length, "Old engine signals are ignored before browser replacement")
            recovery.bindBrowserSignals()
            recovery.resetVisit()
            var switches = window.switches
            mockBrowser.failed("fixture startup failure")
            recovery.active = false
            wait(20)
            check(window.switches === switches, "Queued fallback cannot run after departure")
            recovery.active = true
            recovery.resetVisit()
            recovery.qtAvailable = false
            mockBrowser.failed("fixture failure")
            wait(20)
            check(window.switches === switches && !recovery.switchEngine(), "Unavailable alternate engine stays disabled")
            recovery.qtAvailable = true
            recovery.resetVisit()
            mockBrowser.failed("fixture failure")
            wait(20)
            check(window.switches === switches + 1, "One-way fallback requested once")
            mockBrowser.failed("fixture repeat failure")
            wait(20)
            check(window.switches === switches + 1, "No fallback loop")

            // Actual 30-second production timer, not a shortened test replacement.
            recovery.resetVisit()
            recovery.beginStartup()
            wait(30100)
            check(recovery.slow && recovery.busy && !recovery.error.length,
                  "Thirty-second startup delay is a notice, not a fabricated failure")
            recovery.active = false
            check(!recovery.slow, "Departure clears slow notice")

            var retry = window.find(title, "feriaAvailabilityRetry")
            check(!!retry, "Production Retry action exists")
            for (var state of ["idle","error","unmatched","ready","unsupported"]) {
                titleController.status = {status:state,region:"IN",source:"JustWatch",offers:[]}
                wait(30)
                check(title.availabilityNote().length > 0, state + " has an honest outcome")
            }
            titleController.status = {status:"error",region:"IN",source:"JustWatch",offers:[]}
            wait(30)
            retry.forceActiveFocus(Qt.TabFocusReason)
            var requests = titleController.requests
            keyClick(Qt.Key_Return)
            check(titleController.requests === requests + 1, "Real Enter forces one availability retry")
            keyClick(Qt.Key_Return)
            check(titleController.requests === requests + 1, "Loading disables duplicate retry")
            titleController.status = {status:"error",region:"IN",source:"JustWatch",offers:[]}
            wait(30)
            retry.forceActiveFocus(Qt.TabFocusReason)
            keyClick(Qt.Key_Space)
            check(titleController.requests === requests + 2, "Real Space forces one availability retry")
            titleController.status = {status:"ready",region:"IN",source:"JustWatch",offers:[{}]}
            wait(30)
            var cached = title.doors.length
            titleController.status = {status:"loading",region:"IN",source:"JustWatch",offers:[]}
            titleController.fixtureOffers = []
            wait(30)
            check(title.doors.length === cached, "Known offers remain during refresh")
            titleController.region = "US"
            titleController.status = {status:"error",region:"US",source:"JustWatch",offers:[]}
            wait(30)
            check(title.doors.length === 0 && titleController.titleActionIndex === -1,
                  "Region change does not reuse another region’s cached offers/selection")

            title.visible = false
            notice.visible = true
            for (var size of [[960,640],[1280,800],[1920,1080]]) {
                window.width=size[0];window.height=size[1]
                wait(80)
                check(window.width === size[0] && window.height === size[1] && notice.width === window.width - 64 && notice.implicitHeight <= window.height - notice.y,
                      "Recovery wraps within " + size[0] + "x" + size[1])
                notice.focusAction()
                count = window.noticeActions
                keyClick(Qt.Key_Return)
                check(window.noticeActions === count + 1, "Notice Enter activates once")
                keyClick(Qt.Key_Space)
                check(window.noticeActions === count + 2, "Notice Space activates once")
                var target = window.activeFocusItem
                keyClick(Qt.Key_Tab)
                keyClick(Qt.Key_Backtab, Qt.ShiftModifier)
                check(window.activeFocusItem !== null, "Tab/Shift+Tab leave a valid focus target")
            }
            window.completed = true
        }
        function cleanup() {
            browserReporter.report("feria-recovery", window.completed && window.assertions >= 40,
                                   "All recovery phases executed; assertions=" + window.assertions)
            browserReporter.finish()
        }
    }
    Timer {
        interval: 100; running: true
        onTriggered: { window.hide(); window.show(); window.requestActivate() }
    }
    Timer {
        interval: 500; running: true
        onTriggered: { try { probe.run() } finally { probe.cleanup() } }
    }
    Timer {
        interval: 60000; running: true
        onTriggered: { browserReporter.report("feria-recovery", false, "Recovery smoke timed out"); browserReporter.finish() }
    }
}
