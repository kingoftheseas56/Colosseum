import QtQuick
import QtQuick.Window
import QtTest
import "../../qml/feria" as Feria

Item {
    id: stage
    width: 900; height: 700
    // Give this input-driven case its own first exposure. The Windows fixture's
    // opt-in foreground claim runs once per newly exposed top-level window;
    // the shared QuickTest root may have lost foreground before this test starts.
    Window {
        id: inputHost
        width: 900; height: 700
        visible: false
        title: "Feria popup boundary fixture"
    }
    Component {
        id: browserFactory
        Feria.FeriaQtWebEngine {
            width: 900; height: 700
            sourceUrl: FixtureUrl + "/hosting"
            profilePath: Fixture.evidenceRoot + "/popup-boundary-profile"
            appLayout: false; appKeyboard: false; observationEnabled: false
        }
    }
    TestCase {
        name: "FeriaPopupBoundaries"
        when: windowShown
        property var browser: null
        function init() {
            failOnWarning(/TypeError|ReferenceError|Cannot anchor/)
            verify(Fixture.evidenceRoot.length > 0, "Use a disposable FERIA_SESSION_EVIDENCE_DIR")
            Fixture.switchProfile("popup-boundaries")
            compare(FeriaBrowserPolicy.runtimeViewCount, 0)
            compare(FeriaBrowserPolicy.runtimeProfileCount, 0)
        }
        function cleanup() {
            if (browser) {
                browser.popups.slice().forEach(function(popup) { popup.close() })
                tryVerify(function() { return browser.popups.length === 0 }, 10000)
                browser.prepareForClose(false)
                tryCompare(browser, "pendingJavaScript", 0, 10000)
                tryCompare(browser, "destructionNotified", true, 10000)
                browser.destroy(); browser = null
            }
            inputHost.hide()
            tryCompare(FeriaBrowserPolicy, "runtimeViewCount", 0, 10000)
            tryCompare(FeriaBrowserPolicy, "runtimeProfileCount", 0, 10000)
        }
        function test_clickOpenedPopupHasNoNativeBridge() {
            browser = browserFactory.createObject(inputHost.contentItem)
            verify(browser !== null)
            inputHost.show()
            tryCompare(inputHost, "visible", true, 5000)
            tryCompare(browser, "documentReady", true, 15000)
            var point = null
            verify(browser.evaluateJavaScript("(()=>{const r=document.querySelector('#dialog').getBoundingClientRect();return {x:r.x+r.width/2,y:r.y+r.height/2}})()",
                function(value) { point = value }))
            tryVerify(function() { return point !== null }, 5000)
            browser.forceActiveFocus()
            inputHost.requestActivate()
            tryVerify(function() { return Fixture.focusWindowMatches(browser.Window.window) }, 5000)
            mouseClick(browser, point.x, point.y)
            tryVerify(function() { return browser.popups.length === 1 }, 10000)
            var popup = browser.popups[0]
            tryCompare(popup.view, "title", "POST result", 10000)
            tryCompare(popup.view, "identityDocumentLoaded", true, 10000)
            var report = null
            popup.view.runJavaScript("(()=>{const names=['FeriaAccount','FeriaBrowserPolicy','Sessions','LocalLaunch'];const exposed=names.filter(name=>typeof window[name]!=='undefined');if(window.Clipboard&&typeof window.Clipboard.copy==='function')exposed.push('Clipboard.copy');if(window.qt&&window.qt.webChannelTransport)exposed.push('qt.webChannelTransport');window.__popupBoundaryMarker=41;return {executed:window.__popupBoundaryMarker+1,title:document.title,href:location.href,exposed:exposed}})()",
                function(value) { report = value })
            tryVerify(function() { return report !== null }, 5000)
            compare(report.executed, 42, "The remote child's JavaScript must actually execute")
            compare(report.title, "POST result")
            compare(report.href, String(popup.view.url))
            compare(report.exposed.length, 0, JSON.stringify(report.exposed))
            popup.close()
            tryVerify(function() { return browser.popups.length === 0 }, 10000)
            tryCompare(FeriaBrowserPolicy, "runtimeViewCount", 1, 10000)
            tryCompare(browser, "pendingJavaScript", 0, 10000)
        }
    }
}
