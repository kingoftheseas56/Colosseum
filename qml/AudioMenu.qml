pragma ComponentBehavior: Bound

import QtQuick

Item {
    id: menu
    width: 48
    height: 48

    property bool panelOpen: false
    property var tracks: []
    property alias delegateModel: menu.tracks
    property string selectedId: ""
    property real delay: 0
    property alias syncValue: menu.delay
    property string icon: ""
    property string title: ""
    property int count: (tracks || []).length
    property int panelWidth: 360
    property int panelHeight: 280
    property string emptyText: ""

    signal toggleRequested(bool wasOpen)
    signal trackPicked(string trackId)
    signal delaySet(real value)
    signal delayStep(real delta)
    signal resetDelay()

    readonly property bool many: (tracks || []).length > 1

    function fmtSigned(value) {
        return (value >= 0 ? "+" : "") + Number(value).toFixed(2) + "s";
    }

    function rowLabel(track) {
        return track.label || track.title || track.lang || track.id || "Audio track";
    }

    function rowMeta(track) {
        var parts = [];
        if (track.codec && String(track.codec).trim() !== "")
            parts.push(String(track.codec).toUpperCase());
        if (track.channels && String(track.channels).trim() !== "")
            parts.push(String(track.channels));
        if (track.default)
            parts.push("Default");
        return parts.join(" · ");
    }

    Theme { id: theme }

    Rectangle {
        anchors.fill: parent
        radius: width / 2
        color: menu.panelOpen ? Qt.rgba(1, 1, 1, 0.22)
                              : launchMouse.containsMouse && menu.many ? Qt.rgba(1, 1, 1, 0.10) : "transparent"
    }
    IconGlyph {
        anchors.centerIn: parent
        width: 24
        height: 24
        kind: "audio"
        ink: menu.many ? theme.ink : Qt.rgba(1, 1, 1, 0.30)
    }
    MouseArea {
        id: launchMouse
        anchors.fill: parent
        enabled: menu.many
        hoverEnabled: true
        cursorShape: menu.many ? Qt.PointingHandCursor : Qt.ArrowCursor
        onClicked: menu.toggleRequested(menu.panelOpen)
    }

    Rectangle {
        visible: menu.panelOpen
        z: 30
        width: 360
        height: Math.min(360, 94 + Math.max(1, (menu.tracks || []).length) * 54 + 44)
        x: parent.width - width
        y: -height - 10
        radius: 16
        color: Qt.rgba(12 / 255, 14 / 255, 18 / 255, 0.94)
        border.width: 1
        border.color: Qt.rgba(1, 1, 1, 0.12)
        clip: true

        Row {
            id: head
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: 50
            leftPadding: 16
            rightPadding: 8
            spacing: 8

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: "Audio"
                color: theme.ink
                font.family: theme.ui
                font.pixelSize: 14
                font.weight: Font.DemiBold
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: (menu.tracks || []).length
                color: theme.inkDimmer
                font.family: "Consolas"
                font.pixelSize: 12
            }
            Item { width: 1; height: 1; LayoutMirroring.enabled: false }
        }

        CloseButton {
            anchors.right: parent.right
            anchors.rightMargin: 8
            anchors.top: parent.top
            anchors.topMargin: 7
            onClicked: menu.panelOpen = false
        }

        Rectangle {
            x: 0
            y: 50
            width: parent.width
            height: 1
            color: Qt.rgba(1, 1, 1, 0.08)
        }

        ListView {
            id: audioList
            visible: menu.many
            x: 8
            y: 58
            width: parent.width - 16
            height: parent.height - y - 44
            clip: true
            spacing: 2
            boundsBehavior: Flickable.StopAtBounds
            model: menu.tracks

            delegate: Rectangle {
                id: row
                required property var modelData
                width: ListView.view.width
                height: 50
                radius: 10
                property bool selected: String(modelData.id) === menu.selectedId || modelData.selected === true
                color: selected ? Qt.rgba(1, 1, 1, 0.10)
                                : rowMouse.containsMouse ? Qt.rgba(1, 1, 1, 0.055) : "transparent"
                border.width: selected ? 1 : 0
                border.color: Qt.rgba(1, 1, 1, 0.12)

                Rectangle {
                    x: 12
                    y: 17
                    width: 16
                    height: 16
                    radius: 8
                    color: row.selected ? theme.gold : Qt.rgba(1, 1, 1, 0.12)
                    Text {
                        anchors.centerIn: parent
                        text: row.selected ? "✓" : ""
                        color: "#111111"
                        font.family: theme.ui
                        font.pixelSize: 10
                        font.weight: Font.Bold
                    }
                }
                Rectangle {
                    x: 38
                    y: 18
                    width: 18
                    height: 13
                    radius: 2
                    gradient: Gradient {
                        GradientStop { position: 0; color: "#bbbbbb" }
                        GradientStop { position: 1; color: "#777777" }
                    }
                }
                Text {
                    x: 68
                    y: 8
                    width: parent.width - 82
                    text: menu.rowLabel(row.modelData)
                    color: theme.ink
                    font.family: theme.ui
                    font.pixelSize: 14
                    font.weight: row.selected ? Font.DemiBold : Font.Medium
                    elide: Text.ElideRight
                }
                Text {
                    x: 68
                    y: 28
                    width: parent.width - 82
                    text: menu.rowMeta(row.modelData)
                    color: theme.inkDimmer
                    font.family: theme.ui
                    font.pixelSize: 10
                    elide: Text.ElideRight
                }
                MouseArea {
                    id: rowMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        menu.trackPicked(String(row.modelData.id));
                        menu.panelOpen = false;
                    }
                }
            }
        }

        Text {
            visible: !menu.many
            x: 24
            y: 86
            width: parent.width - 48
            text: "This file has one audio track."
            color: theme.inkDimmer
            font.family: theme.ui
            font.pixelSize: 13
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
        }

        DelayRow {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            value: menu.delay
            label: "SYNC"
            onStep: function(delta) {
                menu.delaySet(Math.round((menu.delay + delta) * 100) / 100);
                menu.delayStep(delta);
            }
            onReset: {
                menu.delaySet(0);
                menu.resetDelay();
            }
        }
    }

    component CloseButton: Item {
        id: closeButton
        signal clicked()
        width: 36
        height: 36
        Rectangle {
            anchors.fill: parent
            radius: 18
            color: closeMouse.containsMouse ? Qt.rgba(1, 1, 1, 0.08) : "transparent"
        }
        Text {
            anchors.centerIn: parent
            text: "x"
            color: closeMouse.containsMouse ? theme.ink : theme.inkDim
            font.family: theme.ui
            font.pixelSize: 16
            font.weight: Font.DemiBold
        }
        MouseArea {
            id: closeMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: closeButton.clicked()
        }
    }

    component DelayRow: Rectangle {
        id: delayRow
        property real value: 0
        property string label: ""
        signal step(real delta)
        signal reset()
        height: 44
        color: "transparent"
        Rectangle {
            anchors.top: parent.top
            width: parent.width
            height: 1
            color: Qt.rgba(1, 1, 1, 0.08)
        }
        Row {
            anchors.fill: parent
            anchors.leftMargin: 14
            anchors.rightMargin: 12
            spacing: 6
            Text {
                width: 128
                anchors.verticalCenter: parent.verticalCenter
                text: delayRow.label
                color: theme.inkDimmer
                font.family: theme.ui
                font.pixelSize: 10
                font.weight: Font.Bold
                font.letterSpacing: 1.4
            }
            StepButton { text: "-0.1"; onClicked: delayRow.step(-0.1) }
            Text {
                width: 58
                anchors.verticalCenter: parent.verticalCenter
                text: menu.fmtSigned(delayRow.value)
                color: theme.ink
                font.family: "Consolas"
                font.pixelSize: 12
                horizontalAlignment: Text.AlignHCenter
            }
            StepButton { text: "+0.1"; onClicked: delayRow.step(0.1) }
            StepButton {
                visible: Math.abs(delayRow.value) > 0.0001
                width: 32
                text: "0"
                onClicked: delayRow.reset()
            }
        }
    }

    component StepButton: Item {
        id: stepButton
        property string text: ""
        signal clicked()
        width: 44
        height: 24
        Rectangle {
            anchors.fill: parent
            radius: 6
            color: stepMouse.containsMouse ? Qt.rgba(1, 1, 1, 0.12) : Qt.rgba(1, 1, 1, 0.06)
            border.width: 1
            border.color: Qt.rgba(1, 1, 1, 0.10)
        }
        Text {
            anchors.centerIn: parent
            text: stepButton.text
            color: theme.inkDim
            font.family: "Consolas"
            font.pixelSize: 11
            font.weight: Font.DemiBold
        }
        MouseArea {
            id: stepMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: stepButton.clicked()
        }
    }

    component IconGlyph: Canvas {
        id: glyph
        property string kind: ""
        property color ink: theme.ink
        antialiasing: true
        onKindChanged: requestPaint()
        onInkChanged: requestPaint()
        onPaint: {
            var ctx = getContext("2d");
            var w = width;
            var h = height;
            var s = Math.min(w, h);
            var cx = w / 2;
            var cy = h / 2;
            ctx.clearRect(0, 0, w, h);
            ctx.strokeStyle = ink;
            ctx.fillStyle = ink;
            ctx.lineWidth = Math.max(1.6, s / 13);
            ctx.lineCap = "round";
            ctx.lineJoin = "round";
            function line(x1, y1, x2, y2) {
                ctx.beginPath();
                ctx.moveTo(cx + x1 * s, cy + y1 * s);
                ctx.lineTo(cx + x2 * s, cy + y2 * s);
                ctx.stroke();
            }
            ctx.beginPath();
            ctx.arc(cx, cy, 0.21 * s, 0, Math.PI * 2);
            ctx.stroke();
            line(-0.20, -0.02, -0.38, -0.17);
            line(0.20, -0.02, 0.38, -0.17);
            line(-0.12, 0.18, -0.24, 0.35);
            line(0.12, 0.18, 0.24, 0.35);
        }
    }
}
