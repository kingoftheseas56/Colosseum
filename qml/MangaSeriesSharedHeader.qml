import QtQuick

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
    readonly property real libraryX: librarySlot.x
    readonly property real libraryY: librarySlot.y

    signal backRequested()
    signal minimizeRequested()
    signal fullscreenRequested()
    signal closeRequested()
    signal tankobanRequested()
    signal chapterRequested()

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
            anchors.left: parent.left; anchors.leftMargin: theme.margin
            anchors.top: parent.top; anchors.topMargin: 22; spacing: 5
            Text { text: "MANGA"; color: theme.gold; font.family: theme.ui; font.pixelSize: 10; font.letterSpacing: 2.4 }
            Text { text: root.seriesTitle; color: theme.ink; font.family: theme.display; font.pixelSize: 34; font.weight: Font.DemiBold; elide: Text.ElideRight; width: Math.min(650, hero.width * .55) }
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

        Item {
            id: librarySlot
            objectName: "mangaSeriesLibraryButton"
            anchors.left: parent.left; anchors.leftMargin: theme.margin
            anchors.bottom: parent.bottom; anchors.bottomMargin: 16
            width: libraryLoader.item ? libraryLoader.item.implicitWidth : 0
            height: libraryLoader.item ? libraryLoader.item.implicitHeight : 0
            visible: libraryLoader.active
            Loader {
                id: libraryLoader
                anchors.fill: parent
                active: root.collectionEntry !== null && typeof Collection !== "undefined"
                sourceComponent: LibraryButton { world: "tankoban"; entry: root.collectionEntry }
            }
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
