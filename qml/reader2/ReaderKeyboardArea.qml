import QtQuick
import QtQuick.Controls

MouseArea {
    id: area

    property bool keyboardTabStop: cursorShape === Qt.PointingHandCursor
    property string keyboardLabel: objectName.length ? objectName : qsTr("Action")
    property string tooltipText: ""
    property int keyboardRole: Accessible.Button
    property var keyboardActivate: null
    property var keyboardContext: null
    property var keyboardDecrease: null
    property var keyboardIncrease: null
    property var keyboardHome: null
    property var keyboardEnd: null
    property var keyboardFocused: null
    // True while this area holds focus because of a pointer press. Reader chrome
    // pins itself awake only for keyboard focus, so a click still lets it idle away.
    property bool pointerFocused: false

    // Tooltip is opt-in; existing click, keyboard and hover handlers stay the owners.
    hoverEnabled: tooltipText.length > 0
    ToolTip.text: area.tooltipText
    ToolTip.delay: 350
    ToolTip.visible: Qt.platform.os !== "android" && area.tooltipText.length > 0
        && area.containsMouse && area.visible && area.enabled

    // Qt focus traversal already skips disabled/effectively-hidden items; keep eligibility stable
    // while a focused popup is closing so activeFocusOnTab never flips underneath active focus.
    activeFocusOnTab: keyboardTabStop
    Accessible.role: keyboardRole
    Accessible.name: keyboardLabel
    Accessible.focusable: activeFocusOnTab
    Accessible.focused: activeFocus

    function focusKeyboard() {
        area.pointerFocused = false
        area.forceActiveFocus(Qt.OtherFocusReason)
    }

    function activateFromKeyboard() {
        if (keyboardActivate)
            keyboardActivate()
        else
            area.clicked(null)
    }

    Keys.onPressed: function(event) {        if (!area.enabled)
            return
        if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space) {
            area.activateFromKeyboard(); event.accepted = true; return
        }
        if (event.key === Qt.Key_Menu || (event.key === Qt.Key_F10 && (event.modifiers & Qt.ShiftModifier))) {
            if (area.keyboardContext) { area.keyboardContext(); event.accepted = true }
            return
        }
        if ((event.key === Qt.Key_Left || event.key === Qt.Key_Down) && area.keyboardDecrease) {
            area.keyboardDecrease(); event.accepted = true; return
        }
        if ((event.key === Qt.Key_Right || event.key === Qt.Key_Up) && area.keyboardIncrease) {
            area.keyboardIncrease(); event.accepted = true; return
        }
        if (event.key === Qt.Key_Home && area.keyboardHome) {
            area.keyboardHome(); event.accepted = true; return
        }
        if (event.key === Qt.Key_End && area.keyboardEnd) {
            area.keyboardEnd(); event.accepted = true
        }
    }

    Connections {
        target: area
        function onPressed(mouse) {
            if (area.keyboardTabStop) {
                area.pointerFocused = true
                area.forceActiveFocus(Qt.MouseFocusReason)
            }
        }
    }
    onActiveFocusChanged: {
        if (!activeFocus)
            pointerFocused = false
        if (activeFocus && keyboardFocused)
            keyboardFocused()
    }

    Rectangle {
        anchors.fill: parent
        anchors.margins: -2
        color: "transparent"
        border.width: 2
        border.color: "#F0C24A"
        radius: 4
        visible: area.activeFocus && area.keyboardTabStop
        z: 10000
    }
}
