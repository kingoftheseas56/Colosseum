// GenreMosaic — genre tiles each FILLED with a representative cover (darkened), name + count over it.
// The genre becomes its own art instead of an abstract color. `covers` is a pool of cover URLs the
// tiles cycle through (placeholder mapping until real per-genre art).
import QtQuick

Column {
    id: gm
    property string title: ""
    property string moreLabel: "Explore"
    property var genres: []
    property var covers: []
    property int columns: 5
    property bool navigable: true            // show the header's "Explore ›" affordance
    signal genreClicked(int index)
    signal exploreClicked()                  // tapped "Explore ›" → host opens the full genre index

    width: parent ? parent.width : 800
    spacing: 14
    Theme { id: theme }

    WidgetHeader {
        width: parent.width; title: gm.title; moreLabel: gm.moreLabel
        navigable: gm.navigable
        onMoreClicked: gm.exploreClicked()
    }

    Grid {
        id: grid
        width: parent.width
        columns: gm.columns
        columnSpacing: 16; rowSpacing: 16
        readonly property real cellW: (width - (columns - 1) * columnSpacing) / columns

        Repeater {
            model: gm.genres
            delegate: Rectangle {
                id: cell
                required property var modelData
                required property int index
                width: grid.cellW; height: 112; radius: 14; clip: true
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0; color: cell.modelData.c1 !== undefined ? cell.modelData.c1 : "#444" }
                    GradientStop { position: 1; color: cell.modelData.c2 !== undefined ? cell.modelData.c2 : "#111" }
                }
                border.width: 1; border.color: theme.edge

                Image {
                    anchors.fill: parent
                    source: cell.modelData.cover !== undefined ? cell.modelData.cover
                          : (gm.covers.length > 0 ? gm.covers[cell.index % gm.covers.length] : "")
                    asynchronous: true; cache: true
                    fillMode: Image.PreserveAspectCrop
                    verticalAlignment: Image.AlignTop
                    sourceSize.width: 360; sourceSize.height: 240
                    opacity: status === Image.Ready ? 1 : 0
                    Behavior on opacity { NumberAnimation { duration: 250 } }
                }
                Rectangle { anchors.fill: parent; color: Qt.rgba(0, 0, 0, 0.46) }   // darken for legibility

                Text {
                    text: cell.modelData.name; color: theme.ink
                    font.family: theme.display; font.pixelSize: 16; font.weight: Font.DemiBold
                    anchors.left: parent.left; anchors.bottom: parent.bottom; anchors.margins: 13
                    style: Text.Outline; styleColor: Qt.rgba(0, 0, 0, 0.6)
                }
                Text {
                    text: cell.modelData.count !== undefined ? cell.modelData.count : ""
                    color: Qt.rgba(1, 1, 1, 0.82)
                    font.family: theme.ui; font.pixelSize: 11
                    anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 12
                }
                Rectangle {
                    anchors.fill: parent; radius: parent.radius; color: "transparent"
                    border.width: ma.containsMouse ? 2 : 0; border.color: theme.gold
                }
                MouseArea {
                    id: ma; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                    onClicked: gm.genreClicked(cell.index)
                }
            }
        }
    }
}
