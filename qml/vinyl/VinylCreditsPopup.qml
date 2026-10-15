import QtQuick
import QtQuick.Controls
import ".."

Popup {
    id: root
    objectName: "vinylCreditsPopup"
    required property var pageData
    required property Item backdrop
    width: Math.min(480, parent.width - 108)
    height: Math.min(320, parent.height - 160)
    padding: 20
    modal: false
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    Theme {
        id: theme
    }
    background: Glass {
        backdrop: root.backdrop
        radius: 18
    }
    onOpened: {
        creditsScroll.contentY = 0;
        creditsScroll.forceActiveFocus();
    }
    contentItem: Flickable {
        id: creditsScroll
        objectName: "vinylCreditsScroll"
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        function moveBy(amount) {
            cancelFlick();
            contentY = Math.max(0, Math.min(contentHeight - height, contentY + amount));
        }
        Keys.onPressed: event => {
            if (event.key === Qt.Key_Down)
                moveBy(40);
            else if (event.key === Qt.Key_Up)
                moveBy(-40);
            else if (event.key === Qt.Key_PageDown)
                moveBy(height * 0.85);
            else if (event.key === Qt.Key_PageUp)
                moveBy(-height * 0.85);
            else if (event.key === Qt.Key_Home)
                moveBy(-contentHeight);
            else if (event.key === Qt.Key_End)
                moveBy(contentHeight);
            else
                return;
            event.accepted = true;
        }
        ScrollGlide {
            anchors.fill: parent
            flick: creditsScroll
        }
        contentHeight: content.height
        ScrollBar.vertical: HouseScrollBar {
            flick: parent
        }
        Column {
            id: content
            width: Math.max(0, parent.width - 18)
            spacing: 18
            Text {
                text: "CREDITS"
                color: theme.inkDimmer
                font.family: theme.ui
                font.pixelSize: 11
                font.letterSpacing: 1.6
            }
            Repeater {
                model: root.pageData.playingCredits
                delegate: Row {
                    required property var modelData
                    width: content.width
                    spacing: 16
                    Text {
                        width: 92
                        text: parent.modelData.role.toUpperCase()
                        color: theme.inkDimmer
                        font.family: theme.ui
                        font.pixelSize: 10
                        wrapMode: Text.WordWrap
                    }
                    Text {
                        width: content.width - 108
                        text: parent.modelData.names.join("\n")
                        color: theme.ink
                        font.family: theme.display
                        font.pixelSize: 20
                        wrapMode: Text.WordWrap
                    }
                }
            }
        }
    }
}
