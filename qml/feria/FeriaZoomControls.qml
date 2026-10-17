// Copyright (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: BSD-3-Clause
// Native zoomFactor wiring adapted from Nano Browser BrowserWindow.qml,
// c8e817ac60146921c9d73de2e420e1445c9cf513. See NanoBrowser-LICENSE.txt.
import QtQuick
import QtQuick.Layouts
import "../account" as AccountUi
import ".." as Colosseum

RowLayout {
    id: root
    objectName: "feriaZoomControls"
    property var tools: null
    spacing: 5
    height: 36
    Colosseum.Theme { id: theme }
    Text { text: "Zoom"; font.family: theme.ui; font.pixelSize: 13; color: theme.inkDim }
    FeriaPageToolButton {
        objectName: "feriaZoomOut"
        iconSource: "../../assets/icons/lucide/minus.svg"
        helpText: "Zoom out (Ctrl+Minus)"
        enabled: root.tools && root.tools.canZoomOut
        onClicked: root.tools.zoomOut()
    }
    Text {
        objectName: "feriaZoomPercent"
        Layout.preferredWidth: 42
        text: (root.tools ? root.tools.zoomPercent : 100) + "%"
        horizontalAlignment: Text.AlignHCenter
        font.family: theme.ui; font.pixelSize: 13; color: theme.ink
        Accessible.role: Accessible.StaticText; Accessible.name: "Zoom " + text
    }
    FeriaPageToolButton {
        objectName: "feriaZoomIn"
        iconSource: "../../assets/icons/lucide/minus.svg"; crossBar: true
        helpText: "Zoom in (Ctrl+Plus)"
        enabled: root.tools && root.tools.canZoomIn
        onClicked: root.tools.zoomIn()
    }
    AccountUi.AccountButton {
        objectName: "feriaZoomReset"
        text: "Reset"; variant: "link"; implicitWidth: 58; implicitHeight: 34
        enabled: root.tools && root.tools.active && root.tools.zoomPercent !== 100
        Accessible.name: "Reset zoom to 100% (Ctrl+0)"
        onClicked: root.tools.resetZoom()
    }
}
