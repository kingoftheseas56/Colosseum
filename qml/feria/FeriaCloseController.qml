// Copyright (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: BSD-3-Clause
import QtQuick
import QtWebEngine

// RequestClose/windowCloseRequested follows Qt Nano Browser's BSD-licensed
// originating-view handshake. Stable IDs and inline consent are Colosseum glue.
QtObject {
    id: controller
    required property var browser
    required property string sessionId
    property bool pending: false
    property var consent: null
    property var request: null
    property int serial: 0
    property int generation: 0

    function begin() {
        if (pending || !browser || browser.closingSession) return false
        pending = true
        ++generation
        browser.wakePage()
        browser.triggerWebAction(WebEngineView.RequestClose)
        return true
    }
    function capture(dialog) {
        if (dialog.type !== JavaScriptDialogRequest.DialogTypeBeforeUnload) return false
        // Forced account retirement/acknowledged close may blank a leased view.
        if (browser.closingSession) { dialog.accepted = true; dialog.dialogAccept(); return true }
        if (!pending) return false
        dialog.accepted = true
        if (request) { dialog.dialogReject(); return true }
        request = dialog
        consent = {id: sessionId + ":" + (++serial), sessionId: sessionId,
            origin: browser.browserRequests.originLabel(dialog.securityOrigin), message: dialog.message}
        return true
    }
    function answer(id, leave) {
        if (!pending || !request || !consent || consent.id !== id) return false
        var dialog = request
        request = null
        consent = null
        if (leave) dialog.dialogAccept()
        else {
            var epoch = generation
            dialog.dialogReject()
            // The renderer's rejection acknowledgement is asynchronous. Do not
            // send another RequestClose while it is unwinding the first one.
            if (!browser.evaluateJavaScript("0", function() {
                if (controller.generation === epoch) controller.pending = false
            }))
                pending = false
        }
        return true
    }
    function cancel() {
        ++generation
        var dialog = request
        request = null
        consent = null
        pending = false
        if (dialog) dialog.dialogReject()
    }
    Component.onDestruction: cancel()
}
