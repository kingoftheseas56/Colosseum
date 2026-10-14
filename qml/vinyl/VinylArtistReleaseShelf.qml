import QtQuick
import ".."

Column {
    id: root
    required property string title
    required property var records
    required property var artistData
    property int columns: 6
    signal albumRequested(var album)
    spacing: 20
    Theme { id: theme }
    Text { text: root.title; color: theme.ink; font.family: theme.display; font.pixelSize: 28 }
    Grid {
        width: parent.width; columns: root.columns; columnSpacing: 22; rowSpacing: 22
        Repeater {
            model: root.records
            delegate: KeyboardAction {
                required property var modelData
                required property int index
                objectName: "vinylArtistSingle_" + index
                width: (root.width - (root.columns - 1) * 22) / root.columns; height: width + 70
                accessibleName: modelData.title
                onTriggered: root.albumRequested(modelData)
                RoundedPosterImage { width: parent.width; height: width; radius: 10; sources: [root.artistData.artwork(modelData.cover)]; hovered: parent.interactionActive }
                Text { y: parent.width + 12; width: parent.width; text: modelData.title; elide: Text.ElideRight; color: theme.ink; font.family: theme.ui; font.pixelSize: 14; font.weight: Font.DemiBold }
                Text { y: parent.width + 36; text: modelData.year + " · " + modelData.releaseType; color: theme.inkDim; font.family: theme.ui; font.pixelSize: 12 }
            }
        }
    }
}
