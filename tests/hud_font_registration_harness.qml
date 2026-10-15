// HUD font registration gate (born of the 2026-07-08 font debug).
// QML never errors on an unknown font family — it silently falls back (to Tahoma on this
// box), which is exactly how "the new font isn't rendering" ships invisibly. This loads
// the bundled faces the way Main.qml does and asserts each family RESOLVES TO ITSELF via
// Text.fontInfo (the post-matching truth), at the weights the HUD actually uses.
// MUST run on the real windows platform: the offscreen platform's thin font database
// soft-matches near names (probe-proven) and would hide the fallback this gate exists
// to catch. Verdict rides the exit code.
import QtQuick
import QtQuick.Window

Window {
    id: win
    visible: true
    width: 200; height: 100

    FontLoader { source: "../assets/fonts/Switzer-Regular.otf" }
    FontLoader { source: "../assets/fonts/Switzer-Medium.otf" }
    FontLoader { source: "../assets/fonts/Switzer-Semibold.otf" }
    FontLoader { source: "../assets/fonts/Switzer-Bold.otf" }
    FontLoader { source: "../assets/fonts/Inter-Regular.otf" }
    FontLoader { source: "../assets/fonts/Inter-Medium.otf" }
    FontLoader { source: "../assets/fonts/Inter-SemiBold.otf" }
    FontLoader { source: "../assets/fonts/Inter-Bold.otf" }
    FontLoader { source: "../assets/fonts/Fraunces-Regular.ttf" }

    Text { id: sw400; text: "x"; font.family: "Switzer" }
    Text { id: sw500; text: "x"; font.family: "Switzer"; font.weight: 500 }
    Text { id: sw600; text: "x"; font.family: "Switzer"; font.weight: 600 }
    Text { id: sw700; text: "x"; font.family: "Switzer"; font.weight: 700 }
    Text { id: in400; text: "x"; font.family: "Inter" }
    Text { id: in500; text: "x"; font.family: "Inter"; font.weight: 500 }
    Text { id: in600; text: "x"; font.family: "Inter"; font.weight: 600 }
    Text { id: in700; text: "x"; font.family: "Inter"; font.weight: 700 }
    Text { id: fr400; text: "x"; font.family: "Fraunces" }

    Timer {
        interval: 1500; running: true
        onTriggered: {
            try {
                function assertFace(label, t, family) {
                    if (t.fontInfo.family !== family)
                        throw new Error(label + " resolved to '" + t.fontInfo.family +
                                        "' — silent fallback, the face is NOT rendering.")
                }
                assertFace("Switzer w400", sw400, "Switzer")
                assertFace("Switzer w500", sw500, "Switzer")
                assertFace("Switzer w600", sw600, "Switzer")
                assertFace("Switzer w700", sw700, "Switzer")
                assertFace("Inter w400", in400, "Inter")
                assertFace("Inter w500", in500, "Inter")
                assertFace("Inter w600", in600, "Inter")
                assertFace("Inter w700", in700, "Inter")
                assertFace("Fraunces w400", fr400, "Fraunces")
                console.log("[diag] HUD FONTS OK — all families resolve to themselves")
                Qt.exit(0)
            } catch (e) {
                console.log("[diag] HUD FONT GATE FAIL: " + e.message)
                Qt.exit(2)
            }
        }
    }
}
