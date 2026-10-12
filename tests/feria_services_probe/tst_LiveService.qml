import QtQuick
import QtTest
import QtWebEngine
import "../../qml/feria" as Feria

// Opt-in network acceptance check; never runs with a personal browser profile.
Item {
    width: 1060; height: 760
    Feria.FeriaSessionDeck {
        id: deck
        anchors.fill: parent
        sessions: Sessions; accountStore: FeriaAccount; policy: FeriaBrowserPolicy
        displayed: true
    }
    TestCase {
        name: "FeriaLiveService"
        when: windowShown
        function cleanup() {
            var closing = deck.activeBrowser
            deck.startQuiescence(false)
            wait(600)
            console.log("SERVICES_LIVE_CLOSE " + JSON.stringify({quiescing:deck.quiescing,
                scripts:closing ? Object.keys(closing.javascriptTickets || {}) : [],
                views:FeriaBrowserPolicy.runtimeViewCount,profiles:FeriaBrowserPolicy.runtimeProfileCount}))
            tryCompare(deck, "quiescing", false, 15000)
            compare(FeriaBrowserPolicy.runtimeViewCount, 0)
            compare(FeriaBrowserPolicy.runtimeProfileCount, 0)
        }
        function js(page, source) {
            var done = false, result
            verify(page.evaluateJavaScript(source, function(value) { result = value; done = true }))
            tryVerify(function() { return done }, 10000)
            return result
        }
        function test_publicPlayback() {
            if (!Fixture.serviceUrl.length) skip("Set FERIA_SERVICE_URL to opt into a public-service playback check")
            verify(/^https:\/\//.test(Fixture.serviceUrl))
            Fixture.switchProfile("live-service")
            var id = deck.open({providerId: "youtube", providerName: "Public service check",
                url: Fixture.serviceUrl, mode: "watch"}, true, false, true)
            verify(id.length > 0)
            tryVerify(function() { return !!deck.browserForSession(id) }, 10000)
            var page = deck.browserForSession(id)
            page.audioMuted = true
            tryVerify(function() { return page.documentReady && !page.loading }, 60000)
            console.log("SERVICES_LIVE_PAGE " + JSON.stringify(js(page,
                "({title:document.title,host:location.host,path:location.pathname,"
                + "videos:document.querySelectorAll('video').length,"
                + "body:document.body?document.body.innerText.slice(0,400):''})")))
            tryVerify(function() { return js(page, "!!document.querySelector('video')") }, 20000)
            js(page, "window.__feriaPlayError='';document.querySelector('video').play()"
                + ".catch(function(e){window.__feriaPlayError=String(e)});true")
            tryVerify(function() {
                return js(page, "(function(){var v=document.querySelector('video');"
                    + "return v&&!v.paused&&v.readyState>=3&&v.currentTime>0})()")
            }, 30000, "The real service must decode and advance its video")
            var before = js(page, "document.querySelector('video').currentTime")
            wait(1200)
            verify(js(page, "document.querySelector('video').currentTime") > before)
            js(page, "document.querySelector('video').pause();true")
            compare(js(page, "document.querySelector('video').paused"), true)
            console.log("SERVICES_LIVE_PLAYBACK decoded=true advanced=true paused=true")
        }
    }
}
