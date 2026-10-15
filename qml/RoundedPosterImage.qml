// RoundedPosterImage — the bounded, genuinely-rounded poster art primitive (Catalogue Poster &
// Shelf Polish, Task 2). It owns: the stable neutral placeholder, honest candidate fallback, a
// decode-size cap (never keep a texture larger than 2× the rendered poster), the ready fade, ONE
// rounded shader pass (shaders/roundedposter.frag), the inset edge, and two CHEAP offset shadow
// plates (flat rounded rectangles — NOT GPU blur). Forbidden by contract and by the static guard in
// the runner: a second render pass, any layer/ShaderEffectSource/MultiEffect, or an animated mask.
// The card above owns interaction, title, and metadata; this component knows only how to draw art.
import QtQuick

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
    // proves the source really contains one rounded ShaderEffect and no forbidden chain.
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

    // ── the decoded art: a texture provider only, never drawn directly. The rounded pass below
    //    samples it, so the art can never paint over the inset edge. ──
    Image {
        id: art
        // Automation identity (Lanista): the decode-truth surface (status/sourceSize/painted
        // size live HERE, not on the wrapper). Named only when the owner named the wrapper.
        objectName: root.objectName.length > 0 ? root.objectName + "_img" : ""
        anchors.fill: parent
        visible: false
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

    // ── the ONE rounded pass: placeholder gradient + centred aspect-crop art + rounded mask, in a
    //    single shader. It replaced a MultiEffect mask over two layer FBOs, whose creation cost
    //    ~8 ms per card (QML profile, 2026-09-29). ──
    ShaderEffect {
        id: roundedPass
        anchors.fill: parent
        readonly property real _texW: Math.max(1, art.implicitWidth)
        readonly property real _texH: Math.max(1, art.implicitHeight)
        readonly property real _texAspect: _texW / _texH
        readonly property real _itemAspect: Math.max(1, width) / Math.max(1, height)
        property var source: art
        property size itemSize: Qt.size(width, height)
        property real radius: root.radius
        property real artMix: art.status === Image.Ready ? art.opacity : 0
        property point uvScale: _texAspect > _itemAspect ? Qt.point(_itemAspect / _texAspect, 1)
                                                         : Qt.point(1, _texAspect / _itemAspect)
        property point uvOffset: Qt.point((1 - uvScale.x) / 2, (1 - uvScale.y) / 2)
        fragmentShader: "shaders/roundedposter.frag.qsb"
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
