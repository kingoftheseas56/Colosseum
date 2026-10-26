import QtQuick
import QtQuick.Window
import "../../qml/feria" as Feria

// Whole-process journeys for the browser foundation, run by
//   feria_foundation_runtime --journey tests/feria_runtime_journey/Journey.qml
// with FERIA_JOURNEY naming the scenario. Unlike the QuickTest probes these
// end the way the application ends: Fixture.finish() leaves the event loop,
// aboutToQuit runs FeriaBrowserRuntime.shutdown(), and the process exit code is
// the verdict. Loopback fixture and disposable profiles only.
Window {
    id: win
    width: 1000; height: 700
    visible: true
    title: "Feria runtime journey: " + Fixture.journey
    readonly property var runtime: FeriaBrowserPolicy.runtime
    property var steps: []
    property int index: 0
    property real stepStarted: 0
    property var notes: ({})

    Feria.FeriaSessionDeck {
        id: deck
        anchors.fill: parent
        sessions: Sessions; accountStore: FeriaAccount; policy: FeriaBrowserPolicy
        displayed: true
    }

    function descriptor(path, extra) {
        return Object.assign({providerId: "mangaplus", providerName: "Fixture",
            url: FixtureUrl + path, startUrl: FixtureUrl + "/home", mode: "read"}, extra || {})
    }
    function page(id) { return deck.browserForSession(id) }
    function loaded(id, path) {
        var p = page(id)
        return !!p && p.documentReady && !p.loading && String(p.url) === FixtureUrl + path
    }
    function evaluate(id, key, script) {
        notes[key] = undefined
        return page(id).evaluateJavaScript(script, function(value) { win.notes[key] = value })
    }
    function fail(reason) { ticker.stop(); Fixture.finish(false, reason) }

    // A step is {name, run(), until()}; run() fires once, until() is polled.
    Timer {
        id: ticker
        interval: 50; repeat: true; running: true
        onTriggered: {
            if (win.index >= win.steps.length) { stop(); return }
            var step = win.steps[win.index]
            if (!step.started) {
                step.started = true
                win.stepStarted = Date.now()
                var problem = step.run ? step.run() : ""
                if (typeof problem === "string" && problem.length) { win.fail(step.name + ": " + problem); return }
            }
            var done = step.until ? step.until() : true
            if (typeof done === "string" && done.length) { win.fail(step.name + ": " + done); return }
            if (done === true) { ++win.index; return }
            if (Date.now() - win.stepStarted > 20000) win.fail(step.name + ": timed out")
        }
    }

    // Several live pages (two at the same URL), a private page, and work
    // still outstanding when the application leaves.
    function shutdownJourney() {
        var ids = {}
        return [
            {name: "open pages", run: function() {
                ids.a = deck.open(descriptor("/a"), true, false, true)
                ids.twin = deck.open(descriptor("/a"), true, false, false)
                ids.secret = deck.open(descriptor("/b", {privateBrowsing: true}), true, false, false)
                ids.slow = deck.open(descriptor("/c"), true, false, false)
                ids.leaving = deck.open(descriptor("/d"), true, false, false)
                return ids.a && ids.twin && ids.secret && ids.slow && ids.leaving && ids.a !== ids.twin ? "" : "open refused"
            }, until: function() {
                return loaded(ids.a, "/a") && loaded(ids.twin, "/a") && loaded(ids.secret, "/b")
                    && loaded(ids.slow, "/c") && loaded(ids.leaving, "/d")
            }},
            {name: "leave work outstanding", run: function() {
                // A script still running in the renderer, a navigation the
                // server has not answered, and a download in flight.
                if (!page(ids.a).evaluateJavaScript(
                        "(()=>{var until=Date.now()+4000;while(Date.now()<until){};return true})()",
                        function() { win.notes.lateScript = true })) return "script not admitted"
                if (!deck.navigateSession(ids.slow, FixtureUrl + "/slow", 0, null, null)) return "navigation refused"
                deck.sharedProfile.downloadPath = Fixture.downloadRoot   // never the user's Downloads
                page(ids.twin).url = FixtureUrl + "/download"
                // And a page that was closed but is still draining a script.
                var leaving = page(ids.leaving)
                if (!leaving.evaluateJavaScript("(()=>{var until=Date.now()+4000;while(Date.now()<until){};return true})()"))
                    return "retiring script not admitted"
                Sessions.close(ids.leaving)
                if (!leaving.closingSession || runtime.phase(ids.leaving) !== "retiring")
                    return "closed page did not start retiring"
                return ""
            }, until: function() {
                return page(ids.slow).loading && deck.downloads.items.length === 1
                    && runtime.pendingWork(page(ids.a)) > 0
            }},
            {name: "exit", run: function() {
                var summary = "views=" + runtime.viewCount + " profiles=" + runtime.profileCount
                    + " pendingScript=" + runtime.pendingWork(page(ids.a))
                    + " loading=" + page(ids.slow).loading
                    + " downloads=" + deck.downloads.items.length
                    + " sharedReleasable=" + runtime.profileReleasable(deck.sharedProfile)
                if (runtime.viewCount !== 5 || runtime.profileCount !== 2) return "unexpected engines: " + summary
                if (runtime.profileReleasable(deck.sharedProfile)) return "a profile with views and a download was releasable"
                Fixture.finish(true, summary)
                return ""
            }}
        ]
    }

    // First process: write synthetic site data in an ordinary and a private
    // page of the same origin, then leave cleanly.
    function retentionWrite() {
        var ids = {}
        return [
            {name: "open", run: function() {
                ids.regular = deck.open(descriptor("/a"), true, false, true)
                ids.secret = deck.open(descriptor("/b", {privateBrowsing: true}), true, false, false)
                return ids.regular && ids.secret ? "" : "open refused"
            }, until: function() { return loaded(ids.regular, "/a") && loaded(ids.secret, "/b") }},
            {name: "write site data", run: function() {
                evaluate(ids.regular, "regular", "document.cookie='feria_regular=kept; max-age=86400; path=/';"
                    + "localStorage.setItem('feria_regular','kept');document.cookie+'|'+localStorage.getItem('feria_regular')")
                evaluate(ids.secret, "secret", "document.cookie='feria_private=gone; max-age=86400; path=/';"
                    + "localStorage.setItem('feria_private','gone');document.cookie+'|'+localStorage.getItem('feria_private')")
                return ""
            }, until: function() {
                if (notes.regular === undefined || notes.secret === undefined) return false
                if (notes.regular !== "feria_regular=kept|kept") return "ordinary page saw " + notes.regular
                if (notes.secret !== "feria_private=gone|gone") return "private page saw " + notes.secret
                return true
            }},
            {name: "exit", run: function() {
                deck.persistTabs()
                Fixture.finish(true, "ordinary=" + notes.regular + " private=" + notes.secret
                    + " savedTabs=" + deck.tabDescriptors().length)
                return ""
            }}
        ]
    }

    // Second process, same disposable profile root and origin.
    function retentionRead() {
        var ids = {}
        return [
            {name: "restore offers", run: function() {
                var offers = deck.restoreSource
                for (var i = 0; i < offers.length; ++i)
                    if (offers[i].privateBrowsing === true || String(offers[i].url).indexOf("/b") >= 0)
                        return "a private space was offered for restore: " + offers[i].url
                notes.offers = offers.length
                if (offers.length !== 1 || String(offers[0].url) !== FixtureUrl + "/a")
                    return "expected exactly the ordinary /a offer, got " + JSON.stringify(offers)
                if (Sessions.browsers().length !== 0) return "pages were allocated before any restore action"
                return ""
            }},
            {name: "restore the ordinary space", run: function() {
                deck.restoreTabs("mangaplus")
                return Sessions.browsers().length === 1 ? "" : "restore did not open one space"
            }, until: function() {
                ids.regular = Sessions.browsers()[0].id
                return loaded(ids.regular, "/a") && deck.restorableTabCount === 0
            }},
            {name: "open a private space", run: function() {
                ids.secret = deck.open(descriptor("/b", {privateBrowsing: true}), true, false, false)
                return ids.secret ? "" : "open refused"
            }, until: function() { return loaded(ids.secret, "/b") }},
            {name: "read site data", run: function() {
                var probe = "document.cookie+'|'+localStorage.getItem('feria_regular')+'|'+localStorage.getItem('feria_private')"
                evaluate(ids.regular, "regular", probe)
                evaluate(ids.secret, "secret", probe)
                return ""
            }, until: function() {
                if (notes.regular === undefined || notes.secret === undefined) return false
                if (notes.regular !== "feria_regular=kept|kept|null")
                    return "ordinary data after restart was " + notes.regular
                if (notes.secret !== "|null|null")
                    return "private page after restart saw " + notes.secret
                return true
            }},
            {name: "exit", run: function() {
                Fixture.finish(true, "ordinary=" + notes.regular + " private=" + notes.secret + " offers=" + notes.offers)
                return ""
            }}
        ]
    }

    Component.onCompleted: {
        Fixture.switchProfile("journey")
        if (Fixture.journey === "shutdown") steps = shutdownJourney()
        else if (Fixture.journey === "retention-write") steps = retentionWrite()
        else if (Fixture.journey === "retention-read") steps = retentionRead()
        else Fixture.finish(false, "unknown journey " + Fixture.journey)
    }
}
