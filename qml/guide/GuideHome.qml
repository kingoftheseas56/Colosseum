import QtQuick 2.15
import QtQuick.Controls.Basic 2.15 as Basic

Item {
    id: root
    objectName: "guideHome"
    property string title: "Guide"
    property var progress: null
    readonly property var popularLabels: ["Continue where I left off", "Open media from this device",
                                         "Choose and enable a source", "Something is not working"]
    signal popularPathRequested(string label)
    signal wallpaperChoiceRequested()

    implicitHeight: content.implicitHeight

    Column {
        id: content
        width: parent.width
        spacing: 20
        Text { text: root.title; color: "#f5f5f5"; font.pixelSize: 34; font.weight: Font.DemiBold }
        GuideFirstJourney { id: firstJourney; width: parent.width; progress: root.progress }
        Column {
            width: parent.width
            spacing: 6
            Text { text: "POPULAR PATHS"; color: "#9b9b9b"; font.pixelSize: 11; font.letterSpacing: 1.1 }
            Repeater {
                model: root.popularLabels
                delegate: Basic.Button {
                    width: parent.width
                    height: 42
                    text: modelData
                    activeFocusOnTab: true
                    contentItem: Text { text: parent.text; color: "#ededed"; verticalAlignment: Text.AlignVCenter; leftPadding: 12; font.pixelSize: 15 }
                    background: Rectangle { color: parent.hovered ? "#252525" : "#1a1a1a"; border.color: parent.activeFocus ? "#efefef" : "#3a3a3a"; radius: 2 }
                    onClicked: root.popularPathRequested(modelData)
                }
            }
        }
    }

    Connections {
        target: firstJourney
        function onWallpaperChoiceRequested() { root.wallpaperChoiceRequested() }
    }
}
