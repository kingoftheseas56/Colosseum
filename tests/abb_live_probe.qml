// abb_live_probe.qml — IN-PROCESS truth for the ABB delivery chain (house doctrine:
// curl lies; only the Qt network stack's own result counts). Runs the REAL AbbApi.js
// fetchInfoHash for the known Joe Country slug through QML's XMLHttpRequest — exactly
// what BiblioBook's download tap does. Run:
//   qml.exe -platform offscreen tests/abb_live_probe.qml
// VERDICT: PASS (hash resolved) / FAIL (null → the in-app fetch is blocked) + exit code.
// NETWORK-DEPENDENT — a live probe for triage, not part of the deterministic suite.
import QtQuick
import "../qml/AbbApi.js" as Abb

Item {
    Component.onCompleted: {
        Abb.fetchInfoHash("joe-icountry-mick-herron", function(d) {
            console.log("RESULT: " + JSON.stringify(d))
            console.log((d && d.infoHash) ? "VERDICT: PASS" : "VERDICT: FAIL")
            Qt.exit((d && d.infoHash) ? 0 : 1)
        })
    }
    Timer {
        interval: 20000; running: true
        onTriggered: { console.log("VERDICT: FAIL (20s timeout — no response)"); Qt.exit(2) }
    }
}
