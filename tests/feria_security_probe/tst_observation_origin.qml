import QtQuick
import QtTest
import "../../qml/feria" as Feria

Item {
    id: stage
    width: 900; height: 700
    Component {
        id: browserFactory
        Feria.FeriaQtWebEngine {
            width: 900; height: 700
            sourceUrl: FixtureUrl + "/services-media"
            profilePath: Fixture.evidenceRoot + "/observation-origin-profile"
            appLayout: false; appKeyboard: false; observationEnabled: false
        }
    }
    TestCase {
        name: "FeriaObservationOrigin"
        when: windowShown
        property var browser: null
        property var samples: []
        property bool captureBlocked: false
        function js(source) {
            var done = false, value
            verify(browser.evaluateJavaScript(source, function(result) { value = result; done = true }))
            tryVerify(function() { return done }, 5000)
            return value
        }
        function init() {
            failOnWarning(/TypeError|ReferenceError|Cannot anchor/)
            verify(Fixture.evidenceRoot.length > 0)
            Fixture.switchProfile("observation-origin")
            browser = browserFactory.createObject(stage)
            verify(browser !== null)
            tryCompare(browser, "documentReady", true, 15000)
            browser.playbackObserved.connect(function(sample) { samples.push(sample) })
            browser.captureProtectionChanged.connect(function(blocked) { captureBlocked = blocked })
            samples = []; captureBlocked = false
        }
        function cleanup() {
            if (browser) {
                browser.observationEnabled = false
                browser.prepareForClose(false)
                tryCompare(browser, "pendingJavaScript", 0, 10000)
                tryCompare(browser, "destructionNotified", true, 10000)
                browser.destroy(); browser = null
            }
            tryCompare(FeriaBrowserPolicy, "runtimeViewCount", 0, 10000)
            tryCompare(FeriaBrowserPolicy, "runtimeProfileCount", 0, 10000)
        }
        function poll() {
            var gate = findChild(browser, "feriaMediaSession")
            verify(gate !== null)
            browser.observationEnabled = true
            browser.samplePlayback()
            tryCompare(gate, "pendingLabel", "", 5000)
            browser.observationEnabled = false
            tryCompare(browser, "pendingJavaScript", 0, 5000)
        }
        function test_pageCannotAttributePlaybackToAnotherOrigin() {
            // Positive control: the real production probe emits the real page URL.
            poll()
            verify(samples.length > 0)
            compare(samples[samples.length - 1].href, String(browser.url))
            samples = []
            // A MainWorld page can intercept this otherwise ordinary probe assignment.
            // The trust decision must live outside that world, after script completion.
            compare(js("(()=>{window.forgedSamples=0;Object.defineProperty(Object.prototype,'mediaGeneration',{configurable:true,set:function(v){window.forgedSamples++;this.href='https://other.example/forged';Object.defineProperty(this,'mediaGeneration',{value:v,writable:true,configurable:true,enumerable:true})}});return true})()"), true)
            poll()
            verify(js("window.forgedSamples") > 0, "The actual production probe must encounter the hostile setter")
            compare(samples.length, 0, "Reject a page-supplied observation URL from another origin")
            compare(captureBlocked, true)
            compare(js("delete Object.prototype.mediaGeneration"), true)
            poll()
            verify(samples.length > 0, "A later valid observation must recover normally")
            compare(samples[samples.length - 1].href, String(browser.url))
            compare(captureBlocked, false)
            samples = []
            var canonical = FixtureUrl + "/watch?item=canonical"
            compare(js("(()=>{Object.defineProperty(Object.prototype,'mediaGeneration',{configurable:true,set:function(v){this.href="
                + JSON.stringify(canonical) + ";Object.defineProperty(this,'mediaGeneration',{value:v,writable:true,configurable:true,enumerable:true})}});return true})()"), true)
            poll()
            verify(samples.length > 0, "Same-origin canonical routes must remain usable")
            compare(samples[samples.length - 1].href, canonical)
            compare(captureBlocked, false)
            compare(js("delete Object.prototype.mediaGeneration"), true)
        }
    }
}
