// VaultTile — the shared shelf/folder tile. It keeps unavailable roots in the gallery and paints
// their state in place instead of letting a missing filesystem path erase the user's shelf.
import QtQuick

Item {
    id: tile
    required property var modelData
    signal folderRequested(var data)

    readonly property bool away: Number(modelData.awayCount || 0) > 0
    readonly property bool hasErrors: Number(modelData.errorCount || 0) > 0
    objectName: "vaultTile_" + (modelData.key || "")
    width: 150
    height: 235

    Theme { id: theme }

    Column {
        anchors.fill: parent
        spacing: 8
        Rectangle {
            id: coverBox
            objectName: "vaultTileCover"
            width: 150; height: 208; radius: 12; clip: true
            border.width: 1; border.color: theme.edge
            opacity: tile.away ? 0.48 : 1.0
            gradient: Gradient {
                GradientStop { position: 0.0; color: Qt.rgba(0.16, 0.14, 0.20, 1) }
                GradientStop { position: 1.0; color: Qt.rgba(0.055, 0.060, 0.090, 1) }
            }
            Image {
                anchors.fill: parent
                visible: !!tile.modelData.coverUrl
                source: tile.modelData.coverUrl || ""
                fillMode: Image.PreserveAspectCrop
                asynchronous: true; cache: true
            }
            Image {
                anchors.centerIn: parent; width: 34; height: 34; opacity: 0.4
                visible: !tile.modelData.coverUrl
                source: tile.modelData.kind === "book" ? "../assets/icons/book-library.svg"
                      : tile.modelData.kind === "video" ? "../assets/icons/projector-theatre.svg"
                      : "../assets/icons/comic-book.svg"
                fillMode: Image.PreserveAspectFit
            }
            Rectangle {
                anchors.fill: parent
                visible: tile.away || tile.hasErrors
                color: Qt.rgba(0.04, 0.04, 0.04, tile.away ? 0.54 : 0.38)
                Text {
                    anchors.centerIn: parent
                    text: tile.away ? "Unavailable" : "Needs attention"
                    color: theme.inkDim
                    font.family: theme.ui; font.pixelSize: 11; font.weight: Font.DemiBold
                }
            }
            Rectangle {
                anchors.top: parent.top; anchors.left: parent.left; anchors.margins: 8
                radius: 99; height: 20; width: badgeText.implicitWidth + 16
                color: Qt.rgba(0, 0, 0, 0.62); border.width: 1; border.color: theme.edge
                Text {
                    id: badgeText; anchors.centerIn: parent
                    text: tile.modelData.kind === "comic" ? "COMIC"
                        : tile.modelData.kind === "book" ? "BOOK" : "VIDEO"
                    color: theme.gold; font.family: theme.ui; font.pixelSize: 9; font.letterSpacing: 1.6
                }
            }
            Rectangle {
                anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                height: 76
                gradient: Gradient {
                    GradientStop { position: 0.0; color: "transparent" }
                    GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.82) }
                }
            }
            Text {
                anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                anchors.leftMargin: 9; anchors.rightMargin: 9; anchors.bottomMargin: 9
                text: tile.modelData.title || ""
                color: "#f2f2f0"; font.family: theme.ui; font.pixelSize: 13; font.weight: Font.DemiBold
                elide: Text.ElideRight; maximumLineCount: 2; wrapMode: Text.WordWrap
                style: Text.Raised; styleColor: Qt.rgba(0, 0, 0, 0.9)
            }
            MouseArea {
                anchors.fill: parent
                enabled: !tile.away
                hoverEnabled: true
                cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                onClicked: tile.folderRequested(tile.modelData)
            }
        }
        Text {
            text: (tile.modelData.count || 0) + ((tile.modelData.count === 1) ? " item" : " items")
            color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 11; font.letterSpacing: 0.4
        }
    }
}
