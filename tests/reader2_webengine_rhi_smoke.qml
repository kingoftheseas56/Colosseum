// Does the EPUB reader still RENDER when the app boots on Direct3D11 for Player 2?
//
// [Agent 4 (Claude), player] — written in Agent 2's domain with Hemanth's explicit permission for
// THIS SMOKE ONLY (2026-07-26). A2 owns Biblio; this touches nothing of his, it only observes.
//
// WHY IT MATTERS: the reader draws through WebEngine, and native/main.cpp says in as many words that
// "mpvqt and WebEngine require OpenGL" while Player 2 "REFUSES to initialise on any other RHI". Qt
// picks the RHI once per process, so a Player 2 boot puts the whole app - including the reader - on
// D3D11. If WebEngine cannot composite there, then flipping Player 2 on by default would cost
// Hemanth his books to gain a video engine. Nobody had ever checked. tests/reader2_chrome_smoke.qml
// deliberately excludes WebEngine ("NO WebEngine"), so this is the first look at the real thing.
//
// HOW IT PROVES IT: load a page with known content, then grab the rendered surface to a PNG. Loading
// is the first failure mode; compositing is the second and the sneaky one - a view can report a
// perfectly successful load and still paint nothing. So the verdict carries BOTH the load result and
// the size of the grabbed image, and the run is done twice: once on OpenGL (the control, today's
// normal boot) and once on D3D11. A blank surface compresses to almost nothing next to a page of
// text, so the two numbers side by side are the answer.
//
// Run BOTH, from the repo root, and compare:
//   native\build-msvc\colosseum.exe tests\reader2_webengine_rhi_smoke.qml                  (OpenGL)
//   set COLOSSEUM_PLAYER2=1 && native\build-msvc\colosseum.exe tests\...same...            (D3D11)
import QtQuick
import QtQuick.Window
import QtWebEngine

Window {
    id: probe
    width: 900; height: 600; visible: true; color: "#101014"
    title: "reader2 WebEngine RHI smoke"

    property bool finished: false
    property string outPath: ""

    function finish(pass, message) {
        if (probe.finished)
            return
        probe.finished = true
        console.log("READER2 RHI SMOKE: " + (pass ? "PASS" : "FAIL") + " " + message)
        console.log("READER2 RHI RESULT: " + (pass ? "PASS" : "FAIL"))
        Qt.callLater(function() { Qt.exit(pass ? 0 : 1) })
    }

    WebEngineView {
        id: view
        anchors.fill: parent
        // Deliberately heavy black-on-white text: a page that renders compresses to a much larger
        // PNG than a blank surface, which is the whole discriminator here.
        url: "data:text/html," + encodeURIComponent(
            "<html><body style='background:#ffffff;color:#000000;font:28px Georgia;margin:24px'>" +
            "<h1>Call me Ishmael.</h1>" +
            "<p>Some years ago - never mind how long precisely - having little or no money in my " +
            "purse, and nothing particular to interest me on shore, I thought I would sail about a " +
            "little and see the watery part of the world.</p>" +
            "<p>It is a way I have of driving off the spleen and regulating the circulation.</p>" +
            "<p>Whenever I find myself growing grim about the mouth; whenever it is a damp, drizzly " +
            "November in my soul; then, I account it high time to get to sea as soon as I can.</p>" +
            "</body></html>")

        onLoadingChanged: function(req) {
            if (req.status === WebEngineView.LoadFailedStatus) {
                probe.finish(false, "the reader's web view FAILED TO LOAD (" + req.errorString
                                    + ") - the reader cannot work on this boot")
                return
            }
            if (req.status === WebEngineView.LoadSucceededStatus) {
                console.log("READER2 RHI SMOKE: load succeeded; grabbing the rendered surface")
                grabDelay.start()
            }
        }
    }

    // Give the compositor a few frames before grabbing: a load event is not a paint.
    Timer {
        id: grabDelay
        interval: 1200
        onTriggered: {
            var ok = view.grabToImage(function(result) {
                if (!result) {
                    probe.finish(false, "grabToImage returned nothing - nothing was composited")
                    return
                }
                var saved = result.saveToFile(probe.outPath)
                probe.finish(saved, saved
                    ? "the reader's web view LOADED and COMPOSITED; grab written to "
                      + probe.outPath + " (compare its size against the other boot)"
                    : "the surface could not be saved - treat as not composited")
            })
            if (!ok)
                probe.finish(false, "grabToImage refused outright - the view has no live surface")
        }
    }

    Timer {
        interval: 25000; running: true
        onTriggered: probe.finish(false, "TIMEOUT - the web view never resolved on this boot")
    }

    Component.onCompleted: {
        probe.outPath = (Qt.application.arguments.length > 2)
            ? Qt.application.arguments[2]
            : "artifacts/reader2-rhi-grab.png"
        console.log("READER2 RHI SMOKE: grab target = " + probe.outPath)
    }
}
