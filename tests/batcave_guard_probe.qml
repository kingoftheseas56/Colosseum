// Autonomous DLE Guard probe (throwaway spike). Implements keiyoushi's BatCave mechanism:
//   load issue URL -> site JS sets a `__guard_trust` cookie -> RELOAD -> real content (__DATA__).
// Watches the cookie, auto-reloads once when it appears, probes for the __DATA__ chapter blob,
// dumps the final HTML between markers, and self-exits (hard 45s cap so it never lingers).
// Driven directly by Bash with stdout redirected to a log (reliable; qml flushes on exit).
// __URL__ is textually replaced by the launcher.
import QtQuick
import QtQuick.Window
import QtWebEngine

Window {
    id: win; visible: true; width: 1100; height: 820
    title: "Batcave DLE Guard probe — DON'T log in; just wait/click naturally; it self-closes"

    property string targetUrl: "__URL__"
    property bool guardTrust: false
    property int reloads: 0
    property int probes: 0

    WebEngineProfile {
        id: prof
        storageName: "batcave"
        offTheRecord: false
        httpUserAgent: "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 " +
                       "(KHTML, like Gecko) Chrome/124.0 Safari/537.36"
    }
    Component.onCompleted: prof.cookieStore.loadAllCookies()

    Connections {
        target: prof.cookieStore
        function onCookieAdded(cookie) {
            var n = cookie.name.toString();
            console.log("COOKIE " + n);
            if (n === "__guard_trust") win.guardTrust = true;
        }
    }

    // When the guard cookie lands, reload ONCE to fetch content past the guard (keiyoushi's retry).
    onGuardTrustChanged: {
        if (guardTrust && reloads === 0) {
            reloads += 1;
            console.log("GUARD_TRUST_SET reloading for content");
            view.reload();
        }
    }

    WebEngineView {
        id: view; anchors.fill: parent; profile: prof; url: win.targetUrl
        onLoadingChanged: function(info) {
            if (info.status === WebEngineView.LoadSucceededStatus) settle.restart();
        }
    }

    // Probe a few seconds after each successful load.
    Timer {
        id: settle; interval: 3500; repeat: false
        onTriggered: view.runJavaScript(`(function(){
            var html = document.documentElement ? document.documentElement.innerHTML : "";
            var body = document.body ? document.body.innerText : "";
            return JSON.stringify({
                title: (document.title||"").slice(0,90),
                hasData: /window\\.__DATA__/.test(html),
                hasXfilter: /window\\.__XFILTER__/.test(html),
                path: location.pathname,
                guard: location.pathname.indexOf("/_c/") === 0,
                login: /LOG IN TO THE SITE|Your password/i.test(body),
                imgs: document.querySelectorAll("img").length,
                len: (document.documentElement ? document.documentElement.outerHTML.length : 0)
            });
        })()`, function(res){
            win.probes += 1;
            console.log("PROBE " + res);
            var r = {}; try { r = JSON.parse(res); } catch(e) {}
            if (r.hasData || win.probes >= 6) win.dumpAndExit(r.hasData ? 0 : 3);
            else settle.restart();   // keep watching (guard JS may still be working)
        });
    }

    function dumpAndExit(code) {
        view.runJavaScript("document.documentElement.outerHTML", function(html){
            console.log("===BCHTML_START===\n" + (html||"") + "\n===BCHTML_END===");
            console.log("PROBE_DONE code=" + code);
            Qt.exit(code);
        });
    }

    // Hard cap: never linger on his screen (60s gives room for a human interaction).
    Timer { interval: 60000; running: true; repeat: false
        onTriggered: { console.log("PROBE_TIMEOUT"); win.dumpAndExit(2); } }
}
