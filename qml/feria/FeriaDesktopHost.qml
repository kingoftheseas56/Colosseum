// Copyright (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: BSD-3-Clause
// Window/geometry/close routing adapted from Qt Nano Browser's BrowserDialog
// and BrowserWindow; see NanoBrowser-LICENSE.txt. This host owns no WebContents.
import QtQuick
import QtQuick.Window

Window {
    id: host
    required property var deck
    required property string sessionId
    property bool dialogWindow: false
    property bool retiring: false
    readonly property var entry: deck.entries[sessionId] || null
    title: entry && entry.browser ? entry.browser.title : "Feria"
    flags: dialogWindow ? Qt.Dialog : Qt.Window
    width: 900; height: 700
    onClosing: function(event) {
        if (retiring) return
        event.accepted = false
        deck.requestClose(sessionId)
    }
    onActiveChanged: if (active && !retiring && entry && !entry.retiring)
        deck.sessions.switchTo(sessionId)
}
