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
        var sawFive = false
        var sawRetiring = false
        function liveIds() { return [ids.a, ids.twin, ids.secret, ids.slow] }
        function bound(all) {
            return all.every(function(id) {
                var view = page(id)
                return !!view && runtime.hasView(view) && runtime.instance(id).bound === true
            })
        }
        function partitionsCorrect(includeLeaving) {
            var ordinary = [ids.a, ids.twin, ids.slow]
            if (includeLeaving) ordinary.push(ids.leaving)
            return !!deck.sharedProfile && !!deck.privateProfile
                && deck.sharedProfile !== deck.privateProfile
                && runtime.hasProfile(deck.sharedProfile) && runtime.hasProfile(deck.privateProfile)
                && ordinary.every(function(id) {
                    var view = page(id)
                    return !!view && view.profile === deck.sharedProfile
                })
                && !!page(ids.secret) && page(ids.secret).profile === deck.privateProfile
        }
        function retiredGone() {
            return runtime.phase(ids.leaving) === "absent"
                && runtime.instance(ids.leaving).bound !== true
                && page(ids.leaving) === null
                && !Sessions.browsers().some(function(record) { return record.id === ids.leaving })
        }
        return [
            {name: "open pages", run: function() {
                ids.a = deck.open(descriptor("/a"), true, false, true)
                ids.twin = deck.open(descriptor("/a"), true, false, false)
                ids.secret = deck.open(descriptor("/b", {privateBrowsing: true}), true, false, false)
                ids.slow = deck.open(descriptor("/c"), true, false, false)
                ids.leaving = deck.open(descriptor("/d"), true, false, false)
                var opened = [ids.a, ids.twin, ids.secret, ids.slow, ids.leaving]
                return opened.every(function(id) { return !!id }) && new Set(opened).size === 5
                    ? "" : "open refused or duplicate session IDs"
            }, until: function() {
                if (!(loaded(ids.a, "/a") && loaded(ids.twin, "/a") && loaded(ids.secret, "/b")
                    && loaded(ids.slow, "/c") && loaded(ids.leaving, "/d"))) return false
                if (runtime.viewCount !== 5 || runtime.profileCount !== 2)
                    return "five-view allocation failed: views=" + runtime.viewCount + " profiles=" + runtime.profileCount
                if (!bound([ids.a, ids.twin, ids.secret, ids.slow, ids.leaving])
                        || !partitionsCorrect(true)) return "five live, correctly partitioned engines not proven"
                sawFive = true
                return true
            }},
            {name: "leave work outstanding", run: function() {
                if (!sawFive || runtime.viewCount !== 5 || runtime.profileCount !== 2)
                    return "five-view baseline was lost before retirement"
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
                sawRetiring = true
                return ""
            }, until: function() {
                if (!retiredGone()) return false
                if (runtime.viewCount !== 4 || runtime.profileCount !== 2) return false
                return page(ids.slow).loading && deck.downloads.items.length === 1
                    && runtime.pendingWork(page(ids.a)) > 0
            }},
            {name: "exit", run: function() {
                var activeScriptView = page(ids.a), slowView = page(ids.slow)
                var pending = activeScriptView ? runtime.pendingWork(activeScriptView) : 0
                var loading = !!slowView && slowView.loading
                var downloads = deck.downloads.items.length
                var releasable = runtime.profileReleasable(deck.sharedProfile)
                var summary = "views=" + runtime.viewCount + " profiles=" + runtime.profileCount
                    + " allocatedFive=" + sawFive + " sawRetiring=" + sawRetiring
                    + " retiredPhase=" + runtime.phase(ids.leaving)
                    + " retiredBound=" + (runtime.instance(ids.leaving).bound === true)
                    + " pendingScript=" + pending + " loading=" + loading
                    + " downloads=" + downloads + " sharedReleasable=" + releasable
                if (!sawFive || !sawRetiring || !retiredGone()) return "retirement unproven: " + summary
                if (runtime.viewCount !== 4 || runtime.profileCount !== 2
                        || Sessions.browsers().length !== 4) return "unexpected survivors: " + summary
                if (!bound(liveIds()) || !partitionsCorrect(false))
                    return "surviving engines or ordinary/private partitions invalid: " + summary
                if (pending <= 0 || !loading || downloads !== 1)
                    return "script, slow navigation or download not in flight: " + summary
                if (releasable) return "shared profile was releasable: " + summary
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
