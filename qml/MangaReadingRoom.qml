// MangaReadingRoom - the glass-and-wallpaper Pages-flow surface.
//
// The series context is compressed into a story masthead. The collection owns
// the rest of the screen so the reader-derived volume flow can lead.
import QtQuick
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

    Rectangle { anchors.fill: parent; color: "#050608" }
    Image {
        anchors.fill: parent
        source: root.banner.length ? root.banner : root.cover
        sourceSize: Qt.size(Math.ceil(width * 0.72), Math.ceil(height * 0.72))
        fillMode: Image.PreserveAspectCrop
        asynchronous: true; cache: true
        opacity: status === Image.Ready ? 0.20 : 0.0
    }
    ShaderEffectSource {
        anchors.fill: parent
        sourceItem: root.backdrop
        live: true; hideSource: false
        visible: root.backdrop !== null
        opacity: 0.12
    }
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.rgba(0.015, 0.02, 0.032, 0.20) }
            GradientStop { position: 0.50; color: Qt.rgba(0.01, 0.012, 0.018, 0.52) }
            GradientStop { position: 1.0; color: Qt.rgba(0.005, 0.006, 0.01, 0.76) }
        }
    }

    Item {
        id: chrome
        anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right
        height: 58

        BackAction {
            x: theme.margin; anchors.verticalCenter: parent.verticalCenter
            activeFocusOnTab: true
            Accessible.role: Accessible.Button
            Accessible.name: "Back to series"
            Keys.onReturnPressed: root.backRequested()
            Keys.onEnterPressed: root.backRequested()
            onTriggered: root.backRequested()
        }
        Text {
            anchors.centerIn: parent
            text: "COLOSSEUM \u00b7 TANKOBAN"
            color: theme.inkDimmer
            font.family: theme.ui; font.pixelSize: 11; font.letterSpacing: 3
        }
        Row {
            anchors.right: parent.right; anchors.rightMargin: theme.margin
            anchors.verticalCenter: parent.verticalCenter; spacing: 12
            Item {
                width: 36; height: 36; activeFocusOnTab: true
                Accessible.role: Accessible.Button; Accessible.name: "Minimize window"
                Keys.onReturnPressed: root.minimizeRequested(); Keys.onEnterPressed: root.minimizeRequested()
                Image { anchors.centerIn: parent; width: 20; height: 20; source: "../assets/icons/minimize.svg"; opacity: minMouse.containsMouse ? 1 : 0.72 }
                MouseArea { id: minMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: root.minimizeRequested() }
            }
            Item {
                width: 36; height: 36; activeFocusOnTab: true
                Accessible.role: Accessible.Button; Accessible.name: "Enter full screen"
                Keys.onReturnPressed: root.fullscreenRequested(); Keys.onEnterPressed: root.fullscreenRequested()
                Image {
                    anchors.centerIn: parent; width: 20; height: 20
                    source: (typeof WindowMode !== "undefined" && WindowMode.shellWindowed)
                        ? "../assets/icons/fullscreen.svg" : "../assets/icons/fullscreen-exit.svg"
                    opacity: fullscreenMouse.containsMouse ? 1 : 0.72
                }
                MouseArea { id: fullscreenMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: root.fullscreenRequested() }
            }
            Item {
                width: 36; height: 36; activeFocusOnTab: true
                Accessible.role: Accessible.Button; Accessible.name: "Close series view"
                Keys.onReturnPressed: root.closeRequested(); Keys.onEnterPressed: root.closeRequested()
                Image { anchors.centerIn: parent; width: 20; height: 20; source: "../assets/icons/power.svg"; opacity: closeMouse.containsMouse ? 1 : 0.72 }
                MouseArea { id: closeMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: root.closeRequested() }
            }
        }
    }

    Item {
        id: storyMasthead
        anchors.top: chrome.bottom; anchors.left: parent.left; anchors.right: parent.right
        anchors.leftMargin: theme.margin; anchors.rightMargin: theme.margin
        readonly property int baseHeight: root.height < 760 ? 118 : 104
        // Grows only for the synopsis line (mock: clamped to one line, expands on tap).
        // Everything else in the strip keeps its original fixed geometry.
        height: root.synopsis.length > 0 ? baseHeight + synopsisBlock.height + 8 : baseHeight

        Rectangle {
            anchors.fill: parent; radius: 14
            color: Qt.rgba(0.04, 0.05, 0.065, 0.72)
            border.width: 1; border.color: Qt.rgba(1, 1, 1, 0.13)
        }

        Item {
            id: storyIdentity
            anchors.left: parent.left; anchors.right: storyActions.left; anchors.top: parent.top; anchors.bottom: parent.bottom
            anchors.leftMargin: 20; anchors.topMargin: 15; anchors.bottomMargin: 14
            Text {
                id: storyTitle
                anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right; anchors.rightMargin: 20
                text: root.seriesTitle; color: theme.ink; font.family: theme.display; font.pixelSize: 30; font.weight: Font.DemiBold
                elide: Text.ElideRight; maximumLineCount: 1
            }
            Row {
                id: metaRow
                anchors.top: parent.top; anchors.topMargin: storyTitle.implicitHeight + 12; anchors.left: parent.left; spacing: 7
                Text { visible: root.author.length > 0; text: root.author; color: theme.ink; font.family: theme.ui; font.pixelSize: 11; font.weight: Font.DemiBold }
                Text { visible: root.author.length > 0 && root.status.length > 0; text: "\u00b7"; color: theme.inkDimmer }
                Text { visible: root.status.length > 0; text: root.status; color: theme.inkDim }
                Text { visible: root.year > 0; text: "\u00b7 " + root.year; color: theme.inkDim }
                Image { visible: root.score > 0; source: "../assets/icons/rating-star.svg"; width: 12; height: 12 }
                Text { visible: root.score > 0; text: root.score.toFixed(1); color: theme.gold; font.family: theme.ui; font.pixelSize: 11; font.weight: Font.DemiBold }
                // Anime-Planet stat line: volumes/chapters promoted to full ink (mock .meta .stat).
                Text {
                    visible: root.library.showVolumes && (root.author.length > 0 || root.status.length > 0 || root.year > 0 || root.score > 0)
                    text: "\u00b7"; color: theme.inkDimmer
                }
                Text {
                    visible: root.library.showVolumes
                    text: root.library.volumeRows.length + " volumes"; color: theme.ink; font.family: theme.ui; font.pixelSize: 11
                }
                Text {
                    visible: root.chapters.length > 0 && (root.library.showVolumes || root.author.length > 0 || root.status.length > 0 || root.year > 0 || root.score > 0)
                    text: "\u00b7"; color: theme.inkDimmer
                }
                Text {
                    visible: root.chapters.length > 0
                    text: root.chapters.length + " chapters"; color: theme.ink; font.family: theme.ui; font.pixelSize: 11
                }
            }
            // Synopsis behind a tap (mock .syn): clamped to one line, tap expands/collapses.
            Item {
                id: synopsisBlock
                anchors.top: metaRow.bottom; anchors.topMargin: 10
                anchors.left: parent.left; anchors.right: parent.right; anchors.rightMargin: 20
                visible: root.synopsis.length > 0
                height: visible ? synopsisText.height + synopsisMore.height + 3 : 0

                Text {
                    id: synopsisText
                    width: parent.width
                    text: root.synopsis
                    color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 13
                    wrapMode: Text.WordWrap
                    maximumLineCount: root.synopsisExpanded ? 100000 : 1
                    elide: Text.ElideRight
                }
                Text {
                    id: synopsisMore
                    anchors.top: synopsisText.bottom; anchors.topMargin: 3
                    text: root.synopsisExpanded ? "less" : "more"
                    color: theme.inkDim; font.family: theme.ui; font.pixelSize: 11; font.letterSpacing: 0.4
                }
                MouseArea {
                    anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                    onClicked: root.synopsisExpanded = !root.synopsisExpanded
                }
            }
        }

        Item {
            id: storyActions
            anchors.right: parent.right; anchors.top: parent.top; anchors.bottom: parent.bottom
            anchors.rightMargin: 20; anchors.topMargin: 15; anchors.bottomMargin: 14
            width: Math.max(360, parent.width * 0.26)
            Text { id: deviceLabel; text: "ON THIS DEVICE"; color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 10; font.letterSpacing: 2.2 }
            Text {
                id: deviceCount
                anchors.left: deviceLabel.right; anchors.leftMargin: 12; anchors.verticalCenter: deviceLabel.verticalCenter
                text: root.library.ownedCount + " OF " + root.library.volumeRows.length
                color: theme.ink; font.family: theme.display; font.pixelSize: 18; font.weight: Font.DemiBold
            }
            Rectangle {
                id: deviceProgress
                anchors.left: parent.left; anchors.right: parent.right; anchors.top: deviceLabel.bottom; anchors.topMargin: 8
                height: 2; color: Qt.rgba(1, 1, 1, 0.13)
                Rectangle { width: root.library.volumeRows.length > 0 ? root.library.ownedCount / root.library.volumeRows.length * parent.width : 0; height: parent.height; color: theme.gold }
            }
            Row {
                id: actionRow
                anchors.left: parent.left; anchors.top: deviceProgress.bottom; anchors.topMargin: 10; spacing: 8
                Rectangle {
                    width: openLabel.implicitWidth + 30; height: 34; radius: 8; color: theme.gold
                    activeFocusOnTab: true; Accessible.role: Accessible.Button; Accessible.name: root.continueText
                    Keys.onReturnPressed: root.primaryRequested(); Keys.onEnterPressed: root.primaryRequested()
                    Row { id: openLabel; anchors.centerIn: parent; spacing: 7
                        Image { source: "../assets/icons/play-dark.svg"; width: 13; height: 13 }
                        Text { text: root.continueText; color: "#171205"; font.family: theme.ui; font.pixelSize: 11; font.weight: Font.DemiBold; anchors.verticalCenter: parent.verticalCenter }
                    }
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.primaryRequested() }
                }
                Loader {
                    active: root.collectionEntry !== null && typeof Collection !== "undefined"
                    sourceComponent: LibraryButton { world: "tankoban"; entry: root.collectionEntry }
                }
            }
        }
    }

    Item {
        id: collectionSurface
        anchors.top: storyMasthead.bottom; anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
        anchors.leftMargin: theme.margin; anchors.rightMargin: theme.margin; anchors.topMargin: 10; anchors.bottomMargin: 10
        MangaTankobanLibrary {
            id: tankLib
            objectName: "readingRoomLibrary"
            anchors.fill: parent
            seriesId: root.seriesId; seriesTitle: root.seriesTitle
            chapters: root.chapters; chapterRows: root.chapterDisplayRows
            service: root.service; progress: root.progress; downloader: root.downloader
            onOpenVolumeRequested: (volumeId) => root.openVolumeRequested(volumeId)
            onSourcesRequested: (ctx) => root.sourcesRequested(ctx)
            onBatchRequested: (numbers, label) => root.batchRequested(numbers, label)
            onOpenChapterRequested: (chapterId, label) => root.openChapterRequested(chapterId, label)
            onChapterDownloadRequested: (chapterId, label) => root.chapterDownloadRequested(chapterId, label)
        }
        Rectangle {
            visible: root.errorText.length > 0; z: 30
            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; height: 34
            color: Qt.rgba(0.08, 0.04, 0.05, 0.92)
            Text { anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12; verticalAlignment: Text.AlignVCenter
                text: root.errorText; color: "#e6a3a3"; font.family: theme.ui; font.pixelSize: 12; elide: Text.ElideRight; maximumLineCount: 1 }
        }
    }

    property alias library: tankLib
    readonly property string continueText: {
        if (root.library.continueVolumeId.length) {
            var n = root.library.currentNumber
            if (root.library.continueMax > 0)
                return "Continue \u00b7 Vol. " + n + " \u00b7 p. " + root.library.continuePage
            return "Continue \u00b7 Vol. " + n
        }
        return root.library.showVolumes ? "Open volume 1" : "Read first chapter"
    }
}
