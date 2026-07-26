// LowPoly — a slowly-morphing low-poly geometric wallpaper.
//
// The animated-geometric look lives in shaders, so this is our first shader
// wallpaper: an ORIGINAL GLSL fragment shader (shaders/lowpoly.frag, compiled to
// .qsb via Qt's `qsb`) run through a ShaderEffect. Not lifted from ShaderToy
// (those are mostly CC-BY-NC); built from standard public techniques (value-noise
// fbm + a cosine/-style constrained palette). A triangular facet grid whose flat
// per-triangle colours drift through a smooth field over time. The palette is a
// monochrome silver ramp (2026-07-25, Hemanth) — near-black through slate to cool
// silver, dark-dominant so the glass UI reads over it; recolour lives in lowpoly.frag.
//
// Freeze-gated: the FrameAnimation clock (and thus iTime) stops when `running`
// is false, so the wallpaper freezes on immersive surfaces / minimize.
import QtQuick

Item {
    id: root
    anchors.fill: parent
    property bool running: true

    FrameAnimation { id: clock; running: root.running }

    ShaderEffect {
        anchors.fill: parent
        property real iTime: clock.elapsedTime
        property size iResolution: Qt.size(root.width, root.height)
        fragmentShader: "shaders/lowpoly.frag.qsb"
    }
}
