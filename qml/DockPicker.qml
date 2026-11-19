pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Effects

FocusScope {
    id: picker
    objectName: "dockPicker"
    property var picks: ({})
    property bool reducedMotion: false
    property bool mediaAvailable: true
    property string activityLabel: qsTr("Your Colosseum")
    property int deckPage: 0
    property bool showingMap: false
    property bool focusHeaderOnArrival: false
    property Component mapSlide
    property Item activityControl
    property int focusColumn: 0
    property bool focusOnArrival: false
    property var restoredPick: null
    property real wheelTravel: 0
    property bool wheelStepTaken: false
    property int artStage: 0
    property int artSlide: 0
    signal pickRequested(string medium, var entry, var returnState)
    signal wallpaperRequested()
    signal chooseFilesRequested()
    signal recentRequested()
    signal joinRequested()
    signal extensionsRequested()
    signal activityRequested()

    readonly property real inset: Math.min(24, height * .045)
    readonly property real railWidth: Math.min(208, width * .23)
    readonly property var deckPageNames: [
        qsTr("Anime, TV and Movies"), qsTr("Manga, Comics and Manhwa"),
        qsTr("Books, Audiobooks and Franchises")
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

    function selectDeckPage(index, keyboard) {
        if (showingMap || index < 0 || index >= deckPageNames.length || index === deckPage)
            return
        focusOnArrival = !!keyboard || focusOnArrival || viewport.activeFocus
        if (focusOnArrival)
            focusEntry()
        deckPage = index
        Qt.callLater(picker.restorePageFocus)
    }
    function showMap(show, keyboard) {
        if (show === showingMap || (show && !mapSlide))
            return
        if (deckPane.activeFocus || mapPane.activeFocus)
            focusEntry()
        focusOnArrival = false
        focusHeaderOnArrival = !!keyboard
        wheelQuiet.stop()
        clearWheel()
        showingMap = show
        Qt.callLater(picker.restorePageFocus)
    }
    function restorePageFocus() {
        if (slideMotion.running || pageMotion.running)
            return
        if (focusHeaderOnArrival) {
            focusHeaderOnArrival = false
            if (entryFocus.activeFocus)
                (showingMap ? mapHeading : pickHeading).focusArrow()
        } else if (focusOnArrival) {
            focusOnArrival = false
            const row = pageRepeater.itemAt(deckPage) as PickRow
            if (entryFocus.activeFocus && row)
                row.focusDeck(focusColumn)
        }
    }
    function entriesFor(medium) {
        const entries = picks[medium] || []
        if (!restoredPick || restoredPick.medium !== medium
                || entries.some(entry => String(entry.id || entry.title) === restoredPick.key))
            return entries
        return [restoredPick.entry].concat(entries)
    }
    function restorePick(state) {
        const row = pageRepeater.itemAt(state.slide) as PickRow
        if (!row)
            return false
        restoredPick = state
        deckPage = state.slide
        artSlide = state.slide
        row.selectPick(state.column, state.key)
        focusColumn = state.column
        focusOnArrival = state.keyboard
        return true
    }
    function clearWheel() {
        wheelTravel = 0
        wheelStepTaken = false
    }
    function pageWheel(deltaX, deltaY, phase) {
        if (showingMap || phase === Qt.ScrollMomentum)
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
            selectDeckPage(deckPage + (wheelTravel < 0 ? 1 : -1))
        }
    }
    function pageSwipe(dy) {
        if (Math.abs(dy) >= 48)
            selectDeckPage(deckPage + (dy < 0 ? 1 : -1))
    }
    onMapSlideChanged: if (!mapSlide) showMap(false, false)
    onVisibleChanged: if (visible) {
        artSlide = deckPage
        artKeepAlive.stop()
    } else {
        focusOnArrival = false
        focusColumn = 0
        focusHeaderOnArrival = false
        showingMap = false
        deckPage = 0
        restoredPick = null
        wheelQuiet.stop()
        clearWheel()
        artWarmup.stop()
        if (artStage) artKeepAlive.restart()
    }
    // Open with the three front cards. Add the current row's backing cards
    // in both directions, then the other rows' fronts and the next likely
    // row's nearest backing cards, after the first frame.
    Connections {
        target: picker.visible ? picker.Window.window : null
        enabled: picker.artStage < 4 && !artWarmup.running
        function onFrameSwapped() { artWarmup.start() }
    }
    Timer {
        id: artWarmup
        interval: 32
        repeat: true
        onTriggered: {
            ++picker.artStage
            if (picker.artStage === 4) stop()
        }
    }
    // Keep the source row's cache while it moves. Avoid allocating a new
    // row of artwork during the page animation.
    Timer {
        interval: 48
        running: picker.visible && !pageMotion.running && picker.artSlide !== picker.deckPage
        onTriggered: picker.artSlide = picker.deckPage
    }
    Timer {
        id: artKeepAlive
        interval: 1800
        onTriggered: picker.artStage = 0
    }

    Keys.onPressed: event => {
        if (picker.showingMap || (event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier)))
            return
        const vertical = viewport.activeFocus || entryFocus.activeFocus || pageDots.activeFocus
        const up = event.key === Qt.Key_PageUp || (vertical && event.key === Qt.Key_Up)
        const down = event.key === Qt.Key_PageDown || (vertical && event.key === Qt.Key_Down)
        if (!up && !down)
            return
        event.accepted = true
        if (!event.isAutoRepeat)
            picker.selectDeckPage(picker.deckPage + (up ? -1 : 1), true)
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
        id: slides
        x: picker.railWidth + 9
        width: Math.max(0, picker.width - x)
        height: picker.height
        clip: true

        property real position: picker.showingMap ? 1 : 0
        Behavior on position {
            enabled: picker.visible && !picker.reducedMotion && !!picker.mapSlide
            NumberAnimation {
                id: slideMotion
                duration: 230
                easing.type: Easing.OutCubic
                onRunningChanged: if (!running) Qt.callLater(picker.restorePageFocus)
            }
        }

        FocusScope {
            id: deckPane
            x: -slides.position * width
            width: slides.width
            height: slides.height
            visible: x + width > 0
            enabled: !picker.showingMap && !slideMotion.running

            PageGesture {
                x: widgetArea.x
                y: widgetArea.y + viewport.y
                width: viewport.width
                height: viewport.height
            }

            Item {
                id: widgetArea
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.topMargin: Math.min(14, picker.inset)
                anchors.bottomMargin: Math.min(12, picker.inset)
                anchors.leftMargin: 10
                anchors.rightMargin: 31

                SlideHeading {
                    id: pickHeading
                    objectName: "dockPickHeading"
                    width: parent.width
                    text: qsTr("Pick Something")
                    forward: true
                    hasArrow: !!picker.mapSlide
                    arrowName: "dockMapNext"
                    arrowLabel: qsTr("Open World Map")
                    onNavigate: keyboard => picker.showMap(true, keyboard)
                }

                FocusScope {
                    id: viewport
                    objectName: "dockPickViewport"
                    y: pickHeading.height + 4
                    width: widgetArea.width
                    height: Math.max(0, widgetArea.height - y)
                    clip: true

                    Item {
                        id: pageStack
                        width: viewport.width
                        height: viewport.height
                        property real pagePosition: picker.deckPage
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
                    }
                }
            }

            FocusScope {
                id: pageDots
                width: 27
                height: width * picker.deckPageNames.length
                z: 1
                anchors.right: parent.right
                anchors.rightMargin: 2
                anchors.verticalCenter: parent.verticalCenter
                Repeater {
                    model: picker.deckPageNames.length
                    delegate: Item {
                        id: dot
                        required property int index
                        y: index * height
                        width: pageDots.width
                        height: width
                        Rectangle {
                            anchors.centerIn: parent
                            width: picker.deckPage === dot.index ? 8 : 6
                            height: width
                            radius: width / 2
                            color: picker.deckPage === dot.index ? theme.gold : "#91ffffff"
                        }
                        KeyboardAction {
                            id: dotAction
                            objectName: "dockDeckDot_" + dot.index
                            anchors.fill: parent
                            focusOnPointer: false
                            accessibleName: picker.deckPageNames[dot.index]
                            focusRadius: 13
                            spaceActivates: true
                            onTriggered: picker.selectDeckPage(dot.index, dotAction.activeFocus)
                        }
                    }
                }
            }
        }

        FocusScope {
            id: mapPane
            x: (1 - slides.position) * width
            width: slides.width
            height: slides.height
            visible: !!picker.mapSlide && x < width
            enabled: picker.showingMap && !slideMotion.running

            Loader {
                id: mapPage
                objectName: "dockPickMapPage"
                anchors.fill: parent
                active: picker.visible && mapPane.visible
                sourceComponent: picker.mapSlide
            }

            SlideHeading {
                id: mapHeading
                objectName: "dockMapHeading"
                x: widgetArea.x
                y: widgetArea.y
                width: widgetArea.width
                text: qsTr("World Map")
                arrowName: "dockMapBack"
                arrowLabel: qsTr("Back to Pick Something")
                onNavigate: keyboard => picker.showMap(false, keyboard)
            }
        }
    }

    component SlideHeading: Text {
        id: heading
        property bool forward: false
        property bool hasArrow: true
        property string arrowName
        property string arrowLabel
        signal navigate(bool keyboard)

        function focusArrow() { arrow.forceActiveFocus(Qt.TabFocusReason) }

        height: Math.ceil(implicitHeight)
        horizontalAlignment: Text.AlignHCenter
        leftPadding: hasArrow ? arrow.width + 3 : 0
        rightPadding: leftPadding
        elide: Text.ElideRight
        color: "#fff5e6"
        font.family: theme.display
        font.weight: Font.DemiBold
        font.pixelSize: theme.fontSize(Math.round(Math.max(24, Math.min(40, width / 24, widgetArea.height / 13))))
        fontSizeMode: Text.HorizontalFit
        minimumPixelSize: theme.fontSize(16)
        Accessible.role: Accessible.Heading
        Accessible.name: text

        KeyboardAction {
            id: arrow
            objectName: heading.arrowName
            accessibleName: heading.arrowLabel
            visible: heading.hasArrow
            width: 44
            height: width
            x: heading.forward ? (heading.width + heading.contentWidth) / 2
                               : (heading.width - heading.contentWidth) / 2 - width
            anchors.verticalCenter: parent.verticalCenter
            anchors.verticalCenterOffset: -1
            focusRadius: width / 2
            focusOnPointer: false
            spaceActivates: true
            onTriggered: heading.navigate(activeFocus)

            Rectangle {
                anchors.centerIn: parent
                width: 32
                height: width
                radius: width / 2
                color: "#25ffffff"
                border.color: "#30ffffff"
                opacity: arrow.interactionActive ? 1 : 0
                Behavior on opacity {
                    enabled: !picker.reducedMotion
                    NumberAnimation { duration: 120 }
                }
            }
            Image {
                anchors.centerIn: parent
                width: 28
                height: width
                sourceSize: Qt.size(56, 56)
                source: heading.forward ? "../assets/icons/lucide/chevron-right.svg"
                                        : "../assets/icons/lucide/chevron-left.svg"
                layer.enabled: true
                layer.effect: MultiEffect {
                    colorization: 1
                    colorizationColor: theme.gold
                }
            }
        }
    }

    component PageGesture: MouseArea {
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

    component PickRow: Grid {
        id: row
        required property int rowIndex
        x: Math.min(12, pageStack.width * .025)
        y: rowIndex * pageStack.height
        width: Math.max(0, pageStack.width - 2 * x)
        height: pageStack.height
        columns: 3
        columnSpacing: Math.min(12, width * .025)
        visible: picker.deckPage === rowIndex
            || (pageMotion.running && y + pageStack.y < viewport.height && y + pageStack.y + height > 0)
        enabled: picker.deckPage === rowIndex && !pageMotion.running
        readonly property real cellWidth: Math.max(0, (width - 2 * columnSpacing) / columns)

        function selectPick(column, key) {
            const deck = decks.itemAt(column) as DockPickDeck
            if (deck) deck.selectEntry(key)
        }

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
                medium: modelData.name
                mediumKey: modelData.key
                aspect: modelData.aspect || .71
                entries: picker.entriesFor(modelData.key)
                logoArt: modelData.key === "universes"
                reducedMotion: picker.reducedMotion
                artRadius: {
                    if (picker.artStage && picker.artSlide === row.rowIndex)
                        return Math.min(picker.artStage, 2)
                    const adjacent = picker.artSlide < 2 ? picker.artSlide + 1 : picker.artSlide - 1
                    if (picker.artStage === 4 && row.rowIndex === adjacent)
                        return 1
                    if ((picker.visible && row.visible) || picker.artStage >= 3)
                        return 0
                    return -1
                }
                onActiveFocusChanged: if (activeFocus) picker.focusColumn = index
                onVerticalSwipeRequested: dy => picker.pageSwipe(dy)
                onOpenRequested: entry => picker.pickRequested(modelData.key, entry, {
                    medium: modelData.key, entry: entry, key: deck.identity(entry),
                    slide: row.rowIndex, column: index, keyboard: deck.activeFocus
                })
            }
        }
    }
}
