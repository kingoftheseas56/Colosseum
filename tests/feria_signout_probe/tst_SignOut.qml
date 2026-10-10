import QtQuick
import QtTest
import "../../qml/feria" as Feria

// Twenty-five real-engine sign-out cycles in one process. The fixture uses
// disposable profiles and its own loopback server, never provider accounts.
Item {
    id: root
    width: 1060; height: 760
    Item { id: embedded; x: 25; y: 125; width: 820; height: 580 }
    Feria.FeriaSessionDeck {
        id: deck
        parent: embedded; anchors.fill: parent
        sessions: Sessions; accountStore: FeriaAccount; policy: FeriaBrowserPolicy
        displayed: true
    }
    TestCase {
        id: tests
        name: "FeriaSignOutStress"
        when: windowShown
        readonly property var runtime: FeriaBrowserPolicy.runtime
        property int cyclesCompleted: 0
        property int observedReleases: 0
        property int releasesWithPending: 0
        property int currentCycle: 0
        property var watchedLoader: null
        property var watchedView: null
        SignalSpy { id: quiescedSpy; target: deck; signalName: "quiesced" }
        SignalSpy { id: cleanedSpy; signalName: "finished" }
        Component { id: cleanerFactory; Feria.FeriaSessionCleaner {} }

        // Read the native ledger at the loader's release boundary, while the
        // retained wrapper reference is still available. A missed observation
        // is reported, not silently counted as a zero-pending release.
        Connections {
            target: tests.watchedLoader
            ignoreUnknownSignals: true
            function onActiveChanged() {
                if (!tests.watchedLoader || tests.watchedLoader.active || !tests.watchedView) return
                var outstanding = tests.runtime.pendingWork(tests.watchedView)
                ++tests.observedReleases
                if (outstanding > 0) ++tests.releasesWithPending
                console.log("SIGNOUT_RELEASE", tests.currentCycle, "pending", outstanding)
            }
        }
        function descriptor(path, url) {
            return {providerId: "mangaplus", providerName: "Fixture",
                url: url, startUrl: FixtureUrl + "/home", mode: "read"}
        }
        function openPage(url) {
            var id = deck.open(descriptor(url.substring(url.lastIndexOf("/")), url), true, false, true)
            verify(id.length > 0, "a fixture tab must open")
            tryVerify(function() {
                var p = deck.browserForSession(id)
                return p && p.documentReady && !p.loading && !p.replacementLoadPending
                    && String(p.url) === url
            }, 15000, "fixture page must load: " + url)
            return deck.browserForSession(id)
        }
        function js(page, script) {
            var completed = false, result
            verify(page.evaluateJavaScript(script, function(value) {
                result = value; completed = true
            }), "evaluateJavaScript accepted")
            tryVerify(function() { return completed }, 5000, "script callback")
            return result
        }
        function findCleanerLoader(cleaner) {
            for (var i = 0; i < cleaner.children.length; ++i) {
                var child = cleaner.children[i]
                if (child && typeof child.setSource === "function"
                        && typeof child.active === "boolean") return child
            }
            return null
        }
        function initTestCase() {
            failOnWarning(/TypeError|ReferenceError|incompatible arguments|Could not convert argument/)
            deck.startQuiescence(false)
            tryCompare(deck, "quiescing", false, 15000)
            Fixture.switchProfile("signout-stress")
            tryCompare(deck, "quiescing", false, 15000)
            deck.displayed = true; deck.suppressed = false
        }
        function cleanupTestCase() {
            watchedView = null; watchedLoader = null
            deck.startQuiescence(false)
            tryCompare(deck, "quiescing", false, 15000)
            compare(FeriaBrowserPolicy.runtimeViewCount, 0)
            compare(FeriaBrowserPolicy.runtimeProfileCount, 0)
            console.log("SIGNOUT_TOTAL cycles", cyclesCompleted,
                "observedReleases", observedReleases,
                "releasesWithPending", releasesWithPending)
        }
        function test_signOutRepeatsAndIsolatesOrigins() {
            var site = FixtureUrl
            var other = FixtureUrl.replace("127.0.0.1", "localhost")
            var first = openPage(site + "/a")
            var second = openPage(other + "/b")
            for (var n = 1; n <= 25; ++n) {
                currentCycle = n
                var siteValue = "site" + n, otherValue = "other" + n
                var siteWrite = "document.cookie='marker=" + siteValue + "; max-age=86400; path=/';"
                    + "localStorage.setItem('marker','" + siteValue + "'); document.cookie + '|' + localStorage.getItem('marker')"
                var otherWrite = "document.cookie='marker=" + otherValue + "; max-age=86400; path=/';"
                    + "localStorage.setItem('marker','" + otherValue + "'); document.cookie + '|' + localStorage.getItem('marker')"
                var read = "document.cookie + '|' + localStorage.getItem('marker')"
                compare(js(first, siteWrite), "marker=" + siteValue + "|" + siteValue)
                compare(js(second, otherWrite), "marker=" + otherValue + "|" + otherValue)

                quiescedSpy.clear()
                deck.startQuiescence(true)
                tryVerify(function() { return quiescedSpy.count === 1 }, 20000, "deck quiesced before sign-out")
                compare(FeriaBrowserPolicy.runtimeViewCount, 0)
                var cleaner = cleanerFactory.createObject(root, {
                    profilePath: FeriaBrowserPolicy.storageRoot,
                    engines: ["qtwebengine"], domains: ["127.0.0.1"], origins: [site]
                })
                verify(cleaner !== null, "cleaner created")
                cleanedSpy.clear(); cleanedSpy.target = cleaner
                tryVerify(function() {
                    watchedLoader = findCleanerLoader(cleaner)
                    watchedView = watchedLoader ? watchedLoader.item : null
                    return watchedView !== null
                }, 15000, "cleaner has a real WebEngineView")
                tryVerify(function() { return cleanedSpy.count === 1 }, 70000, "cleaner finished")
                compare(cleanedSpy.count, 1, "finished exactly once")
                console.log("SIGNOUT_CYCLE", n, "finished", cleanedSpy.signalArguments[0][0])
                cleanedSpy.target = null
                watchedView = null; watchedLoader = null
                cleaner.destroy()
                deck.finishCleanup()
                tryCompare(deck, "quiescing", false, 15000)

                first = openPage(site + "/a")
                second = openPage(other + "/b")
                compare(js(first, read), "|null", "signed-out site cleared in cycle " + n)
                compare(js(second, read), "marker=" + otherValue + "|" + otherValue,
                    "other origin untouched in cycle " + n)
                ++cyclesCompleted
                console.log("SIGNOUT_COMPLETED", cyclesCompleted,
                    "releasesWithPending", releasesWithPending)
            }
            compare(cyclesCompleted, 25)
            compare(observedReleases, 25, "every cleaner view release was observed")
            compare(releasesWithPending, 0, "no cleaner view may be released with pending work")
        }
    }
}
