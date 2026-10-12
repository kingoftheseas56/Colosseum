import QtQuick
import QtTest
import QtWebEngine
import "../../qml/feria" as Feria

// The Windows media-session commands behind system media controls. This is
// agent-driven OS integration evidence, not a physical keyboard-device test.
Item {
    width: 1060; height: 760
    Feria.FeriaSessionDeck {
        id: deck
        anchors.fill: parent
        sessions: Sessions; accountStore: FeriaAccount; policy: FeriaBrowserPolicy
        displayed: true
    }
    TestCase {
        name: "FeriaMediaKeys"
        when: windowShown
        function init() {
            failOnWarning(/TypeError|ReferenceError|incompatible arguments|Could not convert argument/)
            Fixture.switchProfile("media-key-check")
            deck.displayed = true; deck.suppressed = false
        }
        function cleanup() {
            deck.startQuiescence(false)
            tryCompare(deck, "quiescing", false, 15000)
            compare(FeriaBrowserPolicy.runtimeViewCount, 0)
            compare(FeriaBrowserPolicy.runtimeProfileCount, 0)
        }
        function js(page, source) {
            var done = false, result
            verify(page.evaluateJavaScript(source, function(value) { result = value; done = true }))
            tryVerify(function() { return done }, 5000)
            return result
        }
        function test_playPauseAndNextReachTheMediaSession() {
            if (Qt.platform.os !== "windows") skip("Windows media-session acceptance check")
            var id = deck.open({providerId: "ytmusic", providerName: "Media keys fixture",
                url: FixtureUrl + "/services-media", mode: "listen"}, true, false, true)
            verify(id.length > 0)
            tryVerify(function() { return !!deck.browserForSession(id) }, 10000)
            var page = deck.browserForSession(id)
            tryVerify(function() { return page.documentReady && !page.loading }, 15000)
            // A muted player may not acquire Chromium's system media session.
            // Decode a silent eight-second WAV instead; no sound reaches speakers.
            page.audioMuted = false
            page.forceActiveFocus()
            var title = "Feria media key check " + FixtureUrl
            js(page, "navigator.mediaSession.metadata.title=" + JSON.stringify(title) + ";true")
            js(page, "var audio=document.getElementById('serviceAudio');audio.src='/media-keys.wav';"
                + "audio.play();true")
            tryVerify(function() { return js(page, "!document.getElementById('serviceAudio').paused") }, 5000)
            wait(1000)
            console.log("SERVICES_WINDOWS_MEDIA_SESSION " + JSON.stringify(Fixture.systemMediaSession(title)))
            verify(Fixture.systemMediaSession(title, "pause").dispatched, "Windows Pause reaches the owned session")
            tryVerify(function() { return js(page, "window.serviceActions.pause") === 1 }, 5000)
            compare(js(page, "document.getElementById('serviceAudio').paused"), true)
            verify(Fixture.systemMediaSession(title, "play").dispatched, "Windows Play reaches the owned session")
            tryVerify(function() { return js(page, "window.serviceActions.play") === 1 }, 5000)
            compare(js(page, "document.getElementById('serviceAudio').paused"), false)
            verify(Fixture.systemMediaSession(title, "next").dispatched, "Windows Next reaches the owned session")
            tryVerify(function() { return js(page, "window.serviceActions.nexttrack") === 1 }, 5000)
            verify(Fixture.systemMediaSession(title, "previous").dispatched)
            tryVerify(function() { return js(page, "window.serviceActions.previoustrack") === 1 }, 5000)
            console.log("SERVICES_WINDOWS_MEDIA_CONTROLS pause=1 play=1 next=1 previous=1")
        }
    }
}
