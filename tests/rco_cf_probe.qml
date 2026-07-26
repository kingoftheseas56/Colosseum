// Cloudflare managed-challenge probe for readcomicsonline.ru (throwaway spike).
// THE core feasibility test: can a human clear CF's challenge inside an embedded QtWebEngine,
// or does CF flag our Chromium as automation and loop forever? Human clicks the "Verify you
// are human" checkbox; we watch (on a REPEATING timer, since the challenge suppresses the
// load-succeeded event) for cf_clearance + real content. Dumps cleared HTML, self-exits.
// Run with QT_FORCE_STDERR_LOGGING=1. __URL__ replaced by the launcher.
import QtQuick
import QtQuick.Window
import QtWebEngine

Window {
    id: win; visible: true; width: 1150; height: 850
    title: "CF probe — CLICK the 'Verify you are human' checkbox when it appears, then wait"

    property string targetUrl: "__URL__"
    property int ticks: 0
    property bool clearanceSeen: false
    property bool done: false

    WebEngineProfile {
        id: prof
        storageName: "rco"
        offTheRecord: false
        httpUserAgent: "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 " +
                       "(KHTML, like Gecko) Chrome/124.0 Safari/537.36"
    }
    Component.onCompleted: prof.cookieStore.loadAllCookies()

    Connections {
        target: prof.cookieStore
        function onCookieAdded(cookie) {
            var n = cookie.name.toString();
            if (n.indexOf("cf_clearance") >= 0 || n.indexOf("__cf") >= 0) {
                win.clearanceSeen = true;
                console.log("COOKIE " + n);
            }
        }
    }

    WebEngineView {
        id: view; anchors.fill: parent; profile: prof; url: win.targetUrl
    }

    // Probe on a REPEATING timer (every 3s) — the challenge holds the page in a loading state,
    // so we can't rely on loadingChanged. This catches the moment a human clears it.
    Timer {
        id: tick; interval: 3000; repeat: true; running: true
        onTriggered: {
            if (win.done) return;
            view.runJavaScript(`(function(){
                var title = document.title || "";
                var jam = /just a moment|checking your browser|attention required|verify you are human/i.test(title) ||
                          !!document.querySelector('#challenge-form, .cf-turnstile, #cf-challenge-running, iframe[src*="challenges.cloudflare"]');
                var comics = document.querySelectorAll('a[href*="/comic/"]').length;
                var body = document.body ? document.body.innerText.length : 0;
                return JSON.stringify({ t: title.slice(0,70), challenge: jam, comicLinks: comics, bodyLen: body });
            })()`, function(res){
                win.ticks += 1;
                console.log("TICK " + win.ticks + " " + res);
                var r = {}; try { r = JSON.parse(res); } catch(e) {}
                var cleared = (!r.challenge && (r.comicLinks > 0 || r.bodyLen > 500));
                if (cleared) win.finish(0);
            });
        }
    }

    function finish(code) {
        if (win.done) return;
        win.done = true;
        view.runJavaScript("document.documentElement.outerHTML", function(html){
            console.log("===RCOHTML_START===\n" + (html||"") + "\n===RCOHTML_END===");
            console.log("PROBE_DONE code=" + code);
            Qt.exit(code);
        });
    }

    // Generous cap so a human has time to click + CF to verify.
    Timer { interval: 120000; running: true; repeat: false
        onTriggered: { console.log("PROBE_TIMEOUT"); win.finish(2); } }
}
