import QtQuick
import QtQuick.Window
import QtTest
import "../../qml/feria" as Feria

Item {
    id: stage
    width: 900; height: 700
    Feria.FeriaSessionDeck {
        id: deck
        anchors.fill: parent
        sessions: Sessions; accountStore: FeriaAccount; policy: FeriaBrowserPolicy
        displayed: true
    }
    Component {
        id: legacyFactory
        Feria.FeriaQtWebEngine {
            width: 900; height: 700
            sourceUrl: FixtureUrl + "/hosting"
            profilePath: Fixture.evidenceRoot + "/legacy-profile"
            appLayout: false; appKeyboard: false; observationEnabled: false
        }
    }
    TestCase {
        name: "FeriaHostIdentity"
        when: windowShown
        property var legacy: null
        function init() {
            failOnWarning(/TypeError|ReferenceError|Cannot anchor/)
            Fixture.switchProfile("identity-host")
        }
        function cleanup() {
            if (legacy) {
                legacy.popups.slice().forEach(function(popup) { popup.close() })
                legacy.prepareForClose(false)
                tryCompare(legacy, "destructionNotified", true, 10000)
                legacy.destroy(); legacy = null
            }
            deck.startQuiescence(false)
            tryCompare(deck, "quiescing", false, 15000)
            tryCompare(FeriaBrowserPolicy, "runtimeViewCount", 0, 10000)
        }
        function assertIdentity(window, page) {
            var identity = findChild(window.contentItem, "feriaSiteIdentity")
            verify(identity !== null)
            tryCompare(identity, "documentLoaded", true, 10000)
            compare(identity.originText, FixtureUrl)
            compare(identity.connectionText, "Not encrypted (HTTP)")
            var origin = findChild(identity, "feriaSiteOrigin")
            tryVerify(function() { return origin.visible && origin.width > 0 }, 5000,
                "New native hosts must finish layout with a visible origin")
            tryVerify(function() {
                var left = identity.mapToItem(window.contentItem, 0, 0).x
                return identity.width >= 120 && left >= 0 && left + identity.width <= window.width + 1
            }, 5000, "The origin needs usable space inside even the smallest allowed dialog")
            var identityBottom = identity.mapToItem(window.contentItem, 0, identity.height).y
            var pageTop = page.mapToItem(window.contentItem, 0, 0).y
            verify(pageTop >= identityBottom, "Remote content must not overlap the trusted identity header")
        }
        function test_nativeHostShowsEngineOrigin_data() {
            return [{tag: "wide", width: 900}, {tag: "minimum-dialog", width: 320}]
        }
        function test_nativeHostShowsEngineOrigin(row) {
            var id = Sessions.openBrowser({providerId: "mangaplus", providerName: "Untrusted title",
                url: FixtureUrl + "/hosting", startUrl: FixtureUrl + "/home", mode: "read"}, true)
            tryVerify(function() { var p = deck.browserForSession(id); return p && p.documentReady }, 15000)
            var page = deck.browserForSession(id)
            verify(deck.moveToDesktop(id, Qt.rect(100, 100, row.width, 600)))
            var host = deck.entries[id].hostWindow
            tryCompare(host.contentItem, "width", row.width, 5000)
            assertIdentity(host, page)
            verify(Fixture.captureWindow(page, "native-site-identity-" + row.tag + ".png"))
            verify(deck.navigateInput(id, FixtureUrl + "/b"))
            tryVerify(function() { return page.documentReady && String(page.url) === FixtureUrl + "/b" }, 10000)
            assertIdentity(host, page)
        }
        function test_legacyPopupShowsEngineOrigin() {
            legacy = legacyFactory.createObject(stage)
            verify(legacy !== null)
            tryCompare(legacy, "documentReady", true, 15000)
            var point = null
            verify(legacy.evaluateJavaScript("(()=>{let r=document.querySelector('#dialog').getBoundingClientRect();return {x:r.x+r.width/2,y:r.y+r.height/2}})()",
                function(value) { point = value }))
            tryVerify(function() { return point !== null }, 5000)
            legacy.Window.window.requestActivate()
            tryVerify(function() { return Fixture.focusWindowMatches(legacy.Window.window) }, 5000)
            mouseClick(legacy, point.x, point.y)
            tryVerify(function() { return legacy.popups.length === 1 }, 10000)
            var popup = legacy.popups[0]
            tryCompare(popup.view, "title", "POST result", 10000)
            assertIdentity(popup, popup.view)
            verify(Fixture.captureWindow(popup.view, "legacy-site-identity.png"))
        }
    }
}
