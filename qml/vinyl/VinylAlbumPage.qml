import QtQuick
import QtQuick.Controls
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
    readonly property real coverSize: Math.min(334, albumInfoColumn.width, Math.max(200, height * 0.65))
    readonly property bool albumDetailsFit: albumInfoColumn.height + 48 <= height
    signal videoRequested(int track)
    signal songRequested(int track)
    property bool songNavigationEnabled: false
    property bool queueActionsEnabled: false
    function focusPrimaryControl() {
        var play = albumActions.itemAt(0);
        if (play) play.forceActiveFocus(Qt.TabFocusReason);
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
        height: Math.max(root.height, trackList.height + 30, albumInfoColumn.height + 48)
        Column {
            id: albumInfoColumn
            y: root.albumDetailsFit
                ? Math.min(Math.max(24, root.contentY + 24), Math.max(24, hero.height - height - 20))
                : 24
            width: root.usable * 0.4
            spacing: 14
            RoundedPosterImage {
                objectName: "vinylAlbumCover"
                width: root.coverSize
                height: width
                radius: 18
                sources: root.pageData.shownEdition.cover ? [root.pageData.artwork(root.pageData.shownEdition.cover)] : []
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
            VinylArtistLinks {
                id: albumArtist
                objectName: "vinylAlbumArtist"
                width: parent.width
                height: implicitHeight
                navigationEnabled: root.artistNavigationEnabled
                names: root.pageData.shownEdition.artists
                    || (root.pageData.shownEdition.sourceAlbum
                        ? root.pageData.shownEdition.sourceAlbum.artists : [])
                    || []
                fallbackName: root.pageData.shownEdition.artist
                fontSize: 16
                onArtistRequested: name => root.pageData.artistRequested(name)
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
                    model: root.queueActionsEnabled ? ["Play", "Shuffle", "Save", "Queue"] : ["Play", "Shuffle", "Save"]
                    delegate: KeyboardAction {
                        focusOnPointer: false
                        required property string modelData
                        objectName: "vinylAlbum" + modelData
                        width: 40
                        height: 36
                        accessibleName: modelData === "Save" && root.pageData.saved ? "Saved" : modelData
                        enabled: modelData === "Play" ? root.trackCount > 0 : modelData === "Shuffle" ? root.shuffleEnabled : modelData === "Queue" ? root.trackCount>0 : root.saveEnabled
                        opacity: enabled ? 1 : 0.35
                        onTriggered: {
                            if (modelData === "Play")
                                root.pageData.playTrack(0);
                            else if (modelData === "Shuffle")
                                root.pageData.shuffle();
                            else if(modelData === "Queue")
                                root.pageData.enqueueAlbum(root.pageData.shownAlbumId,false);
                            else
                                root.pageData.toggleSaved();
                        }
                        Rectangle {
                            anchors.fill: parent
                            radius: 10
                            color: parent.modelData === "Play" ? theme.gold : theme.glassTint
                            border.color: theme.edge
                        }
                        VinylControlIcon {
                            anchors.centerIn: parent
                            symbol: parent.modelData === "Play" ? "play"
                                : parent.modelData === "Shuffle" ? "shuffle"
                                : parent.modelData === "Save" ? (root.pageData.saved ? "saved" : "save")
                                : "queue"
                            color: parent.modelData === "Play" ? theme.biblioWashBottom
                                : parent.modelData === "Save" && root.pageData.saved ? theme.gold
                                : theme.inkDim
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
                        focusOnPointer: false
                        required property int index
                        objectName: "vinylDisc_" + index
                        width: 75
                        height: 30
                        accessibleName: "Disc " + ((root.pageData.shownEdition.discNumbers || [])[index] || index+1)
                        onTriggered: root.pageData.selectDisc(index)
                        Text {
                            anchors.centerIn: parent
                            text: "Disc " + ((root.pageData.shownEdition.discNumbers || [])[parent.index] || parent.index+1)
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
                    width: trackList.width - (root.queueActionsEnabled ? 40 : 0)
                    KeyboardAction {
                        focusOnPointer: false
                        objectName:"vinylTrackQueue_"+index;visible:root.queueActionsEnabled
                        x:parent.width+8;width:32;height:parent.height;accessibleName:"Add track to queue"
                        onTriggered:root.pageData.enqueueTrack(index,false)
                        Text {anchors.centerIn:parent;text:"+";color:theme.inkDim;font.family:theme.ui;font.pixelSize:18}
                    }
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
