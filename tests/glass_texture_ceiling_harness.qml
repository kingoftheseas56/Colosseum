// Offscreen contract for Glass's GPU texture ceiling.
//
// Glass blurs by allocating TWO textures the size of the WHOLE item (a
// ShaderEffectSource grab of the backdrop, and a layer.enabled rounded-rect mask).
// Ask the driver for one bigger than it allows and it does not degrade — it refuses,
// and the panel renders as garbage:
//
//   QSGRhiLayer: Unsupported size requested: [1758, 54375]. Maximum texture size: 16384
//
// Hit live on 2026-07-30: a 232-chapter list inside a Glass card, ~54,000px tall.
// Any content-sized Glass can exceed any ceiling, so past a safe bound Glass must
// stop allocating and fall back to the tint/scrim/border it always draws.
//
// This pins the DECISION (blurAffordable). The visual fallback is the rectangles
// below it in Glass.qml, which render unconditionally either way.
//
// Verdict rides the sentinel + exit code: a thrown QML error HANGS qml.exe
// offscreen, so every check is wrapped in try/catch -> Qt.exit(1).
import QtQuick

Item {
    id: harness
    width: 640; height: 480
    visible: false

    // Something for Glass to blur. Never actually blurred offscreen; Glass only
    // needs a live Item to map its origin into.
    Rectangle { id: backdropItem; anchors.fill: parent; color: "#202020" }

    property var comp: null
    property var made: []

    function ck(cond, msg) { if (!cond) throw new Error(msg) }

    function makeGlass(w, h) {
        var g = harness.comp.createObject(harness, {
            "backdrop": backdropItem, "width": w, "height": h
        })
        if (!g) throw new Error("Glass createObject returned null (" + w + "x" + h + ")")
        harness.made.push(g)
        return g
    }

    function runChecks() {
        try {
            harness.comp = Qt.createComponent("../qml/Glass.qml")
            if (harness.comp.status === Component.Error)
                throw new Error("component: " + harness.comp.errorString())

            // 1. An ordinary chrome panel still blurs — the fallback must not cost
            //    every existing caller its material.
            ck(makeGlass(880, 360).blurAffordable,
               "an ordinary panel must still blur")

            // 2. A tall content-sized card does NOT. This is the live failure: the
            //    chapter table grew with the chapter count and blew the ceiling.
            ck(!makeGlass(1758, 54375).blurAffordable,
               "the 232-chapter card that broke the GPU must NOT blur")

            // 3. Width is bounded too — a wide panel is the same allocation problem
            //    rotated 90 degrees.
            ck(!makeGlass(20000, 300).blurAffordable,
               "an over-wide panel must not blur either")

            // 4. The bound is inclusive at 8192 and refuses one pixel past it, so a
            //    caller sitting exactly on the documented limit keeps its blur.
            ck(makeGlass(8192, 8192).blurAffordable,
               "exactly 8192 square must still blur")
            ck(!makeGlass(8192, 8193).blurAffordable,
               "one pixel past the bound must not blur")

            // 5. A degenerate (unsized) Glass must not claim the blur is affordable —
            //    it has nothing to allocate and no origin to map yet.
            ck(!makeGlass(0, 0).blurAffordable,
               "an unsized Glass must not claim an affordable blur")

            // 6. The decision TRACKS size: a card that grows past the bound at runtime
            //    (a chapter list filling in) must drop its blur, not keep a stale one.
            var g = makeGlass(880, 400)
            ck(g.blurAffordable, "precondition: the small card blurs")
            g.height = 54375
            ck(!g.blurAffordable, "growing past the bound must DROP the blur")
            g.height = 400
            ck(g.blurAffordable, "shrinking back must restore it")

            console.log("GLASS_TEXTURE_CEILING_OK")
            Qt.exit(0)
        } catch (e) {
            console.log("GLASS_TEXTURE_CEILING_FAIL: " + e.message)
            Qt.exit(1)
        }
    }

    Component.onCompleted: Qt.callLater(runChecks)

    // Safety net: never spin forever offscreen.
    Timer {
        interval: 6000; running: true
        onTriggered: { console.log("GLASS_TEXTURE_CEILING_FAIL timeout"); Qt.exit(1) }
    }
}
