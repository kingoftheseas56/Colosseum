// VaultTile — the shared shelf/folder tile. It keeps unavailable roots in the gallery and paints
// their state in place instead of letting a missing filesystem path erase the user's shelf.
import QtQuick

Item {
    id: tile
    required property var modelData
    signal folderRequested(var data)
    signal openRequested(var data)
    signal revealRequested(var data)
    signal identifyRequested(var data)
    signal unidentifyRequested(var data)
    signal reshelveRequested(string kind, var data)
    signal hideRequested(var data)
    signal restoreRequested(var data)

    readonly property bool away: Number(modelData.awayCount || 0) > 0
    readonly property bool hasErrors: Number(modelData.errorCount || 0) > 0
    readonly property string identSource: String(modelData.identSource || "")
    readonly property string synopsis: String(modelData.synopsis || "")
    readonly property string synopsisSource: String(modelData.synopsisSource || identSource)
    property bool menuOpen: false
    objectName: "vaultTile_" + (modelData.key || "")
    width: 150
    height: 235 + (tile.synopsis.length ? 34 : 0)

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
        Text {
            objectName: "vaultTileSynopsis"
            visible: tile.synopsis.length > 0
            width: 150
            text: tile.synopsis
            color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 10
            maximumLineCount: 2; elide: Text.ElideRight; wrapMode: Text.WordWrap
        }
        Text {
            objectName: "vaultTileSynopsisSource"
            visible: tile.synopsis.length > 0 && tile.synopsisSource.length > 0
            width: 150
            text: "Source: " + tile.synopsisSource
            color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 9
        }
    }

    // Identity badge and action menu live outside coverBox so the menu can extend below the art
    // without being clipped by the poster's rounded-rectangle mask.
    Rectangle {
        objectName: "vaultTileIdentityBadge"
        visible: tile.identSource.length > 0
        x: tile.width - width - 8; y: 8
        height: 20; width: identityBadgeText.implicitWidth + 16; radius: 99
        color: Qt.rgba(0, 0, 0, 0.72); border.width: 1; border.color: theme.edge
        Text {
            id: identityBadgeText
            anchors.centerIn: parent
            text: tile.identSource
            color: theme.gold; font.family: theme.ui; font.pixelSize: 9; font.letterSpacing: 1.4
        }
    }

    Rectangle {
        id: menuButton
        objectName: "vaultTileMenu"
        width: 28; height: 28; radius: 8
        x: tile.width - width - (tile.identSource.length > 0 ? identityBadgeText.implicitWidth + 24 : 8)
        y: 8
        color: menuButtonMa.containsMouse ? Qt.rgba(1, 1, 1, 0.16) : Qt.rgba(0, 0, 0, 0.62)
        border.width: 1; border.color: theme.edge
        z: 30
        Text { anchors.centerIn: parent; text: "..."; color: theme.ink; font.pixelSize: 16 }
        MouseArea {
            id: menuButtonMa; anchors.fill: parent; hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: tile.menuOpen = !tile.menuOpen
        }
    }

    Rectangle {
        id: actionMenu
        objectName: "vaultTileActionMenu"
        visible: tile.menuOpen
        x: tile.width - width; y: 40
        width: 194; height: actionColumn.implicitHeight + 10; radius: 10
        color: Qt.rgba(0.071, 0.082, 0.110, 0.99); border.width: 1; border.color: theme.edge
        z: 40

        Column {
            id: actionColumn
            anchors.fill: parent; anchors.margins: 5; spacing: 2
            Repeater {
                model: [
                    { id: "vaultTileOpen", label: "Open", action: "open" },
                    { id: "vaultTileReveal", label: "Reveal in Explorer", action: "reveal" },
                    { id: "vaultTileIdentify", label: "Identify…", action: "identify" },
                    { id: "vaultTileUnidentify", label: "Un-identify", action: "unidentify" },
                    { id: "vaultTileReshelveBook", label: "This is a book…", action: "book" },
                    { id: modelData.hidden ? "vaultTileRestore" : "vaultTileHide",
                      label: modelData.hidden ? "Restore" : "Hide",
                      action: modelData.hidden ? "restore" : "hide" }
                ]
                delegate: Rectangle {
                    required property var modelData
                    objectName: modelData.id
                    width: actionColumn.width; height: 30; radius: 7
                    color: actionMa.containsMouse ? Qt.rgba(1, 1, 1, 0.08) : "transparent"
                    Text {
                        anchors.left: parent.left; anchors.leftMargin: 10; anchors.verticalCenter: parent.verticalCenter
                        text: modelData.label; color: theme.ink; font.family: theme.ui; font.pixelSize: 12
                    }
                    MouseArea {
                        id: actionMa; anchors.fill: parent; hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            tile.menuOpen = false
                            if (modelData.action === "open") tile.openRequested(tile.modelData)
                            else if (modelData.action === "reveal") tile.revealRequested(tile.modelData)
                            else if (modelData.action === "identify") tile.identifyRequested(tile.modelData)
                            else if (modelData.action === "unidentify") tile.unidentifyRequested(tile.modelData)
                            else if (modelData.action === "book") tile.reshelveRequested("book", tile.modelData)
                            else if (modelData.action === "hide") tile.hideRequested(tile.modelData)
                            else if (modelData.action === "restore") tile.restoreRequested(tile.modelData)
                        }
                    }
                }
            }
        }
    }
}
