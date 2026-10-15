import QtQuick
import QtQuick.Controls
import ".."

Item {
    id: root
    objectName: "vinylLyricsColumn"
    required property var pageData
    clip: true
    Theme {
        id: theme
    }
    Column {
        id: lines
        width: parent.width
        y: root.pageData.timedLyrics ? Math.max(-height + root.height * 0.8, Math.min(root.height * 0.15, root.height * 0.38 - Math.max(0, root.pageData.currentLineIndex) * 44)) : 0
        Behavior on y {
            NumberAnimation {
                duration: 180
            }
        }
        Repeater {
            model: root.pageData.lyrics
            delegate: Item {
                id: line
                required property var modelData
                required property int index
                objectName: "vinylLyricLine_" + index
                width: lines.width
                height: 44
                readonly property bool hasDot: !!(root.pageData.notes[index] && root.pageData.notes[index].length)
                opacity: {
                    var local = y + lines.y;
                    return Math.max(0, Math.min(1, local / Math.max(1, root.height * 0.14), (root.height - local) / Math.max(1, root.height * 0.18)));
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width - 18
                    text: line.modelData.text
                    color: root.pageData.manualNoteLine === line.index ? theme.gold : root.pageData.currentLineIndex === line.index ? theme.ink : root.pageData.currentLineIndex > line.index ? theme.inkDimmer : theme.inkDim
                    font.family: theme.ui
                    font.pixelSize: root.width < 300 ? 14 : 16
                    wrapMode: Text.WordWrap
                }
                Rectangle {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    width: 4
                    height: 4
                    radius: 2
                    color: theme.inkDim
                    visible: line.hasDot
                }
                onVisibleChanged: {
                    if (!visible)
                        single.stop();
                }
                Timer {
                    id: single
                    interval: 250
                    onTriggered: root.pageData.seekToLine(line.index)
                }
                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    cursorShape: root.pageData.timedLyrics || line.hasDot ? Qt.PointingHandCursor : Qt.ArrowCursor
                    onClicked: mouse => {
                        if (mouse.button === Qt.RightButton) {
                            single.stop();
                            root.pageData.showNotes(line.index);
                        } else if (root.pageData.timedLyrics)
                            single.restart();
                    }
                    onDoubleClicked: mouse => {
                        single.stop();
                        root.pageData.showNotes(line.index);
                    }
                    onCanceled: single.stop()
                }
                Connections {
                    target: root.pageData
                    function onPlayingIndexChanged() {
                        single.stop();
                    }
                    function onPlayingEditionIdChanged() {
                        single.stop();
                    }
                    function onFixtureIdChanged() {
                        single.stop();
                    }
                    function onTimedLyricsChanged() {
                        single.stop();
                    }
                }
            }
        }
    }
}
