import QtQuick
import QtQuick.Controls
import QtQuick.Effects
import ".."

Flickable {
    id: root
    objectName: "vinylAlbumPage"
    required property var pageData
    required property Item backdrop
    readonly property int trackCount: pageData.tracks.length
    readonly property string shownEditionId: pageData.shownEditionId
    readonly property bool discSwitchVisible: pageData.shownEdition.discs.length > 1
    readonly property string currentSectionKey: sidebar.currentIndex >= 0 ? sidebar.visibleRows[sidebar.currentIndex].key : ""
    readonly property real inset: Math.max(54, (width - 1280) / 2)
    readonly property real usable: width - 2 * inset
    signal videoRequested(int track)
    clip: true
    contentWidth: width
    contentHeight: sections.y + sections.height + height * 0.85
    boundsBehavior: Flickable.StopAtBounds
    Theme {
        id: theme
    }
    ScrollGlide {
        flick: root
    }
    ScrollBar.vertical: HouseScrollBar {
        flick: root
    }
    Connections {
        target: root.pageData
        function onEditionSelected() {
            root.contentY = 0;
        }
    }
    Item {
        id: hero
        x: root.inset
        width: root.usable
        height: Math.max(root.height, trackList.height + 30)
        Column {
            id: left
            y: Math.min(Math.max(24, root.contentY + 24), Math.max(24, hero.height - height - 20))
            width: root.usable * 0.4
            spacing: 14
            Rectangle {
                id: coverMask
                width: Math.min(334, root.height * 0.5)
                height: width
                radius: 18
                visible: false
                color: theme.ink
                layer.enabled: true
            }
            Image {
                objectName: "vinylAlbumCover"
                layer.enabled: true
                layer.effect: MultiEffect {
                    maskEnabled: true
                    maskSource: coverMask
                }
                width: Math.min(334, root.height * 0.5)
                height: width
                source: root.pageData.artwork(root.pageData.shownEdition.cover)
                fillMode: Image.PreserveAspectFit
            }
            Text {
                objectName: "vinylAlbumTitle"
                width: parent.width
                text: root.pageData.shownEdition.name
                color: theme.ink
                font.family: theme.display
                font.pixelSize: Math.min(44, root.height * 0.067)
                wrapMode: Text.WordWrap
            }
            KeyboardAction {
                objectName: "vinylAlbumArtist"
                width: parent.width
                height: 24
                accessibleName: root.pageData.shownEdition.artist
                onTriggered: root.pageData.artistRequested(accessibleName)
                Text {
                    text: root.pageData.shownEdition.artist
                    color: theme.inkDim
                    font.family: theme.ui
                    font.pixelSize: 16
                }
            }
            Text {
                objectName: "vinylAlbumFacts"
                text: root.pageData.shownEdition.year + " · Album · " + root.pageData.totalTracks(root.pageData.shownEdition) + " tracks · " + (root.pageData.shownEdition.length || "")
                color: theme.inkDimmer
                font.family: theme.ui
                font.pixelSize: 12
            }
            Row {
                spacing: 10
                Repeater {
                    model: ["Play", "Shuffle", "Save"]
                    delegate: KeyboardAction {
                        required property string modelData
                        objectName: "vinylAlbum" + modelData
                        width: 76
                        height: 36
                        accessibleName: modelData
                        onTriggered: {
                            if (modelData === "Play")
                                root.pageData.playTrack(0);
                            else if (modelData === "Shuffle")
                                root.pageData.shuffle();
                            else
                                root.pageData.toggleSaved();
                        }
                        Rectangle {
                            anchors.fill: parent
                            radius: 10
                            color: parent.modelData === "Play" ? theme.gold : theme.glassTint
                            border.color: theme.edge
                        }
                        Text {
                            anchors.centerIn: parent
                            text: parent.modelData === "Save" && root.pageData.saved ? "Saved" : parent.modelData
                            color: parent.modelData === "Play" ? theme.biblioWashBottom : theme.inkDim
                            font.family: theme.ui
                            font.pixelSize: 11
                        }
                    }
                }
            }
        }
        Column {
            id: trackList
            x: root.usable * 0.46
            y: 24
            width: root.usable * 0.54
            Row {
                visible: root.discSwitchVisible
                height: visible ? 42 : 0
                spacing: 14
                Repeater {
                    model: root.pageData.shownEdition.discs.length
                    delegate: KeyboardAction {
                        required property int index
                        objectName: "vinylDisc_" + index
                        width: 75
                        height: 30
                        accessibleName: "Disc " + (index + 1)
                        onTriggered: root.pageData.selectDisc(index)
                        Text {
                            anchors.centerIn: parent
                            text: "Disc " + (parent.index + 1)
                            color: root.pageData.shownDisc === parent.index ? theme.gold : theme.inkDim
                            font.family: theme.ui
                            font.pixelSize: 13
                        }
                    }
                }
            }
            Repeater {
                model: root.pageData.tracks.length
                delegate: VinylTrackRow {
                    required property int index
                    trackIndex: index
                    pageData: root.pageData
                    width: trackList.width
                    height: root.height < 600 ? (artistLine ? 58 : 50) : (artistLine ? 62 : 57)
                }
            }
        }
    }
    VinylAlbumSections {
        id: sections
        x: root.inset + sidebar.contentLeft
        y: hero.height + 50
        width: root.usable - sidebar.contentLeft
        pageData: root.pageData
        onVideoRequested: track => root.videoRequested(track)
    }
    RowIndexSidebar {
        id: sidebar
        parent: root.contentItem
        x: root.inset - 5
        z: 5
        pageFlick: root
        flowHost: sections
        backdrop: root.backdrop
        worldName: "Vinyl"
        automationPrefix: "vinylRowIndex"
        rows: sections.sectionRows
        currentOffset: 12
        onRowRequested: target => {
            root.cancelFlick();
            root.contentY = Math.max(0, target.mapToItem(root.contentItem, 0, 0).y - sidebar.pinLine);
        }
    }
}
