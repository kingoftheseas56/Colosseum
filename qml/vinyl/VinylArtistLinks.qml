import QtQuick
import ".."

Item {
    id: root

    property var names: []
    property string fallbackName: ""
    property bool navigationEnabled: true
    property real fontSize: 16
    signal artistRequested(string name)

    readonly property var displayedNames: {
        var result = [];
        if (Array.isArray(root.names)) {
            for (var i = 0; i < root.names.length; ++i) {
                var name = root.names[i];
                if (typeof name === "string" && name.length > 0)
                    result.push(name);
            }
        }
        if (result.length === 0 && root.fallbackName.length > 0)
            result.push(root.fallbackName);
        return result;
    }

    implicitHeight: Math.max(24, artistFlow.implicitHeight)
    height: implicitHeight
    opacity: root.navigationEnabled ? 1 : 0.5

    Theme {
        id: theme
    }

    Flow {
        id: artistFlow
        width: root.width
        spacing: 0

        Repeater {
            model: root.displayedNames.length

            delegate: Row {
                id: artistEntry
                required property int index
                readonly property string artistName: root.displayedNames[index]
                readonly property bool showSeparator: index < root.displayedNames.length - 1
                spacing: 0
                width: artistAction.width + (showSeparator ? separator.implicitWidth : 0)
                height: Math.max(artistAction.height, separator.implicitHeight)

                TextMetrics {
                    id: artistMetrics
                    text: artistEntry.artistName
                    font.family: theme.ui
                    font.pixelSize: root.fontSize
                }

                KeyboardAction {
                    id: artistAction
                    width: Math.min(artistMetrics.advanceWidth,
                                    Math.max(0, root.width - (artistEntry.showSeparator ? separator.implicitWidth : 0)))
                    height: artistLabel.implicitHeight
                    enabled: root.navigationEnabled
                    focusOnPointer: false
                    focusRadius: 4
                    accessibleName: artistEntry.artistName
                    onTriggered: root.artistRequested(artistEntry.artistName)

                    Text {
                        id: artistLabel
                        anchors.fill: parent
                        text: artistEntry.artistName
                        color: artistAction.hovered ? theme.ink : theme.inkDim
                        font.family: theme.ui
                        font.pixelSize: root.fontSize
                        wrapMode: Text.Wrap
                    }
                }

                Text {
                    id: separator
                    visible: artistEntry.showSeparator
                    text: " · "
                    color: theme.inkDimmer
                    font.family: theme.ui
                    font.pixelSize: root.fontSize
                }
            }
        }
    }
}
