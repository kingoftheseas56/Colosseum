// MangaReadingRoom - the fixed split surface for a manga series.
// The rail never scrolls. MangaTankobanLibrary is the only moving surface.
import QtQuick
import QtQuick.Controls
import "MangaVolumes.js" as Vol

Item {
    id: root
    objectName: "mangaReadingRoom"

    property Item backdrop
    property string seriesId: ""
    property string seriesTitle: ""
    property string banner: ""
    property string cover: ""
    property string author: ""
    property string status: ""
    property int year: 0
    property string synopsis: ""
    property string errorText: ""
    property var genres: []
    property real score: 0
    property var chapters: []
    property var collectionEntry: null
    property var service: null
    property var progress: null
    property var downloader: null
    property bool synopsisExpanded: false
    readonly property real contentHeight: root.height
    readonly property var chapterDisplayRows: {
        if (!root.library.showVolumes) return root.chapters || []
        var grouped = Vol.group(root.chapters || [], Vol.fromEngine(root.library.volumeRows || []))
        return grouped.byKey && grouped.byKey.X ? grouped.byKey.X : []
    }

    signal backRequested()
    signal minimizeRequested()
    signal fullscreenRequested()
    signal closeRequested()
    signal primaryRequested()
    signal openVolumeRequested(string volumeId)
    signal sourcesRequested(var context)
    signal batchRequested(var numbers, string label)
    signal openChapterRequested(string chapterId, string label)
    signal chapterDownloadRequested(string chapterId, string label)

    Theme { id: theme }

    Rectangle { anchors.fill: parent; color: "#000000" }
    Image {
        anchors.fill: parent
        source: root.banner.length ? root.banner : root.cover
        sourceSize: Qt.size(Math.ceil(width * 0.8), Math.ceil(height * 0.8))
        fillMode: Image.PreserveAspectCrop
        asynchronous: true; cache: true
        opacity: status === Image.Ready ? 0.18 : 0.0
    }
    ShaderEffectSource {
        anchors.fill: parent
        sourceItem: root.backdrop
        live: true; hideSource: false
        visible: root.backdrop !== null
        opacity: 0.32
    }
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.rgba(0.02, 0.025, 0.04, 0.34) }
            GradientStop { position: 0.48; color: Qt.rgba(0.02, 0.02, 0.03, 0.80) }
            GradientStop { position: 1.0; color: Qt.rgba(0.01, 0.01, 0.015, 0.96) }
        }
    }

    Item {
        id: chrome
        anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right
        height: 74
        BackAction { x: theme.margin; anchors.verticalCenter: parent.verticalCenter
            onTriggered: root.backRequested() }
        Text { anchors.centerIn: parent; text: "COLOSSEUM · TANKOBAN"; color: theme.inkDimmer
            font.family: theme.ui; font.pixelSize: 11; font.letterSpacing: 3 }
            Row {
                anchors.right: parent.right; anchors.rightMargin: theme.margin
                anchors.verticalCenter: parent.verticalCenter; spacing: 20
            Item { width: 44; height: 44
                Image { anchors.centerIn: parent; width: 22; height: 22; source: "../assets/icons/minimize.svg"; opacity: minMa.containsMouse ? 1 : 0.72 }
                MouseArea { id: minMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                    onClicked: root.minimizeRequested() }
            }
            Item { width: 44; height: 44
                Image { anchors.centerIn: parent; width: 22; height: 22
                    source: (typeof WindowMode !== "undefined" && WindowMode.shellWindowed)
                        ? "../assets/icons/fullscreen.svg" : "../assets/icons/fullscreen-exit.svg"
                    opacity: fsMa.containsMouse ? 1 : 0.72 }
                MouseArea { id: fsMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                    onClicked: root.fullscreenRequested() }
            }
            Item { width: 44; height: 44
                Image { anchors.centerIn: parent; width: 22; height: 22; source: "../assets/icons/power.svg"; opacity: closeMa.containsMouse ? 1 : 0.72 }
                MouseArea { id: closeMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                    onClicked: root.closeRequested() }
            }
        }
    }

    Item {
        id: roomBody
        anchors.top: chrome.bottom; anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom

        Item {
            id: rail
            width: Math.min(426, Math.max(350, parent.width * 0.31))
            anchors.top: parent.top; anchors.bottom: parent.bottom; anchors.left: parent.left
            anchors.leftMargin: theme.margin; anchors.rightMargin: 36

            readonly property int coverWidth: root.height < 650 ? 170 : 236
            readonly property int lineHeight: 21
            readonly property int synopsisFloor: 3 * lineHeight

            Column {
                id: railTop
                anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                spacing: 0
                Image {
                    width: rail.coverWidth; height: width * 1.5
                    source: root.cover
                    sourceSize: Qt.size(Math.ceil(width * 2), Math.ceil(height * 2))
                    asynchronous: true; cache: true
                    fillMode: Image.PreserveAspectCrop
                    visible: status === Image.Ready
                }
                Rectangle {
                    visible: parent.children[0].status !== Image.Ready
                    width: rail.coverWidth; height: width * 1.5; color: Qt.rgba(1, 1, 1, 0.06)
                    Text { anchors.centerIn: parent; text: root.seriesTitle.slice(0, 2).toUpperCase()
                        color: Qt.rgba(1, 1, 1, 0.22); font.family: theme.display; font.pixelSize: 48; font.weight: Font.Black }
                }
                Text { text: "Manga · Tankoban"; color: theme.gold; font.family: theme.ui; font.pixelSize: 11
                    font.letterSpacing: 3; font.capitalization: Font.AllUppercase; topPadding: 20; bottomPadding: 9 }
                Text { width: parent.width; text: root.seriesTitle; color: theme.ink
                    font.family: theme.display; font.pixelSize: 42; font.weight: Font.DemiBold
                    wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight }
                Row {
                    spacing: 8; topPadding: 12; bottomPadding: 5
                    Text { visible: root.author.length > 0; text: root.author; color: theme.ink
                        font.family: theme.ui; font.pixelSize: 13; font.weight: Font.DemiBold }
                    Text { visible: root.author.length > 0 && root.status.length > 0; text: "·"; color: theme.inkDimmer }
                    Text { visible: root.status.length > 0; text: root.status; color: theme.inkDim }
                    Text { visible: root.status.length > 0 && root.year > 0; text: "·"; color: theme.inkDimmer }
                    Text { visible: root.year > 0; text: root.year; color: theme.inkDim }
                    Text { visible: root.score > 0; text: "·"; color: theme.inkDimmer }
                    Image { visible: root.score > 0; source: "../assets/icons/rating-star.svg"; width: 13; height: 13 }
                    Text { visible: root.score > 0; text: root.score.toFixed(1); color: theme.gold
                        font.family: theme.ui; font.pixelSize: 13; font.weight: Font.DemiBold }
                }
                Text { width: parent.width; text: root.genres.slice(0, 3).join(" · "); color: theme.inkDimmer
                    font.family: theme.ui; font.pixelSize: 12; elide: Text.ElideRight; maximumLineCount: 1
                    bottomPadding: 2 }
            }

            Item {
                id: synopsisBox
                anchors.left: parent.left; anchors.right: parent.right
                y: railTop.y + railTop.height + 13
                readonly property real availableHeight: Math.max(rail.synopsisFloor, moreButton.y - y - 4)
                height: root.synopsisExpanded
                    ? availableHeight : Math.min(availableHeight, rail.synopsisFloor)
                clip: true
                Text {
                    id: synopsisText
                    anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                    text: root.synopsis; color: theme.inkDim; font.family: theme.ui; font.pixelSize: 13
                    lineHeight: 1.55; wrapMode: Text.WordWrap
                    maximumLineCount: root.synopsisExpanded ? 0 : Math.max(3, Math.floor(synopsisBox.height / rail.lineHeight))
                    elide: Text.ElideRight
                }
                Rectangle {
                    visible: !root.synopsisExpanded && synopsisText.implicitHeight > synopsisBox.height
                    anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                    height: 28
                    gradient: Gradient {
                        GradientStop { position: 0; color: Qt.rgba(0, 0, 0, 0) }
                        GradientStop { position: 1; color: Qt.rgba(0, 0, 0, 0.92) }
                    }
                }
            }
            Text {
                id: moreButton
                anchors.left: parent.left; anchors.bottom: bottomStack.top; anchors.bottomMargin: 8
                text: root.synopsisExpanded ? "Less" : "More"
                color: moreMa.containsMouse ? theme.gold : theme.inkDimmer
                font.family: theme.ui; font.pixelSize: 12
                MouseArea { id: moreMa; anchors.fill: parent; anchors.margins: -8; hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor; onClicked: root.synopsisExpanded = !root.synopsisExpanded }
            }

            Column {
                id: bottomStack
                anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                spacing: 12
                Text { text: root.library.ownedCount + " of " + root.library.volumeRows.length + " on this device"
                    visible: root.library.showVolumes; color: theme.inkDim; font.family: theme.ui; font.pixelSize: 12 }
                Rectangle { visible: root.library.showVolumes; width: parent.width; height: 2; color: Qt.rgba(1,1,1,0.12)
                    Rectangle { width: root.library.volumeRows.length > 0
                            ? root.library.ownedCount / root.library.volumeRows.length * parent.width : 0
                        height: parent.height; color: theme.gold } }
                Row {
                    spacing: 10
                    Rectangle {
                        width: continueLabel.implicitWidth + 36; height: 46; radius: 10; color: theme.gold
                        Row { id: continueLabel; anchors.centerIn: parent; spacing: 8
                            Image { source: "../assets/icons/play.svg"; width: 15; height: 15 }
                            Text { text: root.continueText; color: "#1a1306"; font.family: theme.ui; font.pixelSize: 12
                                font.weight: Font.DemiBold; anchors.verticalCenter: parent.verticalCenter }
                        }
                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                            onClicked: root.primaryRequested() }
                    }
                    Loader {
                        active: root.collectionEntry !== null && typeof Collection !== "undefined"
                        sourceComponent: LibraryButton {
                            world: "tankoban"
                            entry: root.collectionEntry
                        }
                    }
                }
            }
        }

        Item {
            id: pane
            anchors.top: parent.top; anchors.bottom: parent.bottom
            anchors.left: rail.right; anchors.right: parent.right; anchors.rightMargin: theme.margin
            MangaTankobanLibrary {
                id: tankLib
                objectName: "readingRoomLibrary"
                anchors.fill: parent
                seriesId: root.seriesId
                seriesTitle: root.seriesTitle
                chapters: root.chapters
                chapterRows: root.chapterDisplayRows
                service: root.service
                progress: root.progress
                downloader: root.downloader
                onOpenVolumeRequested: (volumeId) => root.openVolumeRequested(volumeId)
                onSourcesRequested: (ctx) => root.sourcesRequested(ctx)
                onBatchRequested: (numbers, label) => root.batchRequested(numbers, label)
                onOpenChapterRequested: (chapterId, label) => root.openChapterRequested(chapterId, label)
                onChapterDownloadRequested: (chapterId, label) => root.chapterDownloadRequested(chapterId, label)
            }
            Rectangle {
                visible: root.errorText.length > 0
                z: 30
                anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                height: 34
                color: Qt.rgba(1, 1, 1, 0.06)
                Text { anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12
                    verticalAlignment: Text.AlignVCenter; text: root.errorText
                    color: "#e6a3a3"; font.family: theme.ui; font.pixelSize: 12
                    elide: Text.ElideRight; maximumLineCount: 1 }
            }
        }
    }

    property alias library: tankLib
    readonly property string continueText: {
        if (root.library.continueVolumeId.length) {
            var n = root.library.currentNumber
            if (root.library.continueMax > 0)
                return "Continue — Vol. " + n + " · page " + root.library.continuePage
                    + " of " + root.library.continueMax
            return "Continue — Vol. " + n
        }
        return root.library.showVolumes ? "Open first volume" : "Read first chapter"
    }
}
