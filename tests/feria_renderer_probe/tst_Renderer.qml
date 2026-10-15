import QtQuick
import QtTest
import "../../qml/feria" as Feria

Item {
    width: 960; height: 640
    Feria.FeriaSessionDeck {
        id: deck
        anchors.fill: parent
        sessions: Sessions; accountStore: FeriaAccount; policy: FeriaBrowserPolicy
        displayed: true
    }
    TestCase {
        name: "FeriaRenderer"
        when: windowShown
        function cleanup() {
            deck.startQuiescence(false)
            tryCompare(deck, "quiescing", false, 15000)
        }
        function test_ownedRendererLossDuringCloseRetiresUnansweredTickets() {
            var id = Sessions.openBrowser({providerId: "mangaplus", mode: "read", url: FixtureUrl + "/a"}, true)
            tryVerify(function() { var p = deck.browserForSession(id); return p && p.documentReady && !p.loading }, 15000)
            var page = deck.browserForSession(id)
            verify(page.evaluateJavaScript("(()=>{var until=Date.now()+3000;while(Date.now()<until){};return true})()"))
            deck.startQuiescence(false)
            tryCompare(page, "closingSession", true, 1000)
            verify(page.pendingJavaScript > 0)
            verify(Fixture.terminateOnlyRenderer(page))
            tryCompare(deck, "quiescing", false, 10000)
            compare(FeriaBrowserPolicy.runtimeViewCount, 0)
            compare(FeriaBrowserPolicy.runtimeProfileCount, 0)
        }
        function test_ownedRendererLossKeepsSpaceUntilManualReload() {
            var id = Sessions.openBrowser({providerId: "mangaplus", providerName: "Fixture", mode: "read", url: FixtureUrl + "/a"}, true)
            tryVerify(function() { var p = deck.browserForSession(id); return p && p.documentReady && !p.loading }, 15000)
            var page = deck.browserForSession(id)
            tryVerify(function() { return page.renderProcessPid > 0 }, 5000)
            // The fixture's pending transport request must be retired on death,
            // while suppressing its application effect.
            var effect = false
            verify(page.evaluateJavaScript("(()=>{var until=Date.now()+750;while(Date.now()<until){};return true})()", function() { effect = true }))
            verify(page.pendingJavaScript > 0)
            verify(Fixture.terminateOnlyRenderer(page), "Only this fixture's verified descendant renderer may be terminated")
            tryCompare(page, "rendererLost", true, 10000)
            tryCompare(page, "pendingJavaScript", 0, 5000)
            compare(effect, false)
            compare(Sessions.browsers().length, 1)
            compare(Sessions.activeId, id)
            compare(deck.browserForSession(id), page)
            verify(deck.entries[id].errorText.length > 0)
            compare(deck.entries[id].mainLoadSucceeded, false)
            wait(300)
            compare(page.rendererLost, true, "There must be no automatic recovery loop")
            verify(deck.reload(id))
            tryVerify(function() { return page.documentReady && !page.loading && !page.rendererLost }, 15000)
            compare(deck.browserForSession(id), page)
            compare(deck.entries[id].errorText, "")
            compare(deck.entries[id].mainLoadSucceeded, true)
            var recovered = false
            verify(page.evaluateJavaScript("document.title", function(title) { recovered = title === "Fixture /a" }))
            tryVerify(function() { return recovered }, 5000)
            tryCompare(page, "pendingJavaScript", 0, 5000)
            compare(effect, false)
        }
    }
}
