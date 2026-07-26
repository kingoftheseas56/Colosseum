// ComicHubFree image-resolution probe (throwaway). No Cloudflare, no login here — the only
// gate is that page images 404 to a bare curl (anti-hotlink; needs the page's JS/session).
// Test: load the open reading page in QtWebEngine, then have the BROWSER fetch page 1 in-page
// (full session context). If it returns image bytes (not 404 HTML), the browser-backed path
// works for this source. Autonomous; self-exits. Run with QT_FORCE_STDERR_LOGGING=1.
import QtQuick
import QtQuick.Window
import QtWebEngine

Window {
    id: win; visible: true; width: 900; height: 700
    title: "ComicHubFree image probe (autonomous)"

    property string readUrl: "https://comichubfree.com/absolute-catwoman/issue-1/all"
    property string imgUrl: "https://comichubfree.com/absolute-catwoman/issue-1/302873/1.jpg"
    property bool done: false

    WebEngineProfile { id: prof; storageName: "comichub"; offTheRecord: false
        httpUserAgent: "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 " +
                       "(KHTML, like Gecko) Chrome/124.0 Safari/537.36" }

    WebEngineView {
        id: view; anchors.fill: parent; profile: prof; url: win.readUrl
        onLoadingChanged: function(info) {
            if (info.status === WebEngineView.LoadSucceededStatus && !win.done) probe.restart();
        }
    }

    // Phase 1: fire the in-page fetch into a global (runJavaScript can't await a Promise).
    Timer { id: probe; interval: 2500; repeat: false; onTriggered: {
        view.runJavaScript(`(function(){
            window.__r = { pending: true };
            fetch("${win.imgUrl}", { headers: { "Accept": "image/*" } })
              .then(function(r){ return r.arrayBuffer().then(function(b){ return { r: r, b: b }; }); })
              .then(function(o){
                  var buf = new Uint8Array(o.b);
                  var hex = [buf[0],buf[1],buf[2]].map(function(x){ return (x||0).toString(16).padStart(2,'0'); }).join('');
                  window.__r = { status: o.r.status, ct: o.r.headers.get("content-type"), bytes: buf.length, magic: hex };
              })
              .catch(function(e){ window.__r = { error: String(e) }; });
            return "fired";
        })()`, function(){ readback.restart(); });
    } }

    // Phase 2: read the resolved global back.
    Timer { id: readback; interval: 3500; repeat: false; onTriggered: {
        view.runJavaScript("JSON.stringify(window.__r || {none:true})", function(res){
            console.log("IMGFETCH " + res);
            var ok = /"magic":"ffd8ff"/.test(res) || /"magic":"89504e"/.test(res);
            console.log(ok ? "RESULT browser-fetch = REAL IMAGE" : "RESULT browser-fetch = NOT an image");
            win.done = true; Qt.exit(ok ? 0 : 3);
        });
    } }

    Timer { interval: 25000; running: true; repeat: false
        onTriggered: { if(!win.done){ console.log("PROBE_TIMEOUT"); Qt.exit(2); } } }
}
