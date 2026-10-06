import QtQuick
import QtTest
import "../../qml/feria" as Feria

Item {
    width: 960; height: 640
    Feria.FeriaSessionDeck {
        id: deck
        anchors.fill: parent
        sessions: Sessions
        accountStore: FeriaAccount
        policy: FeriaBrowserPolicy
        displayed: true
    }
    TestCase {
        name: "FeriaRetainedSessions"
        when: windowShown
        property int serial: 0
        function init() {
            deck.startQuiescence(false)
            tryCompare(deck, "quiescing", false, 10000)
            Fixture.switchProfile("case-" + (++serial))
            tryCompare(deck, "quiescing", false, 10000)
            deck.displayed = true
        }
        function cleanup() {
            deck.startQuiescence(false)
            tryCompare(deck, "quiescing", false, 10000)
        }
        function open(provider, path, fresh) {
            var id = Sessions.openBrowser({ providerId: provider, providerName: provider,
                title: "Fixture", url: FixtureUrl + path, mode: "watch" }, fresh)
            verify(id.length > 0)
            tryVerify(function() { return deck.entries[id] && deck.entries[id].browser }, 10000)
            var page = deck.entries[id].browser
            tryCompare(page, "documentReady", true, 15000)
            return id
        }
        function evaluate(page, script) {
            var done = false
            var result
            page.evaluateJavaScript(script, function(value) { result = value; done = true })
            tryVerify(function() { return done }, 5000)
            return result
        }
        function test_twoViewsKeepHistoryAndWorldState() {
            var a = open("mangaplus", "/a", false)
            var pageA = deck.entries[a].browser
            evaluate(pageA, "location.href='/b'")
            tryVerify(function() { return String(pageA.url).endsWith("/b") && pageA.documentReady }, 10000)
            evaluate(pageA, "window.sessionMarker=2718")
            var b = open("ytmusic", "/music", false)
            var pageB = deck.entries[b].browser
            verify(pageA !== pageB)
            if (Fixture.evidenceRoot.length > 0) {
                waitForRendering(deck)
                grabImage(deck).save(Fixture.evidenceRoot + "/two-retained-fixture-pages.png")
            }
            compare(pageA.profile, pageB.profile)
            Sessions.switchTo(a)
            compare(deck.activeBrowser, pageA)
            compare(evaluate(pageA, "window.sessionMarker"), 2718)
            deck.displayed = false
            wait(100)
            deck.displayed = true
            compare(deck.entries[a].browser, pageA)
            compare(evaluate(pageA, "window.sessionMarker"), 2718)
            pageA.runJavaScript("history.back()")
            tryVerify(function() { return String(pageA.url).endsWith("/a") && pageA.documentReady }, 10000)
            Sessions.close(a)
            tryVerify(function() { return !deck.entries[a] }, 5000)
            compare(deck.entries[b].browser, pageB)
            compare(Sessions.activeId, "")
            Sessions.switchTo(b)
            compare(deck.activeBrowser, pageB)
        }
        function test_backgroundMediaAdvancesWithoutWorldVisibility() {
            var a = open("ytmusic", "/music", false)
            var page = deck.entries[a].browser
            evaluate(page, "document.getElementById('audio').play(); true")
            tryVerify(function() { return evaluate(page, "document.getElementById('audio').currentTime") > 0.1 }, 10000)
            var before = evaluate(page, "document.getElementById('audio').currentTime")
            deck.displayed = false
            wait(500)
            var after = evaluate(page, "document.getElementById('audio').currentTime")
            verify(after > before, "Unencrypted fixture audio time must advance while world is hidden")
            compare(evaluate(page, "document.getElementById('audio').paused"), false)
            deck.displayed = true
        }
        function test_defaultResumeAndExplicitNewHaveSeparateIdentity() {
            var a = open("mangaplus", "/a", false)
            var pageA = deck.entries[a].browser
            evaluate(pageA, "window.sessionMarker=61")
            var resumed = open("mangaplus", "/home", false)
            compare(resumed, a)
            compare(deck.entries[a].browser, pageA)
            compare(evaluate(pageA, "window.sessionMarker"), 61)
            var b = open("mangaplus", "/a", true)
            verify(a !== b)
            verify(deck.entries[b].browser !== pageA)
            Sessions.switchTo(a)
            compare(open("mangaplus", "/home", false), a)
            verify(deck.navigateSession(a, FixtureUrl + "/b", 0, null))
            tryVerify(function() { return String(pageA.url).endsWith("/b") && pageA.documentReady }, 10000)
            var entryB = deck.entries[b]
            var browserB = entryB.browser
            var urlB = String(browserB.url)
            compare(urlB, FixtureUrl + "/a")
        }
        function test_cleanupWaitsForAllViewsAndSharedProfile() {
            open("mangaplus", "/a", false)
            open("ytmusic", "/music", false)
            verify(FeriaBrowserPolicy.runtimeViewCount >= 2)
            compare(FeriaBrowserPolicy.runtimeProfileCount, 1)
            deck.startQuiescence(true)
            tryCompare(deck, "cleanupNotified", true, 10000)
            compare(FeriaBrowserPolicy.runtimeViewCount, 0)
            compare(FeriaBrowserPolicy.runtimeProfileCount, 0)
            compare(deck.sharedProfile, null)
            compare(Sessions.browsers().length, 0)
            compare(deck.ready, false)
            deck.finishCleanup()
            tryCompare(deck, "quiescing", false, 10000)
            verify(open("mangaplus", "/a", false).length > 0)
        }
        function test_profileSwitchDestroysOldViewsAndTokens() {
            var a = open("mangaplus", "/a", false)
            var token = deck.entries[a].token
            Fixture.switchProfile("replacement-" + serial)
            tryVerify(function() { return Object.keys(deck.entries).length === 0 && !deck.quiescing }, 10000)
            compare(Sessions.browsers().length, 0)
            verify(!FeriaAccount.observeSession(a, token, {
                href: FixtureUrl + "/a", kind: "book", title: "Old", locator: {type:"url"}
            }))
            compare(FeriaAccount.sessions.length, 0)
        }
    }
}
