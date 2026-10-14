import QtQuick
import QtQuick.Window
import QtTest
import QtWebEngine
import "../../qml/feria" as Feria

Item {
    width: 900; height: 700
    Window { id: inputHost; width: 900; height: 700; visible: false; title: "Feria frame permission fixture" }
    Component {
        id: browserFactory
        Feria.FeriaQtWebEngine {
            width: 900; height: 700
            sourceUrl: FixtureUrl + "/services-permission-parent"
            profilePath: Fixture.evidenceRoot + "/frame-permission-profile"
            appLayout: false; appKeyboard: false; observationEnabled: false
        }
    }
    TestCase {
        name: "FeriaFramePermissions"
        when: windowShown
        property var browser: null
        property int serial: 0
        function js(source) {
            var done = false, value
            verify(browser.evaluateJavaScript(source, function(result) { value = result; done = true }))
            tryVerify(function() { return done }, 5000)
            return value
        }
        function report(nonce, kind) {
            return js("window.frameReports.find(r=>r.nonce===" + JSON.stringify(nonce)
                + "&&r.kind===" + JSON.stringify(kind) + ")||null")
        }
        function init() {
            failOnWarning(/TypeError|ReferenceError|Cannot anchor/)
            verify(Fixture.evidenceRoot.length > 0)
            Fixture.switchProfile("frame-permissions-" + (++serial))
            browser = browserFactory.createObject(inputHost.contentItem,
                {profilePath: Fixture.evidenceRoot + "/frame-permission-profile-" + serial})
            verify(browser !== null)
            inputHost.show()
            tryCompare(browser, "documentReady", true, 15000)
            tryVerify(function() { return !!report("first", "ready") }, 10000)
            var ready = report("first", "ready")
            console.log("FRAME_PERMISSION_API " + JSON.stringify(ready))
            compare(ready.secure, true)
            compare(ready.api, true, "Local Fonts API must actually exist in this runtime")
        }
        function cleanup() {
            if (browser) {
                browser.browserRequests.cancelPending()
                browser.prepareForClose(false)
                tryCompare(browser, "pendingJavaScript", 0, 10000)
                tryCompare(browser, "destructionNotified", true, 10000)
                browser.destroy(); browser = null
            }
            inputHost.hide()
            tryCompare(FeriaBrowserPolicy, "runtimeViewCount", 0, 10000)
            tryCompare(FeriaBrowserPolicy, "runtimeProfileCount", 0, 10000)
        }
        function clickRequest() {
            browser.forceActiveFocus(); inputHost.requestActivate()
            tryVerify(function() { return Fixture.focusWindowMatches(inputHost) }, 5000)
            mouseClick(browser, 100, 80)
        }
        function assertDenied(nonce, origin) {
            tryVerify(function() { return !!report(nonce, "result") }, 10000)
            var result = report(nonce, "result")
            // Chromium resolves denial with an empty list (font_access.cc),
            // rather than necessarily rejecting the promise. Check permission
            // state and absence of returned font data, not promise settlement alone.
            compare(browser.profile.queryPermission(origin, WebEnginePermission.PermissionType.LocalFontsAccess).state,
                WebEnginePermission.State.Denied)
            verify(result.result === "rejected" || (result.result === "resolved" && result.count === 0),
                JSON.stringify(result))
        }
        function test_removedFrameCannotReceiveLateGrant() {
            var origin = String(FixtureUrl).replace("127.0.0.1", "localhost")
            var requests = browser.browserRequests
            tryVerify(function() { return !!report("first", "state") }, 5000)
            compare(report("first", "state").state, "prompt")
            clickRequest()
            tryVerify(function() { return requests.current !== null }, 10000)
            compare(requests.current.kind, "permission")
            compare(requests.current.request.permissionType, WebEnginePermission.PermissionType.LocalFontsAccess)
            compare(requests.current.origin, origin)
            verify(requests.promptText().indexOf(origin) >= 0)
            verify(requests.promptText().indexOf("use installed fonts") >= 0)
            var oldId = requests.current.id
            console.log("FRAME_PERMISSION_REQUEST " + JSON.stringify({id:oldId,origin:requests.current.origin}))
            compare(browser.mainFrame.children.length, 1)
            compare(js("document.querySelector('iframe').remove();!document.querySelector('iframe')"), true)
            tryVerify(function() { return browser.mainFrame.children.length === 0 }, 5000)
            requests.answer(oldId, true)
            var state = browser.profile.queryPermission(origin, WebEnginePermission.PermissionType.LocalFontsAccess).state
            console.log("FRAME_PERMISSION_AFTER_ANSWER state=" + state)
            verify(state !== WebEnginePermission.State.Granted, "A removed iframe must not gain a persistent grant from its old prompt")
            compare(js("window.makeFrame('replacement')"), true)
            tryVerify(function() { return !!report("replacement", "state") }, 10000)
            verify(report("replacement", "state").state !== "granted")
            clickRequest()
            tryVerify(function() { return requests.current !== null || !!report("replacement", "result") }, 10000)
            if (requests.current) {
                verify(requests.current.id !== oldId)
                requests.answer(requests.current.id, false)
            }
            assertDenied("replacement", origin)
        }
        function test_liveFrameGrantAndExplicitReset() {
            var origin = String(FixtureUrl).replace("127.0.0.1", "localhost")
            var requests = browser.browserRequests
            tryVerify(function() { return !!report("first", "state") }, 5000)
            compare(report("first", "state").state, "prompt")
            clickRequest()
            tryVerify(function() { return requests.current !== null }, 10000)
            compare(requests.current.kind, "permission")
            compare(requests.current.request.permissionType, WebEnginePermission.PermissionType.LocalFontsAccess)
            compare(requests.current.origin, origin)
            verify(requests.answer(requests.current.id, true))
            tryVerify(function() { return !!report("first", "result") }, 10000)
            compare(report("first", "result").result, "resolved")
            verify(report("first", "result").count > 0, "Live allow must yield actual font entries; names are never logged")
            var permission = browser.profile.queryPermission(origin, WebEnginePermission.PermissionType.LocalFontsAccess)
            compare(permission.state, WebEnginePermission.State.Granted)
            permission.reset()
            compare(browser.profile.queryPermission(origin, WebEnginePermission.PermissionType.LocalFontsAccess).state,
                WebEnginePermission.State.Ask)
            compare(js("document.querySelector('iframe').remove();window.makeFrame('reset')"), true)
            tryVerify(function() { return !!report("reset", "state") }, 10000)
            compare(report("reset", "state").state, "prompt")
            clickRequest()
            tryVerify(function() { return requests.current !== null }, 10000)
            verify(requests.answer(requests.current.id, false))
            assertDenied("reset", origin)
        }
    }
}
