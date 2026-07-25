// DiscoverPicker — one Discover selector pill: label + current value, popup option
// list. House grammar: glass pill, gold active option, click-swallower popup body
// (floating panel rule). Popup closes on outside tap or Esc.
// Options may carry section HEADERS ({header:"…"}) — the catalog picker splits its
// rows into "Cinemeta" / "Your addons" (addon attribution is the parity anatomy).
// A row option is {key, text, sub} — sub is the owning addon, right-aligned.
import QtQuick

Item {
    id: picker
    property string label: ""                 // dim placeholder ("Genre") — shown when no real value
    property var options: []                  // [{key,text,sub} | {header:"…"}]
    property string currentKey: ""
    property bool open: false
    property bool clearable: true             // an active filter shows an ✕ that resets it
    signal picked(string key)
    signal cleared()

    // current = the selected ROW option (headers and the empty "All" key don't count as a value)
    readonly property var current: {
        for (var i = 0; i < options.length; i++)
            if (options[i].header === undefined && options[i].key === currentKey) return options[i];
        return null;
    }
    readonly property bool hasValue: !!current && current.key !== ""

    implicitWidth: Math.max(150, pillRow.implicitWidth + 38)
    implicitHeight: 40

    Theme { id: theme }

    Rectangle {
        id: pill
        anchors.fill: parent
        radius: 13
        color: ma.containsMouse || picker.open ? Qt.rgba(1, 1, 1, 0.10) : Qt.rgba(1, 1, 1, 0.05)
        border.width: 1
        border.color: picker.open ? theme.gold
                    : picker.hasValue ? Qt.rgba(240/255, 196/255, 74/255, 0.55)
                    : theme.edge

        Row {
            id: pillRow
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: parent.left; anchors.leftMargin: 15
            spacing: 8
            Text {
                visible: picker.label.length > 0 && !picker.hasValue
                text: picker.label
                color: theme.inkDim; font.family: theme.ui; font.pixelSize: 14
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                visible: picker.hasValue
                text: picker.hasValue ? picker.current.text : ""
                color: theme.gold; font.family: theme.ui
                font.pixelSize: 14; font.weight: Font.DemiBold
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                visible: picker.hasValue && !!picker.current.sub
                text: picker.hasValue && picker.current.sub ? picker.current.sub : ""
                color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 12
                anchors.verticalCenter: parent.verticalCenter
            }
        }
        readonly property bool showClear: picker.hasValue && picker.clearable
        Text {
            text: pill.showClear ? "✕" : "▾"
            color: pill.showClear ? theme.inkDim : theme.inkDimmer
            font.pixelSize: pill.showClear ? 12 : 11
            anchors.right: parent.right; anchors.rightMargin: 13
            anchors.verticalCenter: parent.verticalCenter
        }
        MouseArea {
            id: ma
            anchors.fill: parent
            hoverEnabled: true; cursorShape: Qt.PointingHandCursor
            onClicked: picker.open = !picker.open
        }
        // clear hit-area — sits over the ✕ (declared after `ma`, so it wins the click there)
        MouseArea {
            id: clearMa
            visible: pill.showClear
            anchors.right: parent.right; anchors.top: parent.top; anchors.bottom: parent.bottom
            width: 36
            hoverEnabled: true; cursorShape: Qt.PointingHandCursor
            onClicked: { picker.open = false; picker.cleared() }
        }
    }

    // popup — parented to the picker; z above siblings. Own MouseArea = click-swallower.
    Rectangle {
        id: pop
        visible: picker.open
        y: pill.height + 6
        width: Math.max(picker.width, 280)
        height: Math.min(380, list.contentHeight + 12)
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
            delegate: Item {
                id: opt
                required property var modelData
                readonly property bool isHeader: modelData.header !== undefined
                width: list.width
                height: isHeader ? 27 : 38

                // section header (.msec) — small uppercase, dim
                Text {
                    visible: opt.isHeader
                    text: opt.isHeader ? opt.modelData.header : ""
                    color: theme.inkDimmer
                    font.family: theme.ui; font.pixelSize: 10
                    font.letterSpacing: 1.6; font.capitalization: Font.AllUppercase
                    anchors.left: parent.left; anchors.leftMargin: 12
                    anchors.bottom: parent.bottom; anchors.bottomMargin: 6
                }

                // option row — catalog name left, owning addon dimmed right (mock .mrow)
                Rectangle {
                    visible: !opt.isHeader
                    anchors.fill: parent
                    radius: 9
                    color: opt.modelData.key === picker.currentKey ? Qt.rgba(240/255, 196/255, 74/255, 0.16)
                         : rowMa.containsMouse ? Qt.rgba(1, 1, 1, 0.08) : "transparent"
                    Text {
                        id: catText
                        text: opt.isHeader ? "" : opt.modelData.text
                        color: opt.modelData.key === picker.currentKey ? theme.gold : theme.ink
                        font.family: theme.ui; font.pixelSize: 13
                        font.weight: opt.modelData.key === picker.currentKey ? Font.DemiBold : Font.Normal
                        anchors.left: parent.left; anchors.leftMargin: 12
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Text {
                        visible: !!opt.modelData.sub
                        text: opt.modelData.sub || ""
                        color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 11
                        anchors.right: parent.right; anchors.rightMargin: 12
                        anchors.left: catText.right; anchors.leftMargin: 8
                        horizontalAlignment: Text.AlignRight
                        elide: Text.ElideRight
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    MouseArea {
                        id: rowMa
                        anchors.fill: parent
                        hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                        onClicked: { picker.open = false; picker.picked(opt.modelData.key); }
                    }
                }
            }
        }
    }
    Keys.onEscapePressed: picker.open = false
}
