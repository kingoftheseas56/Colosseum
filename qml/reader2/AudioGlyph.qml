// AudioGlyph.qml — the reader's audiobook-transport glyphs, HAND-DRAWN in QML Canvas.
//
// Ported from the video player's IconGlyph (PlayerPage.qml — A4's "forged-line" family,
// mock-ratified 2026-07-08): one consistent heavy stroke across every glyph, drawn with
// the same coordinate system (fractions of the icon box around its center), so the
// reader's pill and the player's control bar speak one visual language. Hemanth's call
// 2026-07-18: the SVG-file skip icons never rendered right — "use the symbols from the
// video player." Two reader-only kinds (speed gauge, playlist) are drawn in the same
// family rules.
//
// [Agent 2 (Claude), biblio]
import QtQuick

Canvas {
    id: glyph
    property string kind: ""
    property string label: ""            // seekBack/seekForward center text ("10")
    property color ink: Theme.ink
    antialiasing: true
    onKindChanged: requestPaint()
    onLabelChanged: requestPaint()
    onInkChanged: requestPaint()
    Component.onCompleted: requestPaint()
    onPaint: {
        var ctx = getContext("2d")
        var w = width
        var h = height
        var s = Math.min(w, h)
        var cx = w / 2
        var cy = h / 2
        ctx.clearRect(0, 0, w, h)
        ctx.strokeStyle = ink
        ctx.fillStyle = ink
        ctx.lineWidth = Math.max(2.0, s / 13)   // forged-line weight (player parity)
        ctx.lineCap = "round"
        ctx.lineJoin = "round"

        function line(x1, y1, x2, y2) {
            ctx.beginPath()
            ctx.moveTo(cx + x1 * s, cy + y1 * s)
            ctx.lineTo(cx + x2 * s, cy + y2 * s)
            ctx.stroke()
        }
        function circleArc(r, a1, a2, ccw) {
            ctx.beginPath()
            ctx.arc(cx, cy, r * s, a1 * Math.PI / 180, a2 * Math.PI / 180, ccw)
            ctx.stroke()
        }

        if (kind === "play") {
            // forged-line: outlined triangle, rounded joins
            ctx.beginPath()
            ctx.moveTo(cx - 0.11 * s, cy - 0.21 * s)
            ctx.lineTo(cx - 0.11 * s, cy + 0.21 * s)
            ctx.lineTo(cx + 0.23 * s, cy)
            ctx.closePath()
            ctx.stroke()
        } else if (kind === "pause") {
            // forged-line: two thick round-cap strokes
            ctx.lineWidth = Math.max(2.6, s / 10)
            line(-0.11, -0.19, -0.11, 0.19)
            line(0.11, -0.19, 0.11, 0.19)
        } else if (kind === "seekBack" || kind === "seekForward") {
            // Forged-line loop: near-full arc with a clear arrowhead into the gap.
            // Forward is drawn; back is the same drawing mirrored (matched pair).
            var fwd = kind === "seekForward"
            ctx.save()
            if (!fwd) { ctx.translate(w, 0); ctx.scale(-1, 1) }
            circleArc(0.31, -60, 235, false)
            line(-0.135, -0.28, -0.29, -0.315)
            line(-0.135, -0.28, -0.215, -0.135)
            ctx.restore()
            ctx.font = "600 " + Math.round(s * 0.21) + "px " + Theme.ui
            ctx.textAlign = "center"
            ctx.textBaseline = "middle"
            ctx.fillText(label, cx, cy + s * 0.03)
        } else if (kind === "nextChapter" || kind === "prevChapter") {
            // matched mirrored pair: outlined triangle + bar (player's episode-skip pair)
            var m = kind === "prevChapter" ? -1 : 1
            ctx.beginPath()
            ctx.moveTo(cx - 0.20 * m * s, cy - 0.19 * s)
            ctx.lineTo(cx - 0.20 * m * s, cy + 0.19 * s)
            ctx.lineTo(cx + 0.14 * m * s, cy)
            ctx.closePath()
            ctx.stroke()
            line(0.24 * m, -0.19, 0.24 * m, 0.19)
        } else if (kind === "speed") {
            // reader-only, family rules: a gauge — open arc over the top + a needle
            circleArc(0.30, 150, 390, false)
            line(0, 0.10, 0.17, -0.13)
        } else if (kind === "volume" || kind === "mute") {
            // reader-only, family rules: outlined speaker wedge; waves when live, cross when muted
            ctx.beginPath()
            ctx.moveTo(cx - 0.28 * s, cy - 0.09 * s)
            ctx.lineTo(cx - 0.15 * s, cy - 0.09 * s)
            ctx.lineTo(cx - 0.02 * s, cy - 0.21 * s)
            ctx.lineTo(cx - 0.02 * s, cy + 0.21 * s)
            ctx.lineTo(cx - 0.15 * s, cy + 0.09 * s)
            ctx.lineTo(cx - 0.28 * s, cy + 0.09 * s)
            ctx.closePath()
            ctx.stroke()
            if (kind === "volume") {
                ctx.beginPath()
                ctx.arc(cx + 0.03 * s, cy, 0.12 * s, -50 * Math.PI / 180, 50 * Math.PI / 180, false)
                ctx.stroke()
                ctx.beginPath()
                ctx.arc(cx + 0.03 * s, cy, 0.23 * s, -50 * Math.PI / 180, 50 * Math.PI / 180, false)
                ctx.stroke()
            } else {
                line(0.09, -0.09, 0.27, 0.09)
                line(0.27, -0.09, 0.09, 0.09)
            }
        } else if (kind === "playlist") {
            // reader-only, family rules: three list strokes + an outlined play wedge
            line(-0.28, -0.20, 0.10, -0.20)
            line(-0.28, 0.00, 0.10, 0.00)
            line(-0.28, 0.20, -0.06, 0.20)
            ctx.beginPath()
            ctx.moveTo(cx + 0.14 * s, cy + 0.06 * s)
            ctx.lineTo(cx + 0.14 * s, cy + 0.30 * s)
            ctx.lineTo(cx + 0.30 * s, cy + 0.18 * s)
            ctx.closePath()
            ctx.stroke()
        }
    }
}
