// NoirFlow — a slowly-flowing monochrome shader wallpaper (2026-07-25, Hemanth).
//
// Our second original shader wallpaper (after LowPoly): a GLSL fragment shader
// (shaders/noirflow.frag, compiled to .qsb via Qt's `qsb`) run through a ShaderEffect.
// Domain-warped value-noise (Inigo-Quilez-style fbm warp) makes silver light currents
// drift through deep black — dark-dominant, no colour, so the glass UI + gold accents
// read cleanly over it. Built from standard public technique, fully ours (not ShaderToy).
//
// Freeze-gated: the FrameAnimation clock (and thus iTime) stops when `running` is false,
// so the wallpaper freezes on immersive surfaces / minimize (motion doctrine).
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
        fragmentShader: "shaders/noirflow.frag.qsb"
    }
}
