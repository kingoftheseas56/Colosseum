import QtQuick
import QtTest
import QtWebEngine
import "../../qml/feria" as Feria
import "../../qml/feria/FeriaJavaScript.js" as JavaScript

Item {
    width: 1060; height: 760
    Feria.FeriaSessionDeck {
        id: deck
        anchors.fill: parent
        sessions: Sessions; accountStore: FeriaAccount; policy: FeriaBrowserPolicy
        displayed: true
    }
    TestCase {
        name: "FeriaResourceManagement"
        when: windowShown
        property int serial: 0
        function init() {
            failOnWarning(/TypeError|ReferenceError|incompatible arguments|Could not convert argument/)
            closeAll()
            Fixture.switchProfile("resource-management-" + (++serial))
            tryCompare(deck, "quiescing", false, 15000)
        }
        function closeAll() {
            deck.startQuiescence(false)
            tryCompare(deck, "quiescing", false, 15000)
            compare(FeriaBrowserPolicy.runtimeViewCount, 0)
            compare(FeriaBrowserPolicy.runtimeProfileCount, 0)
        }
        function cleanup() { closeAll() }
        function open(path, privateMode) {
            var id = deck.open({providerId: "ytmusic", providerName: "Resource fixture",
                url: FixtureUrl + path, mode: "listen", privateBrowsing: !!privateMode}, true, false, true)
            verify(id.length > 0)
            tryVerify(function() { return !!deck.browserForSession(id) }, 10000)
            var page = deck.browserForSession(id)
            tryVerify(function() { return page.documentReady && !page.loading }, 15000)
            return {id: id, page: page}
        }
        function js(page, source) {
            var done = false, result
            verify(page.evaluateJavaScript(source, function(value) { result = value; done = true }))
            tryVerify(function() { return done }, 5000)
            return result
        }
        function test_backgroundPreferenceAndPlayingAudio() {
            var idle = open("/a", false)
            idle.page.freezeDelay = 250
            idle.page.freezeBackground = false
            var other = open("/b", false)
            wait(800)
            compare(idle.page.lifecycleState, WebEngineView.LifecycleState.Active)
            idle.page.freezeBackground = true
            tryCompare(idle.page, "lifecycleState", WebEngineView.LifecycleState.Frozen, 5000)
            verify(deck.activate(idle.id))
            tryCompare(idle.page, "lifecycleState", WebEngineView.LifecycleState.Active, 5000)
            var sound = open("/services-media", false)
            sound.page.audioMuted = true
            sound.page.freezeDelay = 250
            js(sound.page, "document.getElementById('serviceAudio').play();true")
            tryCompare(sound.page, "recentlyAudible", true, 3000)
            verify(deck.activate(other.id))
            wait(800)
            compare(sound.page.lifecycleState, WebEngineView.LifecycleState.Active)
            compare(js(sound.page, "document.getElementById('serviceAudio').paused"), false)
            js(sound.page, "document.getElementById('serviceAudio').pause();true")
            console.log("SERVICES_RESOURCE_PAUSED " + JSON.stringify({audible:sound.page.recentlyAudible,
                recommended:sound.page.recommendedState,lifecycle:sound.page.lifecycleState}))
            console.log("SERVICES_RESOURCE_POLICY disabled=active idle=frozen foreground=active playing=active")
        }
        function test_defaultThirtySecondDelay() {
            var idle = open("/a", false)
            compare(idle.page.freezeDelay, 30000)
            var started = Date.now()
            open("/b", false)
            wait(1000)
            compare(idle.page.lifecycleState, WebEngineView.LifecycleState.Active)
            console.log("SERVICES_RESOURCE_IDLE " + JSON.stringify({visible:idle.page.visible,
                audible:idle.page.recentlyAudible,recommended:idle.page.recommendedState,
                freeze:idle.page.freezeBackground,blocked:idle.page.lifecycleBlocked,
                pending:idle.page.pendingJavaScript}))
            tryCompare(idle.page, "lifecycleState", WebEngineView.LifecycleState.Frozen, 35000)
            console.log("SERVICES_RESOURCE_DEFAULT_FREEZE elapsedMs=" + (Date.now() - started))
            verify(deck.activate(idle.id))
            tryCompare(idle.page, "lifecycleState", WebEngineView.LifecycleState.Active, 5000)
        }
        function test_repeatedMixedTabCloseWithPendingScripts() {
            for (var cycle = 0; cycle < 4; ++cycle) {
                var pages = [], callbacks = 0
                for (var tab = 0; tab < 8; ++tab) {
                    var item = open(tab % 2 ? "/a" : "/b", tab >= 4)
                    compare(item.page.profile.offTheRecord, tab >= 4)
                    pages.push(item.page)
                }
                compare(FeriaBrowserPolicy.runtimeViewCount, 8)
                compare(FeriaBrowserPolicy.runtimeProfileCount, 2)
                for (var page of pages)
                    verify(page.evaluateJavaScript("var until=Date.now()+80;while(Date.now()<until){};true",
                        function() { ++callbacks }))
                verify(pages.some(function(page) { return page.pendingJavaScript > 0 }))
                closeAll()
                compare(callbacks, 0, "Retired work must not call the page's consumer")
                console.log("SERVICES_RESOURCE_CLOSE cycle=" + cycle + " tabs=8 views=0 profiles=0 callbacks=0")
            }
        }
        function test_strandedScriptRetiresAtDiscardedBoundary() {
            var page = open("/a", false).page, callbacks = 0
            // Fault injection for the public-page close failure: retain one
            // admitted callback whose engine response has not arrived. Qt may
            // return it only when its web contents are destroyed.
            var work = FeriaBrowserPolicy.runtime.admitWork(page, "script")
            verify(work > 0)
            var ticket = JavaScript.ticket(page, ++page.javascriptTicketSerial, work,
                function() { ++callbacks })
            var tickets = Object.assign({}, page.javascriptTickets)
            tickets[ticket.id] = ticket
            page.javascriptTickets = tickets
            var late = JavaScript.resultCallback(ticket)
            try {
                closeAll()
                compare(callbacks, 0)
                late(true)
                compare(callbacks, 0, "A late engine callback must not touch the retired view")
            } finally {
                // Keep the failing baseline isolated, rather than strand its
                // disposable profile for subsequent tests.
                if (page && page.closingSession && !page.destructionNotified) {
                    page.discardForExit()
                    page.notifySafeToDestroy()
                }
            }
        }
    }
}
