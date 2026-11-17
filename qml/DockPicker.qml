pragma ComponentBehavior: Bound
import QtQuick

FocusScope {
    id: picker
    objectName: "dockPicker"
    property var picks: ({})
    property bool reducedMotion: false
    property bool mediaAvailable: true
    property string activityLabel: qsTr("Your Colosseum")
    property int slide: 0
    property Item activityControl
    signal pickRequested(string medium, var entry)
    signal wallpaperRequested()
    signal filesDropped(var urls)
    signal chooseFilesRequested()
    signal recentRequested()
    signal joinRequested()
    signal extensionsRequested()
    signal activityRequested()

    readonly property real inset: Math.min(24, height * .045)
    readonly property real railWidth: Math.min(220, width * .23)
    readonly property var genres: [
        { key: "manga", name: qsTr("Manga") },
        { key: "comics", name: qsTr("Comics") },
        { key: "music", name: qsTr("Music") },
        { key: "books", name: qsTr("Books") },
        { key: "audiobooks", name: qsTr("Audiobooks"), aspect: 1 },
        { key: "anime", name: qsTr("Anime") },
        { key: "tv", name: qsTr("TV") },
        { key: "movies", name: qsTr("Movies") },
        { key: "tributes", name: qsTr("Tributes"), aspect: 1.2 }
    ]
    Theme { id: theme }

    function selectSlide(index) {
        if (index === 0 || index === 2) slide = index
    }
    function focusActivity() {
        if (activityControl) activityControl.forceActiveFocus(Qt.TabFocusReason)
    }

    Rectangle {
        anchors.fill: parent
        radius: 18
        color: "#7210141c"
        border.color: "#30ffffff"
    }

    MouseArea {
        anchors.fill: parent
        property point start
        onPressed: mouse => start = Qt.point(mouse.x, mouse.y)
        onReleased: mouse => {
            const dx = mouse.x - start.x
            const dy = mouse.y - start.y
            if (start.x > picker.railWidth + picker.inset && Math.abs(dx) > Math.max(48, Math.abs(dy) * 1.4))
                picker.selectSlide(dx < 0 ? 2 : 0)
        }
        onWheel: wheel => wheel.accepted = true
    }

    Column {
        x: picker.inset
        y: picker.inset
        width: picker.railWidth - picker.inset
        spacing: Math.min(10, picker.height * .018)

        Text {
            text: qsTr("Colosseum")
            font.family: theme.display
            font.pixelSize: 23
            color: theme.ink
            bottomPadding: Math.min(16, picker.height * .03)
        }
        Repeater {
            model: [
                { key: "media", title: qsTr("Open Media"), icon: "open-media.svg", run: picker.chooseFilesRequested, available: picker.mediaAvailable },
                { key: "wallpaper", title: qsTr("Wallpaper"), icon: "comicreader/image.svg", run: picker.wallpaperRequested, available: true },
                { key: "watchParty", title: qsTr("Join Watch Party"), icon: "watch-party.svg", run: picker.joinRequested, available: true },
                { key: "extensions", title: qsTr("Extensions"), icon: "extensions.svg", run: picker.extensionsRequested, available: true },
                { key: "activity", title: picker.activityLabel, icon: "your-colosseum-laurel.svg", run: picker.activityRequested, available: true }
            ]
            delegate: Item {
                id: shortcut
                required property var modelData
                width: parent.width
                height: Math.min(52, picker.height * .13)
                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.min(43, shortcut.height)
                    height: width
                    radius: 12
                    color: input.interactionActive ? "#2affffff" : "#13ffffff"
                    Image {
                        anchors.centerIn: parent
                        width: 22; height: 22
                        source: "../assets/icons/" + shortcut.modelData.icon
                        sourceSize: Qt.size(44, 44)
                        opacity: shortcut.modelData.available ? 1 : .5
                    }
                }
                Text {
                    x: Math.min(43, shortcut.height) + 10
                    width: Math.max(0, shortcut.width - x)
                    anchors.verticalCenter: parent.verticalCenter
                    text: shortcut.modelData.title
                    color: theme.ink
                    opacity: shortcut.modelData.available ? 1 : .5
                    font.family: theme.ui
                    font.pixelSize: 14
                    elide: Text.ElideRight
                }
                KeyboardAction {
                    id: input
                    objectName: "dockShortcut_" + shortcut.modelData.key
                    anchors.fill: parent
                    enabled: shortcut.modelData.available
                    contextEnabled: shortcut.modelData.key === "media"
                    accessibleName: shortcut.modelData.title
                    tooltipText: accessibleName
                    focusRadius: 12
                    spaceActivates: true
                    onTriggered: shortcut.modelData.run()
                    onContextRequested: picker.recentRequested()
                    Component.onCompleted: if (shortcut.modelData.key === "activity") picker.activityControl = input
                }
            }
        }
    }

    Rectangle {
        x: picker.railWidth + 9
        y: picker.inset
        width: 1
        height: Math.max(0, picker.height - 2 * y)
        color: "#25ffffff"
    }

    Item {
        id: widgetArea
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.topMargin: picker.inset
        anchors.bottomMargin: picker.inset
        anchors.leftMargin: picker.railWidth + 25
        anchors.rightMargin: 31

        Grid {
            id: grid
            anchors.fill: parent
            visible: picker.slide === 0
            columns: 3
            rowSpacing: 5
            columnSpacing: 9
            Repeater {
                model: picker.genres
                delegate: DockPickDeck {
                    required property var modelData
                    objectName: "dockDeck_" + modelData.key
                    width: Math.max(0, (grid.width - 2 * grid.columnSpacing) / 3)
                    height: Math.max(0, (grid.height - 2 * grid.rowSpacing) / 3)
                    medium: modelData.name
                    mediumKey: modelData.key
                    aspect: modelData.aspect || .71
                    entries: picker.picks[modelData.key] || []
                    locked: modelData.key === "music"
                    reducedMotion: picker.reducedMotion
                    onOpenRequested: entry => picker.pickRequested(modelData.key, entry)
                }
            }
        }

        Loader {
            anchors.fill: parent
            active: picker.visible && picker.slide === 2
            sourceComponent: DockMediaWidgets {
                mediaAvailable: picker.mediaAvailable
                reducedMotion: picker.reducedMotion
                onWallpaperRequested: picker.wallpaperRequested()
                onChooseFilesRequested: picker.chooseFilesRequested()
                onFilesDropped: urls => picker.filesDropped(urls)
            }
        }
    }

    Column {
        anchors.right: parent.right
        anchors.rightMargin: 2
        anchors.verticalCenter: parent.verticalCenter
        Repeater {
            model: 4
            delegate: Item {
                required property int index
                width: 27
                height: 27
                readonly property bool available: index === 0 || index === 2
                Rectangle {
                    anchors.centerIn: parent
                    width: picker.slide === parent.index ? 8 : 6
                    height: width
                    radius: width / 2
                    color: picker.slide === parent.index ? theme.gold : parent.available ? "#91ffffff" : "#48ffffff"
                }
                KeyboardAction {
                    objectName: "dockSlideDot_" + parent.index
                    anchors.fill: parent
                    enabled: parent.available
                    focusOnPointer: false
                    accessibleName: parent.index === 0 ? qsTr("Pick Anything")
                        : parent.index === 2 ? qsTr("Wallpaper and Drop Media") : qsTr("Unavailable slide")
                    focusRadius: 13
                    spaceActivates: true
                    onTriggered: picker.selectSlide(parent.index)
                }
            }
        }
    }
}
