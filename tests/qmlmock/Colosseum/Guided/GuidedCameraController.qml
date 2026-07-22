// TEST-ONLY QML mock of the native guided::GuidedCameraController.
//
// It mirrors the C++ type's property/method/signal surface faithfully enough for the
// MangaReader integration harness to drive Guided under plain qml.exe (no build). The
// REAL controller's state machine + timing are proven by tests/guided_camera_controller_harness.cpp
// (Task 8); this mock only lets the reader wire against a matching API. It lives under
// tests/qmlmock and is only ever on a harness -I import path, never the app's.

import QtQuick

Item {
    id: ctl

    property rect cameraRect: Qt.rect(0, 0, 1, 1)
    property int transitionMs: 350
    property int stepIndex: 0
    property bool autoRead: false
    property bool interrupted: false
    property real speed: 1.0
    property int stopAnimationGeneration: 0
    property int canvasIndex: 0

    signal requestNextCanvas()
    signal requestPreviousCanvas()

    property var _steps: []

    function _applyStep() {
        if (_steps.length > 0 && stepIndex >= 0 && stepIndex < _steps.length) {
            var c = _steps[stepIndex].camera
            if (c) cameraRect = Qt.rect(c.x, c.y, c.w, c.h)
        }
    }

    function setPath(serializedPath, preferredStep) {
        _steps = (serializedPath && serializedPath.steps) ? serializedPath.steps : []
        var n = _steps.length
        stepIndex = n > 0 ? Math.max(0, Math.min(preferredStep || 0, n - 1)) : 0
        autoRead = false
        interrupted = false
        _applyStep()
    }
    function advance() {
        if (_steps.length === 0) return
        if (stepIndex < _steps.length - 1) { stepIndex += 1; _applyStep() }
        else requestNextCanvas()
    }
    function retreat() {
        if (stepIndex > 0) { stepIndex -= 1; _applyStep() }
        else requestPreviousCanvas()
    }
    function startAutoRead() { interrupted = false; autoRead = true }
    function pauseAutoRead() { autoRead = false }
    function interrupt(reason, viewportCenter) {
        autoRead = false
        interrupted = true
        stopAnimationGeneration += 1
    }
    function resumeAutoRead() { interrupted = false; autoRead = true }
    function sessionState() {
        return { canvasIndex: canvasIndex, stepIndex: stepIndex,
                 submode: autoRead ? "auto" : "step", speed: speed, guided: true }
    }
    function restoreSession(state) {
        if (!state) return
        if (state.stepIndex !== undefined) stepIndex = state.stepIndex
        if (state.speed !== undefined) speed = state.speed
        autoRead = false   // restored Auto Read is always paused
    }
}
