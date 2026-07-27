import QtQuick
import "Player2Browser.js" as Browser

// Paints the active subtitle cue. The C++ session owns cue timing (set on arrival, cleared by a C++
// timer after its duration), so this layer only renders what the session publishes — no QML timer
// decides when a cue is on screen. Text cues paint as a line; bitmap (PGS/DVD) cues paint as the
// decoded picture, served by the "player2subtitle" image provider and positioned by the cue's region.
Item {
    // `subs`, NOT `layer` - and the rename is not cosmetic. Every QQuickItem has a `layer` GROUPED
    // PROPERTY, and inside a Repeater delegate (its own compiled component) the delegate's own
    // property wins over an id belonging to the enclosing component. So `layer.subBorderColor` in
    // the outline delegate below silently resolved to QQuickItemLayer.subBorderColor - undefined -
    // and Qt said so eight times a construction: "Unable to assign [undefined] to QColor". Bindings
    // written outside a delegate resolved the id fine, which is why the shadowing only appeared
    // once the outline Repeater existed. Reproduced and fixed by this rename, 2026-07-27.
    id: subs

    property var session
    property QtObject theme

    // --- subtitle style (SubStyleBar's controls, chrome-port Task 4) ---
    // On the mpv boot these five are mpv's own sub-scale / sub-color / sub-border-size /
    // sub-border-color / sub-pos options and mpv redraws the burned-in subtitle. Player 2 has no
    // option surface at all, but on this boot the cue is THIS Text - so here is where those
    // controls are actually implemented; PlayerEngineP2's sub-option seam feeds these five and
    // nothing else. Nothing here can touch the mpv boot: that boot never constructs this file.
    // (The seam is named in PlayerEngineP2.qml rather than here on purpose - the shell contract
    // forbids the mpv option-setter's NAME appearing anywhere under qml/player2, and rightly so:
    // this file paints, it does not speak the engine's option language.)
    // Defaults match SubStyleBar's stored defaults (SubStyleBar.qml:21-26) so the first time a user
    // customises anything, nothing else jumps. The bar only pushes once customised, so an untouched
    // install renders on these.
    property real subScale: 1.0
    property color subColor: "#ffffff"
    property real subBorderSize: 2.0
    property color subBorderColor: "#000000"
    property int subPos: 92

    // The eight compass directions. Qt's Text.Outline has no width, so a variable outline is drawn
    // as eight offset copies of the same Text behind the real one - identical geometry and
    // wrapping, so they cannot drift out of register. subBorderSize 0 means NO outline, which is
    // what that control promises at 0.
    readonly property var outlineOffsets: [[-1,-1],[0,-1],[1,-1],[1,0],[1,1],[0,1],[-1,1],[-1,0]]

    // --- text cue ---
    Item {
        id: cueBox
        width: subs.width * 0.82
        x: (subs.width - width) / 2
        height: cueText.implicitHeight
        // mpv's sub-pos runs 0 (top of the picture) to 100 (bottom). Measured against the FREE
        // space rather than the full height, so the whole 0..100 range stays on screen instead of
        // walking the line off the top edge; 92 reproduces the fixed 8% bottom margin this layer
        // used before the control existed.
        y: (subs.height - height) * Math.max(0, Math.min(100, subs.subPos)) / 100
        visible: cueText.text.length > 0

        Repeater {
            model: subs.subBorderSize > 0 ? subs.outlineOffsets : []
            delegate: Text {
                required property var modelData
                x: modelData[0] * subs.subBorderSize
                y: modelData[1] * subs.subBorderSize
                width: cueBox.width
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                text: cueText.text
                color: subs.subBorderColor
                font: cueText.font
            }
        }

        Text {
            id: cueText
            width: cueBox.width
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            text: subs.session ? subs.session.subtitleText : ""
            color: subs.subColor
            font.family: "Segoe UI"
            font.pixelSize: Math.max(18, subs.height * 0.045) * subs.subScale
            font.weight: Font.DemiBold
        }
    }

    // --- bitmap (PGS/DVD) cue ---
    readonly property var bitmap: subs.session ? subs.session.subtitleBitmap : ({})
    readonly property bool hasBitmap: !!subs.bitmap && subs.bitmap.id !== undefined
    readonly property var bitmapBox: Browser.subtitleBitmapLayout(subs.bitmap, subs.width, subs.height)
    Image {
        visible: subs.hasBitmap && subs.bitmapBox.width > 0
        source: subs.hasBitmap ? "image://player2subtitle/" + subs.bitmap.id : ""
        x: subs.bitmapBox.x
        y: subs.bitmapBox.y
        width: subs.bitmapBox.width
        height: subs.bitmapBox.height
        fillMode: Image.Stretch   // the region already matches the picture's aspect; scale to the frame
        smooth: true
        cache: false              // each cue is a fresh id; don't retain the previous picture
        asynchronous: false       // cues are small and short-lived — show them promptly
    }
}
