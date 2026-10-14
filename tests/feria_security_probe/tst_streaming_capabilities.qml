import QtQuick
import QtTest
import QtWebEngine
import "../../qml/feria" as Feria

// Capability inventory, not protected-content or provider acceptance.
Item {
    width: 900; height: 700
    Component {
        id: browserFactory
        Feria.FeriaQtWebEngine {
            width: 900; height: 700
            sourceUrl: FixtureUrl + "/a"
            profilePath: Fixture.evidenceRoot + "/streaming-capability-profile"
            appLayout: false; appKeyboard: false; observationEnabled: false
        }
    }
    TestCase {
        name: "FeriaStreamingCapabilities"
        when: windowShown
        property var browser: null
        function js(source) {
            var done = false, result
            verify(browser.evaluateJavaScript(source, function(value) { result = value; done = true }))
            tryVerify(function() { return done }, 5000)
            return result
        }
        function cleanup() {
            if (browser) {
                browser.prepareForClose(false)
                tryCompare(browser, "pendingJavaScript", 0, 10000)
                tryCompare(browser, "destructionNotified", true, 10000)
                browser.destroy(); browser = null
            }
            tryCompare(FeriaBrowserPolicy, "runtimeViewCount", 0, 10000)
            tryCompare(FeriaBrowserPolicy, "runtimeProfileCount", 0, 10000)
        }
        function test_inventory() {
            failOnWarning(/TypeError|ReferenceError|Cannot anchor/)
            verify(Fixture.evidenceRoot.length > 0)
            Fixture.switchProfile("streaming-capabilities")
            browser = browserFactory.createObject(parent)
            verify(browser !== null)
            tryCompare(browser, "documentReady", true, 15000)
            verify(js("window.isSecureContext"), "Loopback fixture must be a secure context")
            verify(js("typeof navigator.requestMediaKeySystemAccess === 'function'"), "EME API is required")
            verify(js("typeof MediaSource === 'function'"), "MSE API is required")
            var script = `(function() {
                const types = {
                    vp8: 'video/webm; codecs="vp8"',
                    vorbis: 'audio/webm; codecs="vorbis"',
                    avc: 'video/mp4; codecs="avc1.42E01E"',
                    aac: 'audio/mp4; codecs="mp4a.40.2"'
                };
                const report = {origin: location.origin, secure: isSecureContext,
                    codecs: {}, keySystems: [], done: false};
                window.__feriaStreamingCapabilities = report;
                for (const [name, type] of Object.entries(types)) {
                    report.codecs[name] = {type,
                        canPlayType: document.createElement(name === 'aac' || name === 'vorbis' ? 'audio' : 'video').canPlayType(type),
                        mediaSource: MediaSource.isTypeSupported(type)};
                }
                const probes = [
                    ['clear-key-webm', 'org.w3.clearkey', types.vp8, types.vorbis, 'webm'],
                    ['widevine-webm', 'com.widevine.alpha', types.vp8, types.vorbis, 'webm'],
                    ['widevine-mp4', 'com.widevine.alpha', types.avc, types.aac, 'cenc']
                ];
                (async function() {
                    for (const [label, keySystem, video, audio, initData] of probes) {
                        const row = {label, keySystem, access: false, mediaKeysCreated: false};
                        report.keySystems.push(row);
                        try {
                            const access = await navigator.requestMediaKeySystemAccess(keySystem, [{
                                initDataTypes: [initData], sessionTypes: ['temporary'],
                                distinctiveIdentifier: 'not-allowed', persistentState: 'not-allowed',
                                videoCapabilities: [{contentType: video}], audioCapabilities: [{contentType: audio}]
                            }]);
                            row.access = true;
                            row.configuration = access.getConfiguration();
                            await access.createMediaKeys();
                            row.mediaKeysCreated = true;
                        } catch (error) {
                            row.error = {name: error.name, message: error.message};
                        }
                    }
                    report.done = true;
                })();
                return true;
            })()`
            verify(js(script))
            tryVerify(function() { return js("window.__feriaStreamingCapabilities.done") === true }, 25000)
            var report = js("window.__feriaStreamingCapabilities")
            console.log("STREAMING_CAPABILITIES " + JSON.stringify(report))
            compare(report.keySystems.length, 3)
            // Clear Key is a local control; Widevine refusal is recorded as a
            // capability limitation, never converted into provider acceptance.
            compare(report.keySystems[0].label, "clear-key-webm")
            compare(report.keySystems[0].access, true)
            compare(report.keySystems[0].mediaKeysCreated, true)
            verify(report.codecs.vp8.canPlayType.length > 0)
            compare(report.codecs.vp8.mediaSource, true)
        }
    }
}
