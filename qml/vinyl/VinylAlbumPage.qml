import QtQuick
import QtQuick.Controls
import QtQuick.Effects
import ".."

Flickable {
    id: root
    objectName: "vinylAlbumPage"
    required property var pageData
    required property Item backdrop
    property bool artistNavigationEnabled: true
    property bool shuffleEnabled: true
    property bool saveEnabled: true
    property bool scrollbarVisible: true
    readonly property int trackCount: pageData.tracks.length
    readonly property string shownEditionId: pageData.shownEditionId
    readonly property bool discSwitchVisible: pageData.shownEdition.discs.length > 1
    readonly property string currentSectionKey: sidebar.currentIndex >= 0 ? sidebar.visibleRows[sidebar.currentIndex].key : ""
    readonly property real inset: Math.max(54, (width - 1280) / 2)
    readonly property real usable: width - 2 * inset
    // Preserve the approved artwork size when it fits. In a short native page,
    // reserve the measured text/actions and the hero's 24px top/bottom padding.
    readonly property real coverSize: Math.max(72, Math.min(334, height * 0.5,
        height - 48 - albumTitle.implicitHeight - albumArtist.height
        - albumFacts.implicitHeight - albumActionsRow.height - left.spacing * 4))
    signal videoRequested(int track)
    function focusPrimaryControl() {
        var play = albumActions.itemAt(0);
        if (play) play.forceActiveFocus();
    }
    clip: true
    contentWidth: width
    contentHeight: sections.y + sections.height + height * 0.85
    boundsBehavior: Flickable.StopAtBounds
    Keys.onPressed: event => {
        var target = contentY;
        if (event.key === Qt.Key_PageDown) target += height * 0.8;
        else if (event.key === Qt.Key_PageUp) target -= height * 0.8;
        else if (event.key === Qt.Key_Home) target = 0;
        else if (event.key === Qt.Key_End) target = contentHeight - height;
        else return;
        cancelFlick();
        contentY = Math.max(0, Math.min(Math.max(0, contentHeight - height), target));
        event.accepted = true;
    }
    Theme {
        id: theme
    }
    ScrollGlide {
        flick: root
    }
    ScrollBar.vertical: HouseScrollBar {
        visible: root.scrollbarVisible
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
                width: root.coverSize
                height: width
                radius: 18
                visible: false
                color: theme.ink
                layer.enabled: true
            }
            Item {
                width: root.coverSize
                height: width
                Rectangle { anchors.fill: parent; radius: 18; color: theme.glassTint; border.color: theme.edge }
                Text { anchors.centerIn: parent; text: "♪"; font.pixelSize: parent.width * 0.24; color: theme.inkDimmer }
                Image {
                    anchors.fill: parent
                    objectName: "vinylAlbumCover"
                    layer.enabled: true
                    layer.effect: MultiEffect {
                        maskEnabled: true
                        maskSource: coverMask
                    }
                    source: root.pageData.artwork(root.pageData.shownEdition.cover)
                    fillMode: Image.PreserveAspectFit
                }
            }
            Text {
                id: albumTitle
                objectName: "vinylAlbumTitle"
                width: parent.width
                text: root.pageData.shownEdition.name
                color: theme.ink
                font.family: theme.display
                font.pixelSize: Math.min(44, root.height * 0.067)
                wrapMode: Text.WordWrap
            }
            KeyboardAction {
                id: albumArtist
                objectName: "vinylAlbumArtist"
                enabled: root.artistNavigationEnabled
                opacity: enabled ? 1 : 0.5
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
                id: albumFacts
                objectName: "vinylAlbumFacts"
                text: [root.pageData.shownEdition.year, root.pageData.shownEdition.origin === "local" ? "Local release" : "Album", root.pageData.totalTracks(root.pageData.shownEdition) + (root.pageData.totalTracks(root.pageData.shownEdition) === 1 ? " track" : " tracks"), root.pageData.shownEdition.length].filter(function(value) { return !!value; }).join(" · ")
                width: parent.width
                wrapMode: Text.WordWrap
                color: theme.inkDimmer
                font.family: theme.ui
                font.pixelSize: 12
            }
            Row {
                id: albumActionsRow
                spacing: 10
                Repeater {
                    id: albumActions
                    model: ["Play", "Shuffle", "Save"]
                    delegate: KeyboardAction {
                        required property string modelData
                        objectName: "vinylAlbum" + modelData
                        width: 76
                        height: 36
                        accessibleName: modelData
                        enabled: modelData === "Play" ? root.trackCount > 0 : modelData === "Shuffle" ? root.shuffleEnabled : root.saveEnabled
                        opacity: enabled ? 1 : 0.35
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
