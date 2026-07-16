// reader2_chrome_smoke — headless proof that the TASK 7 chrome QML tree parses and
// instantiates CLEAN, with NO WebEngine and NO bridge (ReaderChrome is bridge-free).
// A parse error, a float font.pixelSize, or an unresolved Theme singleton would fail
// the engine load (qml.exe exits non-zero) or a check below. Run:
//   qml.exe -platform offscreen tests/reader2_chrome_smoke.qml
// Verdict via console + Qt.exit. Body wrapped in try/catch (throw HANGS offscreen).
//
// [Agent 2 (Claude), biblio]
import QtQuick
import "../qml/reader2" as R

Item {
    width: 1280
    height: 720

    R.ReaderChrome {
        id: chrome
        anchors.fill: parent
        title: "Moby-Dick"
        author: "Herman Melville"
        chapterLabel: "Loomings"
        percent: 2
        pageInChapter: 4
        pagesInChapter: 18
        ticks: [0.1, 0.3, 0.6, 0.9]
        returnVisible: true
        returnPageLabel: "4"
    }

    Component.onCompleted: {
        var fails = 0
        function check(ok, what) { if (!ok) { console.log("FAIL " + what); fails++ } else console.log("ok   " + what) }
        try {
            check(String(R.Theme.gold).toLowerCase() === "#f0c24a", "Theme singleton resolves (gold)")
            check(chrome !== null, "ReaderChrome instantiated (with TopBar + BottomRail children)")
            check(chrome.awake === false, "chrome starts asleep (naked reading surface)")
            chrome.toggle()
            check(chrome.awake === true, "center-tap toggle wakes chrome")
            chrome.setPanelOpen(true)
            check(chrome.revealState.pinned === true, "setPanelOpen pins awake")
            chrome.tick()   // a tick while pinned must NOT hide
            check(chrome.awake === true, "pinned survives a tick")
            chrome.setPanelOpen(false)
            check(chrome.revealState.pinned === false, "setPanelClose unpins")
            console.log(fails ? "VERDICT: FAIL" : "VERDICT: PASS")
            Qt.exit(fails ? 1 : 0)
        } catch (e) {
            console.log("VERDICT: FAIL (threw) " + e)
            Qt.exit(1)
        }
    }
}
