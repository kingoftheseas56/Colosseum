import QtQuick
import QtTest
import QtWebEngine
Item {
    id: scene
    width: 960; height: 640
    WebEngineProfile { id: profile; offTheRecord: true }
    WebEngineView { id: view; anchors.fill: parent; profile: profile
        settings.playbackRequiresUserGesture: false
        url: FixtureUrl + "/media"
    }
    TestCase {
        name: "FeriaNativeFixtureSmoke"
        when: windowShown
        function evaluate(script) {
            var done = false, result
            view.runJavaScript(script, function(value) { result = value; done = true })
            tryVerify(function() { return done }, 5000)
            return result
        }
        function test_realHttpDecodedAudioVideo() {
            tryCompare(view, "loading", false, 15000)
            tryCompare(view, "title", "Fixture /media", 15000)
            evaluate("document.getElementById('playing').play();document.getElementById('video').play();true")
            tryVerify(function() { return evaluate("document.getElementById('playing').currentTime>0.1 && document.getElementById('video').currentTime>0.1") }, 15000)
            verify(Fixture.requestCount("/media.wav") > 0)
            verify(Fixture.requestCount("/media.webm") > 0)
            verify(Fixture.captureWindow(scene, "native-fixture-media-smoke.png"))
        }
    }
}
