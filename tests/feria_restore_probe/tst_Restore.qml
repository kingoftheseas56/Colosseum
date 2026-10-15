import QtQuick
import QtTest
import "../../qml/feria" as Feria

Item {
    id: scene
    width: 960; height: 640
    property var deck: null
    Component {
        id: deckFactory
        Feria.FeriaSessionDeck {
            anchors.fill: parent
            sessions: Sessions; accountStore: FeriaAccount; policy: FeriaBrowserPolicy
            displayed: true
        }
    }
    TestCase {
        name: "FeriaRestore"
        when: windowShown
        property int serial: 0
        function init() { Fixture.switchProfile("restore-v1-" + (++serial)) }
        function cleanup() {
            if (!scene.deck) return
            scene.deck.startQuiescence(false)
            tryCompare(scene.deck, "quiescing", false, 15000)
            scene.deck.destroy(); scene.deck = null
            wait(0)
        }
        function saved(path, provider) {
            return {providerId: provider || "mangaplus", providerName: "Fixture", mode: "read", url: FixtureUrl + path}
        }
        function create(savedRows) {
            verify(FeriaAccount.saveBrowserTabs(savedRows))
            scene.deck = deckFactory.createObject(scene)
            verify(scene.deck !== null)
            tryCompare(scene.deck, "ready", true)
        }
        function test_failedLoadKeepsOfferAndRetryReusesSpace() {
            create([saved("/broken")])
            compare(scene.deck.restorableTabCount, 1)
            scene.deck.restoreTabs("mangaplus")
            compare(Sessions.browsers().length, 1)
            var id = Sessions.browsers()[0].id
            tryVerify(function() { return scene.deck.entries[id].errorText.length > 0 }, 15000)
            compare(scene.deck.restorableTabCount, 1, "Failed navigation must remain offered")
            scene.deck.restoreTabs("mangaplus")
            compare(Sessions.browsers().length, 1, "Retry must reuse the existing failed space")
        }
        function test_realLoadConsumesOnlyItsAppOffer() {
            create([saved("/a"), saved("/b", "ytmusic")])
            scene.deck.restoreTabs("mangaplus")
            compare(Sessions.browsers().length, 1, "An app restores only its own spaces")
            tryCompare(scene.deck, "restorableTabCount", 1, 15000)
            compare(scene.deck.restoreSource[0].providerId, "ytmusic")
            compare(FeriaAccount.browserRestoreOffers.length, 1)
        }
        function test_duplicateUrlsRemainSeparateSpaces() {
            create([saved("/a"), saved("/a")])
            scene.deck.restoreTabs("mangaplus")
            compare(Sessions.browsers().length, 2)
            tryCompare(scene.deck, "restorableTabCount", 0, 15000)
        }
        function test_pendingDuplicateDoesNotMatchSuccessfulSibling() {
            create([saved("/a"), saved("/a")])
            var pending = scene.deck.restoreSource[1]
            scene.deck.restoreTabs("mangaplus")
            tryCompare(scene.deck, "restorableTabCount", 0, 15000)
            var records = Sessions.browsers()
            var failed = records.find(function(row) { return row.target.restoreId === pending.restoreId })
            verify(failed !== undefined)
            scene.deck.browserForSession(failed.id).renderProcessTerminated(1, 9)
            verify(FeriaAccount.saveBrowserRestoreOffers([pending]))
            scene.deck.restoreTabs("mangaplus")
            compare(Sessions.activeId, failed.id, "Retry must raise the exact duplicate that lost its renderer")
            compare(Sessions.browsers().length, 2)
            tryCompare(scene.deck, "restorableTabCount", 0, 15000)
        }
        function test_legacyPreferenceNeverRestoresAutomatically() {
            verify(FeriaAccount.setBrowserPreference("restoreTabs", true))
            create([saved("/a")])
            wait(300)
            compare(Sessions.browsers().length, 0)
            compare(scene.deck.restorableTabCount, 1)
        }
        function test_currentSnapshotDoesNotEraseDurableOffer() {
            create([saved("/broken")])
            var id = Sessions.openBrowser(saved("/a"), true)
            tryVerify(function() { var p=scene.deck.browserForSession(id); return p && p.documentReady && !p.loading }, 15000)
            scene.deck.persistTabs()
            compare(scene.deck.restoreSource[0].url, FixtureUrl + "/broken")
            compare(FeriaAccount.browserRestoreOffers[0].url, FixtureUrl + "/broken")
            verify(scene.deck.dismissRestoreTabs("mangaplus"))
            compare(FeriaAccount.browserRestoreOffers.length, 0)
        }
    }
}
