import QtQuick
import QtQuick.Window
import "../../qml/feria"

// Exercises the website navigation controls in the host Options area against
// the local fixture server: engine history Back/Forward driven by in-page
// links, Stop on the first load and with a previous page behind it, Stop
// during a still-loading document (delayed image), a slow navigation replaced
// by a newer one, failure recovery, activity-recording independence, engine
// switching, and the Feria return action staying app-level.
Window {
    id: window
    width: 1280; height: 900; visible: true
    property int phase: 0
    property bool finished: false
    property var host: null
    property var loader: null
    property var item: null
    property var back: null
    property var forward: null
    property var reloadButton: null
    property var track: null
    property string base: ""
    property string other: ""
    property var graceMet: ({})
    function find(item, name) {
        if (item.objectName === name) return item
        for (var i = 0; i < item.children.length; ++i) {
            var found = find(item.children[i], name)
            if (found) return found
        }
        return null
    }
    // Engines settle back/forward state a beat after load completion, so every
    // wait holds true for one extra poll tick before its checks run.
    function grace(key, ok) {
        if (!ok) { graceMet[key] = false; return false }
        if (!graceMet[key]) { graceMet[key] = true; return false }
        return true
    }
    function check(ok, detail) {
        browserReporter.report("feria-browser-nav", ok, detail)
        if (!ok) finish()
        return ok
    }
    function finish() {
        if (finished) return
        finished = true; step.stop()
        Qt.callLater(function() { browserReporter.finish() })
    }
    function at(mark) { return String(host.currentUrl).indexOf(mark) >= 0 }
    function atHost(h) { return String(host.currentUrl).indexOf("://" + h + ":") >= 0 }
    function settled(mark) {
        return item && item.ready && host.loadSucceeded && at(mark) && !host.pageLoading
    }
    function state() {
        return JSON.stringify({back: item.navCanGoBack, fwd: item.navCanGoForward,
            backEnabled: back.enabled, fwdEnabled: forward.enabled,
            url: String(item.url), current: String(host.currentUrl), engine: host.engine})
    }
    // Website navigation through real in-page links — the main user path.
    function navigate(url) {
        var script = "var a=document.createElement('a');a.href='" + url + "';a.click()"
        if (smokeEngine === "webview2") item.executeScript("nav-smoke", script)
        else item.runJavaScript(script)
    }
    FeriaWorld { id: feria; anchors.fill: parent; lifecycleActive: true }
    Timer { id: deadline; interval: 90000; running: true; onTriggered: { window.check(false, "Navigation controls timed out at phase " + window.phase + (window.item ? " " + window.state() : "")); window.finish() } }
    Timer {
        id: step
        interval: 300; repeat: true; running: true
        onTriggered: {
            if (window.phase === 0) {
                window.host = window.find(feria, "feriaBrowserHost")
                if (!window.check(host, "Provider host view mounts")) return
                var first = String(smokeUrls[0])
                window.base = first.substring(0, first.lastIndexOf("/fixture/") + 9)
                window.other = window.base.replace("127.0.0.1", "localhost")
                // Becoming visible clears engineOverride, so pin the engine after openHost.
                feria.openHost("youtube", "", "home", first)
                host.engineOverride = smokeEngine
                host.showOptions()
                window.back = window.find(feria, "feriaBrowserBack")
                window.forward = window.find(feria, "feriaBrowserForward")
                window.reloadButton = window.find(feria, "feriaBrowserReload")
                window.track = window.find(feria, "feriaLoadTrack")
                if (!window.check(back && forward && reloadButton && track
                                  && window.find(feria, "feriaAppHome") && window.find(feria, "feriaBrowserReturn"),
                                  "Options area carries website navigation controls")) return
                window.phase = 1
            } else if (window.phase === 1) {
                // The provider destination itself is slow: stop the very first load.
                if (!window.item) {
                    window.loader = window.find(feria, "feriaBrowserLoader")
                    window.item = loader && loader.item
                }
                if (!item || !host.pageLoading) return
                if (!window.check(reloadButton.text === "Stop" && track.visible,
                                  "The first slow load offers Stop and shows the load indicator")) return
                reloadButton.clicked()
                window.phase = 2
            } else if (window.phase === 2) {
                if (!grace("p2", !host.pageLoading && host.loadSucceeded && !host.errorText.length)) return
                if (!window.check(host.errorText === "", "Stopping the first load reports no open failure")) return
                if (!window.check(reloadButton.text === "Reload" && !track.visible,
                                  "After the first Stop the shell settles idle")) return
                if (!window.check(!item.navCanGoBack && !back.enabled && !item.navCanGoForward && !forward.enabled,
                                  "Back and Forward stay disabled with nothing committed yet " + window.state())) return
                item.url = base + "page?a"
                window.phase = 3
            } else if (window.phase === 3) {
                if (!grace("p3", settled("?a"))) return
                FeriaAccount.setRecording(true)
                window.navigate(base + "page?b")
                window.phase = 4
            } else if (window.phase === 4) {
                if (!grace("p4", settled("?b") && item.navCanGoBack && !item.navCanGoForward)) return
                if (!window.check(item.navCanGoBack && back.enabled && !item.navCanGoForward && !forward.enabled,
                                  "After an in-page link A→B Back is enabled and Forward disabled " + window.state())) return
                window.navigate(base + "page?c")
                window.phase = 5
            } else if (window.phase === 5) {
                if (!grace("p5", settled("?c") && item.navCanGoBack && !item.navCanGoForward)) return
                if (!window.check(item.navCanGoBack && !item.navCanGoForward,
                                  "After A→B→C Forward stays disabled at the newest entry " + window.state())) return
                back.clicked()
                window.phase = 6
            } else if (window.phase === 6) {
                if (!grace("p6", settled("?b") && item.navCanGoBack && item.navCanGoForward)) return
                if (!window.check(item.navCanGoBack && item.navCanGoForward && forward.enabled,
                                  "Back from C to B keeps Back and enables Forward " + window.state())) return
                back.clicked()
                window.phase = 7
            } else if (window.phase === 7) {
                // Whether the stopped first load leaves an initial blank entry
                // behind A is engine-timing dependent, so only Forward — which
                // both engines guarantee here — is asserted at the oldest page.
                if (!grace("p7", settled("?a") && item.navCanGoForward)) return
                if (!window.check(item.navCanGoForward && forward.enabled,
                                  "Back to A keeps Forward available " + window.state())) return
                forward.clicked()
                window.phase = 8
            } else if (window.phase === 8) {
                if (!grace("p8", settled("?b") && item.navCanGoBack && item.navCanGoForward)) return
                if (!window.check(item.navCanGoBack && item.navCanGoForward,
                                  "Forward from A to B keeps the remaining forward entries " + window.state())) return
                window.navigate(base + "page?d")
                window.phase = 9
            } else if (window.phase === 9) {
                if (!grace("p9", settled("?d") && item.navCanGoBack && !item.navCanGoForward)) return
                if (!window.check(item.navCanGoBack && !item.navCanGoForward && !forward.enabled,
                                  "A new navigation after Back clears the forward history " + window.state())) return
                window.navigate(base + "slow")
                window.phase = 10
            } else if (window.phase === 10) {
                if (!host.pageLoading) return
                if (!window.check(reloadButton.text === "Stop" && track.visible,
                                  "A slow load with a page behind it still offers Stop")) return
                reloadButton.clicked()
                window.phase = 11
            } else if (window.phase === 11) {
                if (!grace("p11", !host.pageLoading && host.loadSucceeded && !host.errorText.length)) return
                if (!window.check(host.errorText === "" && reloadButton.text === "Reload" && item.navCanGoBack,
                                  "Stopping with a previous page behind reports no failure and keeps history")) return
                window.navigate(base + "slow-image")
                window.phase = 12
            } else if (window.phase === 12) {
                // The document is committed and interactive while the delayed
                // image still loads: Stop must remain available until the
                // navigation itself ends.
                if (!host.pageLoading) return
                if (smokeEngine === "webview2" && !host.loadSucceeded) return
                if (!window.check(host.pageLoading && track.visible && reloadButton.text === "Stop",
                                  "Stop and the load indicator outlive document readiness " + window.state())) return
                if (!window.check(smokeEngine !== "webview2" || host.loadSucceeded,
                                  "WebView2 completes the document early while subresources still load")) return
                reloadButton.clicked()
                window.phase = 13
            } else if (window.phase === 13) {
                if (!grace("p13", !host.pageLoading && host.loadSucceeded && !host.errorText.length)) return
                if (!window.check(host.errorText === "" && item.navCanGoBack,
                                  "Stopping the still-loading document reports no failure")) return
                window.navigate(base + "slow")
                window.phase = 14
            } else if (window.phase === 14) {
                if (!host.pageLoading) return
                window.navigate(base + "page?g")
                window.phase = 15
            } else if (window.phase === 15) {
                if (!grace("p15", settled("?g"))) return
                if (!window.check(host.errorText === "" && item.navCanGoBack && !item.navCanGoForward,
                                  "A navigation replacing a slow load wins cleanly " + window.state())) return
                // The second fixture origin: cross-origin link coverage.
                window.navigate(other + "page?x")
                window.phase = 16
            } else if (window.phase === 16) {
                if (!grace("p16", settled("page?x") && atHost("localhost") && item.navCanGoBack)) return
                if (!window.check(item.navCanGoBack && back.enabled,
                                  "Crossing origins by link keeps Back enabled " + window.state())) return
                back.clicked()
                window.phase = 17
            } else if (window.phase === 17) {
                if (!grace("p17", settled("?g") && atHost("127.0.0.1") && item.navCanGoForward)) return
                if (!window.check(item.navCanGoForward && forward.enabled,
                                  "Returning across origins enables Forward " + window.state())) return
                forward.clicked()
                window.phase = 18
            } else if (window.phase === 18) {
                if (!grace("p18", settled("page?x") && atHost("localhost") && item.navCanGoBack)) return
                if (!window.check(item.navCanGoBack,
                                  "Forward across origins keeps Back enabled " + window.state())) return
                window.navigate(base + "redirect")
                window.phase = 19
            } else if (window.phase === 19) {
                if (!grace("p19", settled("/fixture/page") && atHost("localhost") && !at("?") && item.navCanGoBack)) return
                if (!window.check(item.navCanGoBack && back.enabled,
                                  "A redirect across origins keeps Back enabled " + window.state())) return
                back.clicked()
                window.phase = 20
            } else if (window.phase === 20) {
                // One Back step from the redirect target lands on the page the
                // redirect started from (same origin here), not the origin before it.
                if (!grace("p20", settled("page?x") && atHost("localhost") && item.navCanGoForward)) return
                if (!window.check(item.navCanGoForward && forward.enabled,
                                  "Returning from a cross-origin redirect enables Forward " + window.state())) return
                window.navigate("http://127.0.0.1:9/fixture/page")
                window.phase = 21
            } else if (window.phase === 21) {
                if (!host.errorText.length) return
                if (!window.check(!host.loadSucceeded && reloadButton.enabled,
                                  "A dead destination reports failure while controls stay usable")) return
                // The failed load leaves an error document behind; recovering is
                // app-initiated navigation (the App home path), not a page link.
                item.url = base + "page?e"
                window.phase = 22
            } else if (window.phase === 22) {
                if (!grace("p22", settled("?e") && !host.errorText.length && item.navCanGoBack)) return
                if (!window.check(host.errorText === "" && host.loadSucceeded && item.navCanGoBack && back.enabled,
                                  "A successful navigation after failure clears the error and keeps Back " + window.state())) return
                FeriaAccount.setRecording(false)
                window.navigate(base + "page?f")
                window.phase = 23
            } else if (window.phase === 23) {
                if (!grace("p23", settled("?f"))) return
                if (!window.check(item.navCanGoBack && !track.visible,
                                  "Navigation state updates with activity recording disabled")) return
                back.clicked()
                window.phase = 24
            } else if (window.phase === 24) {
                if (!grace("p24", settled("?e") && item.navCanGoForward)) return
                if (!window.check(item.navCanGoForward && forward.enabled,
                                  "Back works with activity recording disabled " + window.state())) return
                forward.clicked()
                window.phase = 25
            } else if (window.phase === 25) {
                if (!grace("p25", settled("?f"))) return
                window.find(feria, "feriaBrowserSwitch").clicked()
                window.phase = 26
            } else if (window.phase === 26) {
                var fresh = loader.item
                if (!grace("p26", fresh && fresh !== item && fresh.ready && host.loadSucceeded && !host.pageLoading && !fresh.navCanGoBack)) return
                var swapped = smokeEngine === "webview2" ? "qtwebengine" : "webview2"
                if (!window.check(host.engine === swapped, "Compatibility mode switches the engine")) return
                if (!window.check(!fresh.navCanGoBack && !back.enabled,
                                  "A recreated engine starts with empty history state")) return
                window.item = fresh
                host.showOptions()
                window.find(feria, "feriaAppHome").clicked()
                window.phase = 27
            } else if (window.phase === 27) {
                if (!window.check(String(item.url).indexOf("https://") === 0,
                                  "App home returns to the provider destination")) return
                window.find(feria, "feriaBrowserReturn").triggered()
                window.phase = 28
            } else if (window.phase === 28) {
                if (!window.check(feria.viewState === "home",
                                  "Feria returns through application navigation, not engine history")) return
                window.finish()
            }
        }
    }
}
