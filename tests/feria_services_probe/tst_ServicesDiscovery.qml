import QtQuick
import QtQuick.Controls
import QtTest
import QtWebEngine
import "../../qml/feria" as Feria

// Set 5 discovery only: real production deck/wrapper, loopback-only pages,
// silent media, no OS notification presenter, disposable browser profiles.
Item {
    id: root
    width: 1060; height: 760
    property var capturedNotice: null
    property int noticeCount: 0
    property var lastSample: null
    property int sampleCount: 0
    property bool captureMedia: false
    Item { id: embedded; x: 25; y: 125; width: 820; height: 580 }
    Feria.FeriaSessionDeck {
        id: deck
        parent: embedded
        anchors.fill: parent
        sessions: Sessions; accountStore: FeriaAccount; policy: FeriaBrowserPolicy
        displayed: true
    }
    Connections {
        target: deck.sharedProfile
        ignoreUnknownSignals: true
        function onPresentNotification(notification) {
            root.capturedNotice = notification
            ++root.noticeCount
            console.log("SERVICES_NOTIFICATION_SIGNAL title=" + notification.title
                + " message=" + notification.message
                + " origin=" + notification.origin
                + " iconProperty=" + typeof notification.icon
                + " show=" + typeof notification.show
                + " click=" + typeof notification.click
                + " close=" + typeof notification.close)
        }
    }
    Connections {
        target: deck.activeBrowser
        ignoreUnknownSignals: true
        function onPlaybackObserved(sample) {
            if (!root.captureMedia) return
            root.lastSample = sample
            ++root.sampleCount
            console.log("SERVICES_MEDIA_SAMPLE " + JSON.stringify(sample))
        }
    }
    TestCase {
        id: tests
        name: "FeriaServicesDiscovery"
        when: windowShown
        property int serial: 0
        function init() {
            failOnWarning(/TypeError|ReferenceError|incompatible arguments|Could not convert argument/)
            deck.startQuiescence(false)
            tryCompare(deck, "quiescing", false, 15000)
            // The first case alone can be repeated in a second process with
            // FERIA_FIXTURE_ROOT for a genuine persistent-profile restart check.
            ++serial
            Fixture.switchProfile(serial === 1 ? "services-restart" : "services-case-" + serial)
            tryCompare(deck, "quiescing", false, 15000)
            deck.displayed = true
            deck.suppressed = false
            root.capturedNotice = null; root.noticeCount = 0
            root.lastSample = null; root.sampleCount = 0; root.captureMedia = false
        }
        function cleanup() {
            root.capturedNotice = null
            deck.startQuiescence(false)
            tryCompare(deck, "quiescing", false, 15000)
            compare(FeriaBrowserPolicy.runtimeViewCount, 0)
            compare(FeriaBrowserPolicy.runtimeProfileCount, 0)
        }
        function descriptor(path) {
            return { providerId:"mangaplus", providerName:"Services fixture",
                url:FixtureUrl + path, startUrl:FixtureUrl + "/a", mode:"listen" }
        }
        function settled(page, path) {
            var stable = -1, drained = false
            tryVerify(function() {
                var ready = page && page.documentReady && !page.loading
                    && !page.replacementLoadPending
                    && String(page.url) === FixtureUrl + path
                if (!ready) { stable = -1; drained = false; return false }
                if (stable !== page.navigationGeneration) {
                    stable = page.navigationGeneration; drained = false
                    var generation = stable
                    Qt.callLater(function() { if (stable === generation) drained = true })
                    return false
                }
                return drained
            }, 15000, "Settled real browser page " + path)
        }
        function open(path) {
            var id = deck.open(descriptor(path), true, false, true)
            verify(id.length > 0)
            tryVerify(function() { return deck.sharedProfile !== null }, 10000)
            deck.sharedProfile.downloadPath = Fixture.downloadRoot
            tryVerify(function() { return !!deck.browserForSession(id) }, 10000)
            settled(deck.browserForSession(id), path)
            return id
        }
        function js(page, source) {
            var done = false, result
            verify(page.evaluateJavaScript(source, function(value) { result = value; done = true }))
            tryVerify(function() { return done }, 5000)
            return result
        }
        function start(page, source) {
            verify(page.evaluateJavaScript("setTimeout(function(){" + source + "},0);true", function() {}))
        }
        function titled(page, value) {
            tryVerify(function() { return page.title === value }, 10000,
                "Expected " + value + ", observed " + page.title)
        }
        function test_01_loginCookieClosingAndReopening() {
            var id = open("/services-login"), page = deck.browserForSession(id)
            var initiallySignedIn = page.title === "services:signed-in"
            console.log("SERVICES_COOKIE_BEGIN restartStartSignedIn=" + initiallySignedIn
                + " profileOffRecord=" + deck.sharedProfile.offTheRecord
                + " policy=" + deck.sharedProfile.persistentCookiesPolicy)
            if (!initiallySignedIn) {
                start(page, "document.getElementById('username').value='reader';"
                    + "document.getElementById('password').value='fixture-pass';"
                    + "document.getElementById('login').requestSubmit()")
                titled(page, "services:signed-in")
            }
            verify(deck.closeTab(id), "close real signed-in tab")
            tryVerify(function() { return !deck.browserForSession(id) }, 10000)
            var reopened = open("/services-login")
            titled(deck.browserForSession(reopened), "services:signed-in")
            console.log("SERVICES_COOKIE_REOPEN closedTab=true signedIn=true"
                + " restartStartSignedIn=" + initiallySignedIn)
        }
        function test_02_passwordAndFormSurface() {
            var id = open("/services-login"), page = deck.browserForSession(id)
            var settingNames = Object.keys(page.settings).filter(function(s) {
                return /auto.?fill|password|hardware.?media/i.test(s)
            })
            console.log("SERVICES_PASSWORD_SETTINGS properties=" + settingNames.join(",")
                + " autoFillPasswords=" + typeof page.settings.autoFillPasswords
                + " passwordManager=" + typeof deck.sharedProfile.passwordManager)
            console.log("SERVICES_PASSWORD_FORM inputType="
                + js(page, "document.querySelector('#password').type")
                + " initialPassword=" + js(page, "document.querySelector('#password').value.length"))
            // The stock engine's save/fill popup is not surfaced through any
            // production wrapper callback. Observation is deliberately reported,
            // not turned into an unsupported absence assertion.
            var formId = open("/services-autofill")
            var formPage = deck.browserForSession(formId)
            console.log("SERVICES_AUTOFILL_INITIAL value="
                + JSON.stringify(js(formPage, "document.querySelector('#topic').value")))
            start(formPage, "var f=document.querySelector('form');"
                + "document.querySelector('#topic').value='fixture-topic';f.requestSubmit()")
            tryVerify(function() { return String(formPage.url).indexOf("?topic=fixture-topic") >= 0
                && formPage.documentReady && !formPage.loading }, 10000)
            verify(deck.navigateSession(formId, FixtureUrl + "/services-autofill", 0, null, null))
            settled(formPage, "/services-autofill")
            console.log("SERVICES_AUTOFILL_REVISIT value="
                + JSON.stringify(js(formPage, "document.querySelector('#topic').value"))
                + " suggestionPopup=not-instrumented")
        }
        function test_03_crossHostSubresource() {
            var id = open("/services-resource-page"), page = deck.browserForSession(id)
            tryVerify(function() { return page.title !== "services:pending" }, 10000)
            console.log("SERVICES_FILTER title=" + page.title
                + " pageHost=127.0.0.1 subresourceHost=localhost"
                + " profileInterceptorProperty=" + typeof deck.sharedProfile.urlRequestInterceptor)
        }
        function test_04_notificationDelivery() {
            var id = open("/a"), page = deck.browserForSession(id)
            console.log("SERVICES_NOTIFICATION_PERMISSION_INITIAL " + js(page, "Notification.permission"))
            start(page, "Notification.requestPermission().then(function(value){document.title='services:permission:'+value})")
            tryVerify(function() {
                return page.browserRequests.current !== null
                    && page.browserRequests.current.kind === "permission"
            }, 10000)
            var accept = findChild(page.browserRequests, "feriaRequestAccept")
            verify(accept !== null && accept.visible)
            mouseClick(accept)
            titled(page, "services:permission:granted")
            js(page, "window.__noticeEvents={show:0,click:0,close:0};true")
            start(page, "var n=new Notification('Fixture notice',{body:'Fixture body'});"
                + "n.onshow=function(){window.__noticeEvents.show++};"
                + "n.onclick=function(){window.__noticeEvents.click++};"
                + "n.onclose=function(){window.__noticeEvents.close++}")
            wait(1000)
            console.log("SERVICES_NOTIFICATION_DELIVERY count=" + root.noticeCount
                + " pageEventsBefore=" + JSON.stringify(js(page, "window.__noticeEvents")))
            if (root.capturedNotice) {
                // QML engine methods dispatch page events. No Windows toast or
                // tray surface is instantiated by this test.
                root.capturedNotice.show()
                wait(100)
                console.log("SERVICES_NOTIFICATION_SHOW "
                    + JSON.stringify(js(page, "window.__noticeEvents")))
                root.capturedNotice.click()
                root.capturedNotice.close()
                wait(100)
                console.log("SERVICES_NOTIFICATION_CLICK_CLOSE "
                    + JSON.stringify(js(page, "window.__noticeEvents")))
            }
        }
        function test_05_mediaPageObservation() {
            var id = open("/services-media"), page = deck.browserForSession(id)
            root.captureMedia = true
            page.audioMuted = true // tests never play through speakers
            start(page, "document.getElementById('serviceAudio').play().then(function(){"
                + "navigator.mediaSession.playbackState='playing'}).catch(function(e){"
                + "window.serviceMediaError=String(e)})")
            wait(700)
            console.log("SERVICES_MEDIA_PAGE " + JSON.stringify(js(page,
                "({supported:!!navigator.mediaSession,playing:!document.getElementById('serviceAudio').paused,"
                + "position:document.getElementById('serviceAudio').currentTime,"
                + "metadata:navigator.mediaSession&&navigator.mediaSession.metadata&&navigator.mediaSession.metadata.title,"
                + "actions:window.serviceActions,error:window.serviceMediaError||''})")))
            page.samplePlayback()
            wait(1200)
            console.log("SERVICES_MEDIA_GATE sampleCount=" + root.sampleCount
                + " title=" + (root.lastSample ? root.lastSample.title : "none")
                + " position=" + (root.lastSample ? root.lastSample.position : "none")
                + " playCommand=" + typeof page.play
                + " nextCommand=" + typeof page.nextTrack
                + " playingState=" + page.recentlyAudible)
            console.log("SERVICES_MEDIA_ACTIONS_AFTER_POLL "
                + JSON.stringify(js(page, "window.serviceActions")))
        }
        function test_06_threeTabLifecycleAndRenderers() {
            var soundId = open("/services-media")
            var sound = deck.browserForSession(soundId)
            sound.audioMuted = true
            sound.freezeDelay = 180
            start(sound, "document.getElementById('serviceAudio').play().catch(function(e){"
                + "window.serviceMediaError=String(e)})")
            wait(450)
            var playing = js(sound, "!document.getElementById('serviceAudio').paused")
            var idleId = open("/a"), idle = deck.browserForSession(idleId)
            idle.freezeDelay = 180
            var activeId = open("/b"), active = deck.browserForSession(activeId)
            wait(1200)
            console.log("SERVICES_LIFECYCLE_THREE audioPlayingBefore=" + playing
                + " audioMuted=" + sound.audioMuted + " recentAudio=" + sound.recentlyAudible
                + " audioLifecycle=" + sound.lifecycleState
                + " idleLifecycle=" + idle.lifecycleState
                + " activeLifecycle=" + active.lifecycleState
                + " audioRecommended=" + sound.recommendedState
                + " idleRecommended=" + idle.recommendedState
                + " defaultDelayMs=30000 acceleratedDelayMs=180"
                + " audioPid=" + sound.renderProcessPid
                + " idlePid=" + idle.renderProcessPid
                + " activePid=" + active.renderProcessPid)
            verify(deck.activate(idleId))
            tryCompare(idle, "lifecycleState", WebEngineView.LifecycleState.Active, 10000)
            console.log("SERVICES_LIFECYCLE_WAKE idleLifecycle=" + idle.lifecycleState
                + " visible=" + idle.visible + " previousActiveVisible=" + active.visible)
        }
    }
}
