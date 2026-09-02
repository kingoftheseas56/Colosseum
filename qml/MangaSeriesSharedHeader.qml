import QtQuick
import QtQuick.Controls
import QtQuick.Effects

Item {
    id: root
    objectName: "mangaSeriesSharedHeader"
    height: 263

    property bool tankobanMode: true
    property string seriesTitle: ""
    property string banner: ""
    property string cover: ""
    property string author: ""
    property string status: ""
    property int year: 0
    property real score: 0
    property string synopsis: ""
    property var collectionEntry: null
    readonly property real modeSwitchX: modeSwitch.x
    readonly property real modeSwitchY: modeSwitch.y
    readonly property real modeSwitchWidth: modeSwitch.width
    readonly property bool libraryAvailable: root.collectionEntry !== null && typeof Collection !== "undefined" && root.collectionEntry.id !== undefined && String(root.collectionEntry.id).length > 0
    readonly property bool librarySaved: root.libraryAvailable ? (Collection.revision, Collection.has("tankoban", String(root.collectionEntry.id))) : false
    readonly property real libraryX: titleBlock.x + titleRow.x + libraryIconButton.x
    readonly property real libraryY: hero.y + titleBlock.y + titleRow.y + libraryIconButton.y

    signal backRequested()
    signal minimizeRequested()
    signal fullscreenRequested()
    signal closeRequested()
    signal tankobanRequested()
    signal chapterRequested()

    function toggleLibrary() {
        if (!root.libraryAvailable) return
        var id = String(root.collectionEntry.id)
        if (root.librarySaved) Collection.remove("tankoban", id)
        else Collection.add("tankoban", root.collectionEntry)
    }

    Theme { id: theme }

    Rectangle { anchors.fill: parent; color: "#050608" }
    Item {
        id: chrome
        anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right
        height: 58; z: 20
        Rectangle {
            anchors.fill: parent
            color: Qt.rgba(.02, .025, .035, .90)
            border.width: 1; border.color: Qt.rgba(1,1,1,.06)
        }
        BackAction {
            objectName: root.tankobanMode ? "tankobanReadingRoomBack" : "mangaChapterSeriesBack"
            x: theme.margin
            anchors.verticalCenter: parent.verticalCenter
            activeFocusOnTab: true
            onTriggered: root.backRequested()
        }
        Text {
            anchors.centerIn: parent
            text: "Tankoban"
            color: "#deddd8"
            font.family: theme.ui; font.pixelSize: 15; font.weight: Font.DemiBold
        }
        Row {
            anchors.right: parent.right; anchors.rightMargin: theme.margin
            anchors.verticalCenter: parent.verticalCenter; spacing: 8
            Item {
                width: 36; height: 36; activeFocusOnTab: true
                Accessible.role: Accessible.Button; Accessible.name: "Minimize window"
                Image { anchors.centerIn: parent; width: 20; height: 20; source: "../assets/icons/minimize.svg"; opacity: minMa.containsMouse ? 1 : .72 }
                MouseArea { id: minMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: root.minimizeRequested() }
                Keys.onReturnPressed: root.minimizeRequested(); Keys.onEnterPressed: root.minimizeRequested()
            }
            Item {
                width: 36; height: 36; activeFocusOnTab: true
                Accessible.role: Accessible.Button; Accessible.name: "Toggle full screen"
                Image {
                    anchors.centerIn: parent; width: 20; height: 20
                    source: (typeof WindowMode !== "undefined" && WindowMode.shellWindowed)
                        ? "../assets/icons/fullscreen.svg" : "../assets/icons/fullscreen-exit.svg"
                    opacity: fullMa.containsMouse ? 1 : .72
                }
                MouseArea { id: fullMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: root.fullscreenRequested() }
                Keys.onReturnPressed: root.fullscreenRequested(); Keys.onEnterPressed: root.fullscreenRequested()
            }
            Item {
                width: 36; height: 36; activeFocusOnTab: true
                Accessible.role: Accessible.Button; Accessible.name: "Close series view"
                Image { anchors.centerIn: parent; width: 20; height: 20; source: "../assets/icons/power.svg"; opacity: closeMa.containsMouse ? 1 : .72 }
                MouseArea { id: closeMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: root.closeRequested() }
                Keys.onReturnPressed: root.closeRequested(); Keys.onEnterPressed: root.closeRequested()
            }
        }
    }
    Item {
        id: hero
        anchors.top: chrome.bottom; anchors.left: parent.left; anchors.right: parent.right
        height: 205; clip: true
        Image {
            anchors.fill: parent
            source: root.banner.length ? root.banner : root.cover
            fillMode: Image.PreserveAspectCrop
            asynchronous: true; cache: true
            opacity: status === Image.Ready ? .28 : 0
        }
        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                GradientStop { position: 0.0; color: Qt.rgba(.02,.025,.04,.20) }
                GradientStop { position: .62; color: Qt.rgba(.02,.025,.04,.58) }
                GradientStop { position: 1.0; color: Qt.rgba(.02,.025,.04,.94) }
            }
        }
        Column {
            id: titleBlock
            anchors.left: parent.left; anchors.leftMargin: theme.margin
            anchors.top: parent.top; anchors.topMargin: 22; spacing: 5
            Text { text: "MANGA"; color: theme.gold; font.family: theme.ui; font.pixelSize: 10; font.letterSpacing: 2.4 }
            Row {
                id: titleRow
                spacing: 10
                Text {
                    id: titleText
                    text: root.seriesTitle; color: theme.ink; font.family: theme.display; font.pixelSize: 34; font.weight: Font.DemiBold
                    elide: Text.ElideRight; width: Math.min(implicitWidth, 650, Math.max(120, hero.width * .55 - 46))
                    y: Math.round((titleRow.height - height) / 2)
                }
                Item {
                    id: libraryIconButton
                    objectName: "mangaSeriesLibraryButton"
                    width: 36; height: 36
                    visible: root.libraryAvailable
                    activeFocusOnTab: visible
                    Accessible.role: Accessible.Button
                    Accessible.name: root.librarySaved ? "Remove from Library" : "Add to Library"
                    ToolTip.visible: libraryMa.containsMouse
                    ToolTip.text: root.librarySaved ? "Remove from Library" : "Add to Library"
                    ToolTip.delay: 450
                    y: Math.round((titleRow.height - height) / 2)
                    Rectangle {
                        anchors.fill: parent; radius: 9
                        color: libraryMa.containsMouse ? Qt.rgba(1,1,1,.10) : (root.librarySaved ? Qt.rgba(0.78,.62,.22,.10) : "transparent")
                        border.width: root.librarySaved ? 1 : 0
                        border.color: Qt.rgba(0.78,.62,.22,.28)
                    }
                    Image {
                        id: libraryGlyph
                        anchors.centerIn: parent; width: 20; height: 20
                        source: root.librarySaved ? "../assets/icons/lucide/bookmark-check.svg" : "../assets/icons/lucide/bookmark-plus.svg"
                        sourceSize.width: 40; sourceSize.height: 40
                        fillMode: Image.PreserveAspectFit; smooth: true; cache: true; visible: false
                    }
                    MultiEffect { anchors.fill: libraryGlyph; source: libraryGlyph; colorization: 1.0; colorizationColor: root.librarySaved ? theme.gold : theme.ink }
                    MouseArea { id: libraryMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: root.toggleLibrary() }
                    Keys.onPressed: function(event) {
                        if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space) { root.toggleLibrary(); event.accepted = true }
                    }
                }
            }
            Row {
                spacing: 7
                Text { visible: root.author.length > 0; text: root.author; color: theme.inkDim; font.family: theme.ui; font.pixelSize: 13 }
                Text { visible: root.author.length > 0 && root.status.length > 0; text: "·"; color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 13 }
                Text { visible: root.status.length > 0; text: root.status; color: theme.inkDim; font.family: theme.ui; font.pixelSize: 13 }
                Text { visible: root.year > 0; text: "· " + root.year; color: theme.inkDim; font.family: theme.ui; font.pixelSize: 13 }
                Text { visible: root.score > 0; text: "★ " + root.score.toFixed(2); color: theme.inkDim; font.family: theme.ui; font.pixelSize: 13 }
            }
        }

        Text {
            id: synopsisText
            anchors.left: parent.left; anchors.leftMargin: theme.margin
            anchors.right: parent.right; anchors.rightMargin: 300
            anchors.top: parent.top; anchors.topMargin: 111
            text: root.synopsis
            color: theme.inkDim; font.family: theme.ui; font.pixelSize: 13; lineHeight: 1.45
            wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight
            visible: root.synopsis.length > 0
        }


        Rectangle {
            id: modeSwitch
            objectName: "mangaSeriesModeSwitch"
            anchors.right: parent.right; anchors.rightMargin: theme.margin
            anchors.top: parent.top; anchors.topMargin: 31
            width: 258; height: 36; radius: 10; z: 10
            color: Qt.rgba(.06,.065,.075,.92)
            border.width: 1; border.color: theme.edge
            Row {
                anchors.centerIn: parent
                Rectangle {
                    objectName: "mangaModeTankoban"
                    width: 125; height: 28; radius: 7
                    color: root.tankobanMode ? theme.gold : "transparent"
                    Text { anchors.centerIn: parent; text: "Tankoban Mode"; color: root.tankobanMode ? "#191407" : theme.inkDim; font.family: theme.ui; font.pixelSize: 12; font.weight: root.tankobanMode ? Font.DemiBold : Font.Normal }
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.tankobanRequested() }
                }
                Rectangle {
                    objectName: "mangaModeChapter"
                    width: 125; height: 28; radius: 7
                    color: !root.tankobanMode ? theme.gold : "transparent"
                    Text { anchors.centerIn: parent; text: "Chapter Mode"; color: !root.tankobanMode ? "#191407" : theme.inkDim; font.family: theme.ui; font.pixelSize: 12; font.weight: !root.tankobanMode ? Font.DemiBold : Font.Normal }
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.chapterRequested() }
                }
            }
        }
    }
}
