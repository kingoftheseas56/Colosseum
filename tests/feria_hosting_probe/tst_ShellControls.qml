import QtQuick
import QtQuick.Controls
import QtTest
import "../../qml/feria" as Feria

Item {
    id: root
    width: 1100; height: 650
    Rectangle { id: skin; anchors.fill: parent; color: "#202936" }
    Item { id: fullSize; anchors.fill: parent; visible: false }
    Feria.FeriaSessionDeck {
        id: deck
        sessions: Sessions; accountStore: FeriaAccount; policy: FeriaBrowserPolicy
        displayed: true; fullSizeSurface: fullSize
        x: 25; y: 180; width: 1050; height: 445
        z: 2
    }
    QtObject {
        id: fixtureController
        property var accountStore: FeriaAccount
        property var browserDeck: deck
        property bool managedBrowserRoute: true
        property string hostSessionId: Sessions.activeId
        property string hostApp: "mangaplus"
        property string hostMode: "read"
        property string hostTitle: "Fixture"
        property string hostUrl: FixtureUrl + "/hosting"
        property var hostResumeLocator: null
        property real hostResumePosition: 0
        property bool browserSuppressed: false
        property bool lifecycleActive: true
        property bool recording: false
        property bool providerWebViewReady: false
        property var continueItems: []
        property int navigationEpoch: 0
        property var backdrop: skin
        property color gold: "#efc94a"
        property color ink: "#ffffff"
        property color mist: "#acb6c4"
        property color slate: "#acb6c4"
        property string uiFont: "Segoe UI"
        property real unit: 16
        property real marginX: 16
        property string toast: ""
        function providerName(id) { return "Fixture" }
        function providerMode(id) { return "read" }
        function isReadingProvider(id) { return true }
        function setToast(text) { toast = text }
        function beginHostVisit() {}
        function openHost() {}
        function back() {}
        function browserParkRequested() {}
        function browserCloseRequested(id) { deck.closeTab(id) }
    }
    Loader {
        id: chromeLoader
        x: 25; y: 128; width: 1050; height: 497
        active: false
        sourceComponent: Feria.PorticoDiscoveryHostView { controller: fixtureController }
    }
    TestCase {
        name: "FeriaShellControls"
        when: windowShown
        function init() {
            failOnWarning(/TypeError|ReferenceError|Cannot assign|Cannot anchor|not installed/)
            root.Window.window.width = root.width
            root.Window.window.height = root.height
            root.Window.window.requestActivate()
            tryVerify(function() { return Fixture.focusWindowMatches(root.Window.window) })
            Fixture.switchProfile("shell-controls")
            tryCompare(deck, "quiescing", false, 15000)
            chromeLoader.active = true
            tryCompare(chromeLoader, "status", Loader.Ready)
            waitForRendering(chromeLoader.item)
        }
        function cleanup() {
            chromeLoader.active = false
            deck.startQuiescence(false)
            tryCompare(deck, "quiescing", false, 15000)
        }
        function open() {
            var id = Sessions.openBrowser({providerId: "mangaplus", providerName: "Fixture",
                url: FixtureUrl + "/hosting", startUrl: FixtureUrl + "/home", mode: "read"}, true)
            tryVerify(function() { var p = deck.browserForSession(id)
                return p && p.documentReady && !p.loading && p.title === "Fixture /hosting" }, 15000)
            return id
        }
        function js(page, source) {
            var done = false, value
            verify(page.evaluateJavaScript(source, function(result) { value = result; done = true }))
            tryVerify(function() { return done }, 5000)
            return value
        }
        function test_realMaxButtonAndEscapePreservePageAndBounds() {
            var id = open(), page = deck.browserForSession(id)
            var bounds = Qt.size(page.width, page.height)
            js(page, "window.shellMarker=911")
            var action = findChild(chromeLoader.item, "feriaHostFullscreen")
            verify(action !== null)
            mouseClick(action, action.width / 2, action.height / 2)
            tryVerify(function() { return deck.entries[id].fullSizeHost !== null })
            compare(deck.browserForSession(id), page)
            compare(js(page, "window.shellMarker"), 911)
            page.forceActiveFocus()
            keyClick(Qt.Key_Escape)
            tryCompare(deck.entries[id], "fullSizeHost", null)
            tryCompare(page, "width", bounds.width)
            tryCompare(page, "height", bounds.height)
        }
        function test_realCloseButtonHonorsStay() {
            var id = open(), page = deck.browserForSession(id)
            var point = js(page, "(()=>{let r=document.querySelector('#guard').getBoundingClientRect();return {x:r.x+r.width/2,y:r.y+r.height/2}})()")
            mouseClick(page, point.x, point.y)
            tryVerify(function() { return js(page, "typeof window.onbeforeunload") === "function" })
            var action = findChild(chromeLoader.item, "feriaHostClose")
            verify(action !== null)
            mouseClick(action, action.width / 2, action.height / 2)
            tryVerify(function() { return deck.entries[id].closeConsent !== null })
            compare(deck.entries[id].closeConsent.kind, "close")
            verify(deck.answerClose(id, deck.entries[id].closeConsent.attempt, false))
            tryVerify(function() {
                var close = deck.entries[id].closeController
                if (close.failure.length) console.log("SHELL_STAY_FAILURE", close.failure,
                    JSON.stringify(close.identity), JSON.stringify(Sessions.browserIdentity(id)),
                    close.pending, close.retiredBarrier, close.awaitingRejection)
                return !close.busy
            }, 6000)
            compare(deck.browserForSession(id), page)
            mouseClick(action, action.width / 2, action.height / 2)
            tryVerify(function() { return deck.entries[id].closeConsent !== null })
            verify(deck.answerClose(id, deck.entries[id].closeConsent.attempt, true))
            tryVerify(function() { return !deck.entries[id] }, 10000)
        }
        function test_shortcutsOpenAppStartInCorrectProfiles() {
            var id = open(), original = deck.browserForSession(id)
            original.forceActiveFocus()
            keyClick(Qt.Key_T, Qt.ControlModifier)
            tryVerify(function() { return Sessions.browsers().length === 2 })
            var next = deck.activeBrowser
            verify(next !== original)
            tryVerify(function() { return next.documentReady && String(next.url) === FixtureUrl + "/home" }, 10000)
            compare(next.profile, original.profile)
            next.forceActiveFocus()
            keyClick(Qt.Key_N, Qt.ControlModifier | Qt.ShiftModifier)
            tryVerify(function() { return Sessions.browsers().length === 3 })
            var privatePage = deck.activeBrowser
            verify(privatePage !== next)
            tryVerify(function() { return privatePage.documentReady && String(privatePage.url) === FixtureUrl + "/home" }, 10000)
            verify(privatePage.profile.offTheRecord)
            verify(privatePage.profile !== original.profile)
        }
        function test_siteFullscreenEscapeThenAppMaxEscape() {
            var id = open(), page = deck.browserForSession(id)
            verify(deck.toggleAppMax(id))
            js(page, "document.querySelector('#dialog').onclick=function(e){e.preventDefault();document.documentElement.requestFullscreen()}")
            var point = js(page, "(()=>{let r=document.querySelector('#dialog').getBoundingClientRect();return {x:r.x+r.width/2,y:r.y+r.height/2}})()")
            page.forceActiveFocus()
            mouseClick(page, point.x, point.y)
            tryCompare(page, "fullScreenActive", true, 5000)
            keyClick(Qt.Key_Escape)
            tryCompare(page, "fullScreenActive", false, 5000)
            verify(deck.entries[id].fullSizeHost !== null,
                "Leaving website fullscreen must preserve the app's Max presentation")
            keyClick(Qt.Key_Escape)
            tryCompare(deck.entries[id], "fullSizeHost", null)
        }
    }
}
