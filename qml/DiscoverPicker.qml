// DiscoverPicker — one Discover selector pill: label + current value, popup option
// list. House grammar: glass pill, gold active option, click-swallower popup body
// (floating panel rule). Popup closes on outside tap or Esc.
import QtQuick

Item {
    id: picker
    property string label: ""                 // dim prefix ("Genre") — optional
    property var options: []                  // [{key, text, sub}] — sub dims after text
    property string currentKey: ""
    property bool open: false
    signal picked(string key)

    readonly property var current: {
        for (var i = 0; i < options.length; i++)
            if (options[i].key === currentKey) return options[i];
        return null;
    }

    implicitWidth: Math.max(150, pillRow.implicitWidth + 38)
    implicitHeight: 40

    Theme { id: theme }

    Rectangle {
        id: pill
        anchors.fill: parent
        radius: 13
        color: ma.containsMouse || picker.open ? Qt.rgba(1, 1, 1, 0.10) : Qt.rgba(1, 1, 1, 0.05)
        border.width: 1
        border.color: picker.open ? theme.gold : theme.edge

        Row {
            id: pillRow
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: parent.left; anchors.leftMargin: 15
            spacing: 8
            Text {
                visible: picker.label.length > 0 && !picker.current
                text: picker.label
                color: theme.inkDim; font.family: theme.ui; font.pixelSize: 14
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                visible: !!picker.current
                text: picker.current ? picker.current.text : ""
                color: theme.ink; font.family: theme.ui
                font.pixelSize: 14; font.weight: Font.DemiBold
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                visible: !!(picker.current && picker.current.sub)
                text: picker.current && picker.current.sub ? picker.current.sub : ""
                color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 12
                anchors.verticalCenter: parent.verticalCenter
            }
        }
        Text {
            text: "▾"
            color: theme.inkDimmer; font.pixelSize: 11
            anchors.right: parent.right; anchors.rightMargin: 13
            anchors.verticalCenter: parent.verticalCenter
        }
        MouseArea {
            id: ma
            anchors.fill: parent
            hoverEnabled: true; cursorShape: Qt.PointingHandCursor
            onClicked: picker.open = !picker.open
        }
    }

    // popup — parented to the picker; z above siblings. Own MouseArea = click-swallower.
    Rectangle {
        id: pop
        visible: picker.open
        y: pill.height + 6
        width: Math.max(picker.width, 240)
        height: Math.min(360, list.contentHeight + 12)
        radius: 13
        z: 60
        color: Qt.rgba(0.045, 0.05, 0.075, 0.97)
        border.width: 1; border.color: theme.edge
        MouseArea { anchors.fill: parent }   // swallow

        ListView {
            id: list
            anchors.fill: parent; anchors.margins: 6
            clip: true
            model: picker.options
            boundsBehavior: Flickable.StopAtBounds
            delegate: Rectangle {
                required property var modelData
                width: list.width; height: 38; radius: 9
                color: modelData.key === picker.currentKey ? Qt.rgba(240/255, 196/255, 74/255, 0.16)
                     : rowMa.containsMouse ? Qt.rgba(1, 1, 1, 0.08) : "transparent"
                Row {
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left; anchors.leftMargin: 12
                    spacing: 8
                    Text {
                        text: modelData.text
                        color: modelData.key === picker.currentKey ? theme.gold : theme.ink
                        font.family: theme.ui; font.pixelSize: 13
                        font.weight: modelData.key === picker.currentKey ? Font.DemiBold : Font.Normal
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Text {
                        visible: !!modelData.sub
                        text: modelData.sub || ""
                        color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 11
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
                MouseArea {
                    id: rowMa
                    anchors.fill: parent
                    hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                    onClicked: { picker.open = false; picker.picked(modelData.key); }
                }
            }
        }
    }
    Keys.onEscapePressed: picker.open = false
}
