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
    property Component mapSlide
    property Item activityControl
    property int focusColumn: 0
    property bool focusOnArrival: false
    property real wheelTravel: 0
    property bool wheelStepTaken: false
    signal pickRequested(string medium, var entry)
    signal wallpaperRequested()
    signal chooseFilesRequested()
    signal recentRequested()
    signal joinRequested()
    signal extensionsRequested()
    signal activityRequested()

    readonly property real inset: Math.min(24, height * .045)
    readonly property real railWidth: Math.min(208, width * .23)
    readonly property int slideCount: mapSlide ? 4 : 3
    readonly property var slideNames: [
        qsTr("Anime, TV and Movies"), qsTr("Manga, Comics and Manhwa"),
        qsTr("Books, Audiobooks and Franchises"), qsTr("Colosseum map")
    ]
    readonly property var genres: [
        { key: "anime", name: qsTr("Anime") },
        { key: "tv", name: qsTr("TV") },
        { key: "movies", name: qsTr("Movies") },
        { key: "manga", name: qsTr("Manga") },
        { key: "comics", name: qsTr("Comics") },
        { key: "manhwa", name: qsTr("Manhwa") },
        { key: "books", name: qsTr("Books") },
        { key: "audiobooks", name: qsTr("Audiobooks"), aspect: 1 },
        { key: "universes", name: qsTr("Franchises") }
    ]
    Theme { id: theme }

    function selectSlide(index, keyboard) {
        if (index < 0 || index >= slideCount || index === slide)
            return
        focusOnArrival = !!keyboard || focusOnArrival || viewport.activeFocus
        if (focusOnArrival)
            focusEntry()
        slide = index
        Qt.callLater(picker.restorePageFocus)
    }
    function restorePageFocus() {
        if (pageMotion.running || !focusOnArrival)
            return
        focusOnArrival = false
        const row = pageRepeater.itemAt(slide) as PickRow
        if (entryFocus.activeFocus && row)
            row.focusDeck(focusColumn)
    }
    function clearWheel() {
        wheelTravel = 0
        wheelStepTaken = false
    }
    function pageWheel(deltaX, deltaY, phase) {
        if (phase === Qt.ScrollMomentum)
            return
        if (phase === Qt.ScrollBegin || phase === Qt.ScrollEnd) {
            wheelQuiet.stop()
            clearWheel()
            if (phase === Qt.ScrollEnd)
                return
        }
        if (phase === Qt.NoScrollPhase)
            wheelQuiet.restart()
        if (wheelStepTaken || Math.abs(deltaY) <= Math.abs(deltaX))
            return
        if (wheelTravel * deltaY < 0)
            wheelTravel = 0
        wheelTravel += deltaY
        if (Math.abs(wheelTravel) >= 40) {
            wheelStepTaken = true
            selectSlide(slide + (wheelTravel < 0 ? 1 : -1))
        }
    }
    function pageSwipe(dy) {
        if (Math.abs(dy) >= 48)
            selectSlide(slide + (dy < 0 ? 1 : -1))
    }
    onMapSlideChanged: if (!mapSlide && slide === 3) selectSlide(2)
    onVisibleChanged: if (!visible) {
        focusOnArrival = false
        focusColumn = 0
        slide = 0
        wheelQuiet.stop()
        clearWheel()
    }
    Keys.onPressed: event => {
        if (event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier))
            return
        const vertical = viewport.activeFocus || entryFocus.activeFocus || pageDots.activeFocus
        const up = event.key === Qt.Key_PageUp || (vertical && event.key === Qt.Key_Up)
        const down = event.key === Qt.Key_PageDown || (vertical && event.key === Qt.Key_Down)
        if (!up && !down)
            return
        event.accepted = true
        if (!event.isAutoRepeat)
            picker.selectSlide(picker.slide + (up ? -1 : 1), true)
    }
    Timer { id: wheelQuiet; interval: 200; onTriggered: picker.clearWheel() }

    function focusEntry() {
        entryFocus.forceActiveFocus(Qt.PopupFocusReason)
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
        onWheel: event => event.accepted = true
    }

    Item { id: entryFocus }

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
                    focusOnPointer: false
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
        anchors.topMargin: Math.min(14, picker.inset)
        anchors.bottomMargin: Math.min(12, picker.inset)
        anchors.leftMargin: picker.railWidth + 19
        anchors.rightMargin: 31
        clip: true

        Text {
            id: pickHeading
            objectName: "dockPickHeading"
            visible: picker.slide !== 3
            width: widgetArea.width
            height: Math.ceil(implicitHeight)
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
            text: qsTr("Pick Something")
            color: "#fff5e6"
            font.family: theme.display
            font.weight: Font.DemiBold
            font.pixelSize: theme.fontSize(Math.round(Math.max(24, Math.min(40, width / 24, widgetArea.height / 13))))
            fontSizeMode: Text.HorizontalFit
            minimumPixelSize: theme.fontSize(16)
            Accessible.role: Accessible.Heading
            Accessible.name: text
        }

        FocusScope {
            id: viewport
            objectName: "dockPickViewport"
            y: pickHeading.height + 4
            width: widgetArea.width
            height: Math.max(0, widgetArea.height - y)
            clip: true

            MouseArea {
                anchors.fill: parent
                property point start
                onPressed: mouse => {
                    mouse.accepted = !pageMotion.running
                    start = Qt.point(mouse.x, mouse.y)
                }
                onReleased: mouse => {
                    const dy = mouse.y - start.y
                    if (Math.abs(dy) > Math.abs(mouse.x - start.x))
                        picker.pageSwipe(dy)
                }
                onWheel: wheel => {
                    wheel.accepted = true
                    const pixelScroll = wheel.pixelDelta.x !== 0 || wheel.pixelDelta.y !== 0
                    picker.pageWheel(pixelScroll ? wheel.pixelDelta.x : wheel.angleDelta.x / 3,
                                     pixelScroll ? wheel.pixelDelta.y : wheel.angleDelta.y / 3, wheel.phase)
                }
            }

            Item {
                id: pageStack
                width: viewport.width
                height: viewport.height
                property real pagePosition: picker.slide
                y: -pagePosition * height
                Behavior on pagePosition {
                    enabled: picker.visible && !picker.reducedMotion
                    NumberAnimation {
                        id: pageMotion
                        duration: 230
                        easing.type: Easing.OutCubic
                        onRunningChanged: if (!running) Qt.callLater(picker.restorePageFocus)
                    }
                }
                Repeater {
                    id: pageRepeater
                    model: 3
                    delegate: PickRow {
                        required property int index
                        objectName: "dockPickPage_" + index
                        rowIndex: index
                    }
                }
                Loader {
                    id: mapPage
                    objectName: "dockPickMapPage"
                    y: 3 * pageStack.height
                    width: pageStack.width
                    height: pageStack.height
                    visible: !!picker.mapSlide && (picker.slide === 3 || (pageMotion.running && mapPage.y + pageStack.y < viewport.height))
                    enabled: picker.slide === 3 && !pageMotion.running
                    sourceComponent: picker.mapSlide
                }
            }
        }
    }

    component PickRow: Grid {
        id: row
        required property int rowIndex
        x: Math.min(12, pageStack.width * .025)
        y: rowIndex * pageStack.height
        width: Math.max(0, pageStack.width - 2 * x)
        height: pageStack.height
        columns: 3
        columnSpacing: Math.min(12, width * .025)
        visible: picker.slide === rowIndex
            || (pageMotion.running && y + pageStack.y < viewport.height && y + pageStack.y + height > 0)
        enabled: picker.slide === rowIndex && !pageMotion.running
        readonly property real cellWidth: Math.max(0, (width - 2 * columnSpacing) / columns)

        function focusDeck(column) {
            const preferred = decks.itemAt(column) as DockPickDeck
            if (preferred && preferred.count > 0) {
                preferred.focusCover()
                return
            }
            for (let i = 0; i < decks.count; ++i) {
                const deck = decks.itemAt(i) as DockPickDeck
                if (deck.count > 0) {
                    deck.focusCover()
                    return
                }
            }
        }

        Repeater {
            id: decks
            model: picker.genres.slice(row.rowIndex * 3, row.rowIndex * 3 + 3)
            delegate: DockPickDeck {
                id: deck
                required property int index
                required property var modelData
                objectName: "dockDeck_" + modelData.key
                width: row.cellWidth
                height: row.height
                headingExpansion: Math.max(0, pickHeading.height - 28)
                leftMotionRoom: row.x
                medium: modelData.name
                mediumKey: modelData.key
                aspect: modelData.aspect || .71
                entries: picker.picks[modelData.key] || []
                logoArt: modelData.key === "universes"
                reducedMotion: picker.reducedMotion
                onActiveFocusChanged: if (activeFocus) picker.focusColumn = index
                onVerticalSwipeRequested: dy => picker.pageSwipe(dy)
                onOpenRequested: entry => picker.pickRequested(modelData.key, entry)
            }
        }
    }

    FocusScope {
        id: pageDots
        width: 27
        height: width * picker.slideCount
        z: 1
        anchors.right: parent.right
        anchors.rightMargin: 2
        anchors.verticalCenter: parent.verticalCenter
        Repeater {
            model: picker.slideCount
            delegate: Item {
                id: dot
                required property int index
                y: index * height
                width: pageDots.width
                height: width
                Rectangle {
                    anchors.centerIn: parent
                    width: picker.slide === dot.index ? 8 : 6
                    height: width
                    radius: width / 2
                    color: picker.slide === dot.index ? theme.gold : "#91ffffff"
                }
                KeyboardAction {
                    id: dotAction
                    objectName: "dockSlideDot_" + dot.index
                    anchors.fill: parent
                    focusOnPointer: false
                    accessibleName: picker.slideNames[dot.index]
                    focusRadius: 13
                    spaceActivates: true
                    onTriggered: picker.selectSlide(dot.index, dotAction.activeFocus)
                }
            }
        }
    }
}
