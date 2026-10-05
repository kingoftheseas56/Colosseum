pragma ComponentBehavior: Bound
import QtQuick

FocusScope {
    id: menu

    property var mode: "off"
    property bool open: false
    signal chosen(var mode)

    readonly property var choices: [
        { value: "off", label: "Off" },
        { value: 15, label: "15 min" },
        { value: 30, label: "30 min" },
        { value: 45, label: "45 min" },
        { value: 60, label: "60 min" },
        { value: "chapterEnd", label: "End of chapter" }
    ]

    property int highlightedIndex: indexForMode(mode)

    implicitWidth: 206
    implicitHeight: choices.length * 38 + 12
    width: implicitWidth
    height: implicitHeight
    visible: open
    enabled: open
    focus: open
    function sameMode(a, b) {
        return a === b || String(a) === String(b)
    }

    function indexForMode(value) {
        for (var i = 0; i < choices.length; ++i) {
            if (sameMode(choices[i].value, value))
                return i
        }
        return 0
    }

    function moveHighlight(delta) {
        highlightedIndex = Math.max(0, Math.min(choices.length - 1,
                                               highlightedIndex + delta))
    }

    function chooseIndex(index) {
        if (index < 0 || index >= choices.length)
            return
        chosen(choices[index].value)
        open = false
    }

    onModeChanged: if (!open) highlightedIndex = indexForMode(mode)
    onOpenChanged: {
        if (!open)
            return
        highlightedIndex = indexForMode(mode)
        Qt.callLater(function() { if (menu.open && menu.visible && menu.enabled) menu.forceActiveFocus(Qt.PopupFocusReason) })
    }
    Keys.onPressed: function(event) {
        if (!open)
            return
        if (event.key === Qt.Key_Escape) {
            open = false
            event.accepted = true
        } else if (event.key === Qt.Key_Down) {
            moveHighlight(1)
            event.accepted = true
        } else if (event.key === Qt.Key_Up) {
            moveHighlight(-1)
            event.accepted = true
        } else if (event.key === Qt.Key_Home) {
            highlightedIndex = 0
            event.accepted = true
        } else if (event.key === Qt.Key_End) {
            highlightedIndex = choices.length - 1
            event.accepted = true
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter
                   || event.key === Qt.Key_Space) {
            chooseIndex(highlightedIndex)
            event.accepted = true
        }
    }

    Rectangle {
        anchors.fill: parent
        radius: 13
        color: Qt.rgba(13 / 255, 13 / 255, 16 / 255, 0.96)
        border.color: Theme.barBorder
        border.width: 1
        antialiasing: true

        Column {
            anchors.fill: parent
            anchors.margins: 6

            Repeater {
                model: menu.choices

                delegate: Item {
                    id: row
                    required property var modelData
                    required property int index
                    objectName: "sleepTimerRow" + index
                    width: parent.width
                    height: 38

                    readonly property bool current: menu.indexForMode(menu.mode) === index
                    readonly property bool highlighted: menu.highlightedIndex === index

                    Rectangle {
                        anchors.fill: parent
                        radius: 8
                        color: row.highlighted ? Theme.rowHover : "transparent"
                    }

                    Text {
                        objectName: "sleepTimerMark" + row.index
                        anchors.left: parent.left
                        anchors.leftMargin: 10
                        anchors.verticalCenter: parent.verticalCenter
                        width: 16
                        text: "✓"
                        visible: row.current
                        color: Theme.gold
                        font.family: Theme.ui
                        font.pixelSize: 12
                        font.weight: Font.DemiBold
                    }

                    Text {
                        objectName: "sleepTimerLabel" + row.index
                        anchors.left: parent.left
                        anchors.leftMargin: 34
                        anchors.right: parent.right
                        anchors.rightMargin: 10
                        anchors.verticalCenter: parent.verticalCenter
                        text: row.modelData.label
                        color: row.current ? Theme.gold
                                           : (row.highlighted ? Theme.ink : Theme.inkDim)
                        font.family: Theme.ui
                        font.pixelSize: 13
                        font.weight: row.current ? Font.DemiBold : Font.Medium
                    }

                    ReaderKeyboardArea {
                        objectName: "sleepTimerAction" + row.index
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        keyboardLabel: row.modelData.label
                        keyboardFocused: function() { menu.highlightedIndex = row.index }
                        onClicked: menu.chooseIndex(row.index)
                    }
                }
            }
        }
    }
}
