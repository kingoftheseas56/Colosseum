// RoundedPosterImage — the bounded, genuinely-rounded poster art primitive (Catalogue Poster &
// Shelf Polish, Task 2). It owns: the stable neutral placeholder, honest candidate fallback, a
// decode-size cap (never keep a texture larger than 2× the rendered poster), the ready fade, ONE
// rounded MultiEffect mask pass, the inset edge, and two CHEAP offset shadow plates (flat rounded
// rectangles — NOT GPU blur). Forbidden by contract and by the static guard in the runner: a
// second MultiEffect, any ShaderEffectSource, MultiEffect blur/shadow, or an animated mask.
// The card above owns interaction, title, and metadata; this component knows only how to draw art.
import QtQuick
import QtQuick.Effects

Item {
    id: root

    // ── inputs ──
    property var sources: []                 // ordered candidate URLs from PosterSourcePolicy
    property real radius: 12
    property int revealDuration: 280
    property bool hovered: false             // drives the cheap depth plates + inset edge accent
    // 0 → use the live Screen.devicePixelRatio; a positive value is a deterministic harness override.
    property real testDevicePixelRatio: 0

    // ── bounded decode (design §5.2): clamp(dpr, 1, 2) × rendered geometry ──
    readonly property real effectiveScale: Math.max(1, Math.min(2,
        testDevicePixelRatio > 0 ? testDevicePixelRatio : Screen.devicePixelRatio))
    readonly property int decodeWidth: Math.ceil(width * effectiveScale)
    readonly property int decodeHeight: Math.ceil(height * effectiveScale)

    // ── candidate fallback state machine ──
    property int candidateIndex: 0
    property bool _exhausted: false
    readonly property bool exhausted: _exhausted || !(root.sources && root.sources.length > 0)
    readonly property url activeSource: (root.sources && candidateIndex >= 0
                                         && candidateIndex < root.sources.length)
                                        ? root.sources[candidateIndex] : ""
    readonly property bool ready: art.status === Image.Ready
    // the placeholder is the visible surface whenever real art is not shown (loading OR exhausted) —
    // an exhausted card keeps the stable placeholder, never a broken-image icon or a transparent hole.
    readonly property bool placeholderVisible: !ready
    // contract marker: this renderer uses exactly one rounded mask pass. The runner statically
    // proves the source really contains one MultiEffect and no forbidden chain.
    readonly property int maskPassCount: 1

    onSourcesChanged: { candidateIndex = 0; _exhausted = false; }

    // advance to the next candidate on failure; returns false (and marks exhausted) at the last one.
    // Never wraps back to zero. Production calls this once per Image.Error; harnesses call it directly.
    function advanceCandidate() {
        if (root.sources && candidateIndex < root.sources.length - 1) {
            candidateIndex += 1;
            return true;
        }
        _exhausted = true;
        return false;
    }

    // ── two cheap offset shadow plates behind the art (flat rounded rects; no blur, no FBO) ──
    Rectangle {
        x: 0; y: 3; width: root.width; height: root.height
        radius: root.radius + 1
        color: Qt.rgba(0, 0, 0, root.hovered ? 0.42 : 0.28)
        Behavior on color { ColorAnimation { duration: 260 } }
    }
    Rectangle {
        x: -2; y: root.hovered ? 11 : 7; width: root.width + 4; height: root.height
        radius: root.radius + 3
        color: Qt.rgba(0, 0, 0, root.hovered ? 0.20 : 0.10)
        Behavior on y { NumberAnimation { duration: 260; easing.type: Easing.OutCubic } }
        Behavior on color { ColorAnimation { duration: 260 } }
    }

    // ── the content that gets rounded: stable placeholder + the decoded art. Rendered ONLY through
    //    the single MultiEffect mask (layer.effect), so the 12px crop is genuine and the art can
    //    never paint over the inset edge. ──
    Item {
        id: content
        anchors.fill: parent
        layer.enabled: true
        layer.effect: MultiEffect {
            maskEnabled: true
            maskSource: maskShape
            maskThresholdMin: 0.5      // crisp 50% cutoff on the AA'd rounded-rect mask texture
        }

        Rectangle {                    // stable neutral placeholder (design §4.2)
            anchors.fill: parent
            gradient: Gradient {
                GradientStop { position: 0.0; color: "#191b21" }
                GradientStop { position: 0.5; color: "#101218" }
                GradientStop { position: 1.0; color: "#17171b" }
            }
        }
        Image {
            id: art
            anchors.fill: parent
            source: root.activeSource
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            cache: true
            smooth: true
            mipmap: true               // mipmap only on the BOUNDED decoded image, never an unbounded original
            sourceSize.width: root.decodeWidth
            sourceSize.height: root.decodeHeight
            opacity: status === Image.Ready ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: root.revealDuration; easing.type: Easing.OutCubic } }
            // exactly once per failed candidate; exhaustion stops the walk (no retry loop).
            onStatusChanged: if (status === Image.Error) root.advanceCandidate()
        }
    }

    // ── stable rounded mask source (no animation) — a texture provider, not drawn directly ──
    Item {
        id: maskShape
        anchors.fill: parent
        layer.enabled: true
        visible: false
        Rectangle { anchors.fill: parent; radius: root.radius; color: "black" }
    }

    // ── inset edge, painted ABOVE the masked art: 1px white 8% at rest, 2px soft gold on hover ──
    Rectangle {
        anchors.fill: parent
        radius: root.radius
        color: "transparent"
        border.width: root.hovered ? 2 : 1
        border.color: root.hovered ? Qt.rgba(240 / 255, 196 / 255, 74 / 255, 0.55)
                                   : Qt.rgba(1, 1, 1, 0.08)
        Behavior on border.color { ColorAnimation { duration: 220 } }
    }
}
