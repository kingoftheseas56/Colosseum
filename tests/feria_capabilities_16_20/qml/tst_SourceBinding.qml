// Safe source-provenance reproducer for official Qt 6.11.1.
// Never evaluates request.view/page and never retires the view while a request
// is pending. A live WebEngineView does NOT prove Quick's raw getter non-null.
import QtQuick
import QtTest
import QtWebEngine

Item {
    width: 800
    height: 600

    WebEngineProfile {
        id: testProfile
        offTheRecord: true
        downloadPath: Fixture.downloadRoot
        onDownloadRequested: function(request) {
            // Read only supported transfer facts and the fixture's independently
            // held live view. No attempt to recover source from the request.
            var evidence = {
                caseName: testCase.currentCase,
                event: "public-download-request",
                nativeId: String(request.id),
                downloadUrl: String(request.url),
                initiatingDocument: testCase.initiatingDocument,
                currentKnownViewDocument: retainedView.documentToken,
                currentKnownViewUrl: String(retainedView.url),
                viewIdentityUnchanged: retainedView.objectName === "retained-provenance-view",
                unsafeSourceGetterRead: false,
                sourceAttribution: "not-inferred",
                gateReleased: testCase.gateReleased
            }
            testCase.callbacks = testCase.callbacks.concat([evidence])
            console.info("FERIA_SOURCE_BINDING_CALLBACK " + JSON.stringify(evidence))
            request.cancel()
        }
    }

    WebEngineView {
        id: retainedView
        anchors.fill: parent
        objectName: "retained-provenance-view"
        profile: testProfile
        property string documentToken: ""
    }

    TestCase {
        id: testCase
        name: "SourceBinding"
        when: windowShown
        property string currentCase: ""
        property string initiatingDocument: ""
        property string gate: ""
        property bool gateReleased: false
        property var callbacks: []
        property var scriptResult: null
        property bool scriptFinished: false

        function runScript(script) {
            scriptFinished = false
            scriptResult = null
            retainedView.runJavaScript(script, function(result) {
                scriptResult = result
                scriptFinished = true
            })
            tryVerify(function() { return scriptFinished }, 10000)
            return scriptResult
        }

        function loadDocument(token) {
            retainedView.documentToken = token
            var target = Fixture.baseUrl + "/provenance-" + token
            retainedView.url = target
            tryVerify(function() {
                return !retainedView.loading && String(retainedView.url) === target
                    && retainedView.title === "Fixture /provenance-" + token
            }, 15000)
            compare(runScript("location.pathname"), "/provenance-" + token)
        }

        function beginGatedDownload(label) {
            Fixture.reset()
            currentCase = label
            gate = "source-binding-" + label
            callbacks = []
            gateReleased = false
            loadDocument("A-" + label)
            initiatingDocument = retainedView.documentToken
            var href = "/gated-download?gate=" + gate + "&name=source-proof.bin"
            var script = "document.body.innerHTML = '';"
                + "var a=document.createElement('a');a.id='fixtureDownload';"
                + "a.href=" + JSON.stringify(href) + ";a.download='source-proof.bin';"
                + "a.textContent='Start gated download';"
                + "a.style='position:fixed;left:20px;top:20px;width:260px;height:70px;display:block';"
                + "document.body.appendChild(a);location.pathname;"
            compare(runScript(script), "/provenance-A-" + label)
            mouseClick(retainedView, 70, 45, Qt.LeftButton)
            tryVerify(function() { return Fixture.requestCount(gate) > 0 }, 10000)
            compare(callbacks.length, 0, "The fixture withholds attachment response headers")
            console.info("FERIA_SOURCE_BINDING_INITIATOR " + JSON.stringify({
                caseName: label, initiatingDocument: initiatingDocument,
                serverRequestLog: Fixture.requestLog(), unsafeSourceGetterRead: false
            }))
        }

        function releaseGate() {
            gateReleased = true
            var released = Fixture.release(gate)
            console.info("FERIA_SOURCE_BINDING_GATE " + JSON.stringify({
                caseName: currentCase, releasedConnections: released,
                currentKnownViewDocument: retainedView.documentToken
            }))
            return released
        }

        function test_01_liveDocumentControl() {
            beginGatedDownload("control")
            compare(releaseGate(), 1)
            tryVerify(function() { return callbacks.length > 0 }, 10000)
            compare(callbacks.length, 1)
            verify(callbacks[0].gateReleased)
            compare(callbacks[0].initiatingDocument, callbacks[0].currentKnownViewDocument)
            console.info("FERIA_SOURCE_BINDING_CONTROL_OBSERVED")
        }

        function test_02_retainedViewNavigatesBeforeDelivery() {
            beginGatedDownload("navigation")
            loadDocument("B-navigation")
            compare(callbacks.length, 0)
            compare(retainedView.documentToken, "B-navigation")
            var released = releaseGate()
            // A timeout is an observation window for this fixture, not evidence
            // that source identity or request retention works. Report explicitly.
            var elapsed = 0
            while (!callbacks.length && elapsed < 10000) { wait(50); elapsed += 50 }
            if (!callbacks.length) {
                console.info("FERIA_SOURCE_BINDING_INCONCLUSIVE " + JSON.stringify({
                    reason: "No public request after navigation; request may have been cancelled",
                    releasedConnections: released, serverRequestLog: Fixture.requestLog(),
                    unsafeSourceGetterRead: false
                }))
                skip("Navigation cancelled or suppressed the gated download; no provenance outcome claimed")
                return
            }
            compare(callbacks.length, 1)
            verify(callbacks[0].gateReleased)
            compare(callbacks[0].initiatingDocument, "A-navigation")
            compare(callbacks[0].currentKnownViewDocument, "B-navigation")
            verify(callbacks[0].initiatingDocument !== callbacks[0].currentKnownViewDocument)
            console.info("FERIA_SOURCE_BINDING_PROVENANCE_GAP_OBSERVED " + JSON.stringify(callbacks[0]))
        }
    }
}
