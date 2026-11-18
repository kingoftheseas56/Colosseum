import QtQuick
import QtTest
import "../../qml" as App

Item {
    width: 1280
    height: 720

    Rectangle { id: scene; anchors.fill: parent; color: "#26354e" }
    Component { id: mapComponent; Item {} }
    App.DockPickerCatalog { id: catalogue }
    Component {
        id: taskbarComponent
        App.Taskbar {
            pickerScene: scene
            localMediaAvailable: true
            pickerPicks: ({
                tv: [
                    { title: "First", cover: Qt.resolvedUrl("../../assets/wallpaper/captured-motion.jpg") },
                    { title: "Second", cover: Qt.resolvedUrl("../../assets/wallpaper/cold-ripple.jpg") },
                    { title: "Third", cover: Qt.resolvedUrl("../../assets/wallpaper/sagas/dune.jpg") }
                ]
            })
        }
    }

    TestCase {
        id: suite
        name: "DesktopDockPicker"
        when: windowShown
        property Item bar
        property real widthAtChooser: 0
        Connections {
            target: suite.bar
            function onOpenMediaClicked() {
                suite.widthAtChooser = suite.findChild(suite.bar, "taskbarDock").width
            }
        }
        SignalSpy { id: picks; target: suite.bar; signalName: "pickerPickRequested" }
        SignalSpy { id: wallpapers; target: suite.bar; signalName: "pickerWallpaperRequested" }
        SignalSpy { id: files; target: suite.bar; signalName: "openMediaClicked" }

        function init() {
            bar = createTemporaryObject(taskbarComponent, scene.parent)
            verify(bar !== null)
            picks.clear()
            wallpapers.clear()
            files.clear()
            widthAtChooser = 0
        }

        function test_dragTucksTheSameCardAndPreservesSelection() {
            bar.open = true
            const picker = findChild(bar, "dockPicker")
            const deck = findChild(picker, "dockDeck_tv")
            const card = findChild(deck, "dockPickCard_tv")
            verify(waitForRendering(picker))
            verify(picker.visible)
            const dock = findChild(bar, "taskbarDock")
            tryCompare(dock, "width", picker.width)
            compare(picker.x, dock.x)
            compare(picker.x + picker.width, bar.width - picker.x)
            compare(picker.height, 560)
            tryVerify(() => card.height >= 148)
            const labels = findChild(deck, "dockPickLabels_tv")
            const heading = findChild(picker, "dockPickHeading")
            compare(heading.text, "Pick Something")
            verify(heading.visible)
            verify(card.x + card.width < labels.x)
            compare(labels.children[0].font.family, "Cinzel")
            compare(labels.children[0].font.pixelSize, 14)
            compare(labels.children[1].font.family, "Fraunces")
            compare(labels.children[1].font.pixelSize, 17)
            const x = card.width / 2
            const y = card.height / 2
            mouseMove(card, card.width * .8, card.height * .2)
            tryVerify(() => card.hoverTilt > 0 && card.hoverPitch > 0)
            mousePress(card, x, y)
            compare(card.hoverTilt, 0)
            compare(card.hoverPitch, 0)
            verify(card.pressed, "Press reaches the front cover")
            mouseMove(card, x - 11, y, 20, Qt.LeftButton)
            verify(deck.shift < 0)
            mouseRelease(card, x - 11, y)
            tryCompare(deck, "moving", -1)
            compare(deck.current, 0)
            compare(picks.count, 0)

            mousePress(card, x, y)
            mouseMove(card, x - 48, y, 20, Qt.LeftButton)
            compare(deck.moving, 0)
            mouseRelease(card, x - 48, y)
            tryVerify(() => deck.behind && deck.moving === 0 && deck.current === 1)
            tryCompare(deck, "moving", -1)
            compare(picks.count, 0)

            compare(deck.current, 1)
            mouseClick(card)
            compare(picks.count, 1)
            compare(picks.signalArguments[0][0], "tv")
            compare(picks.signalArguments[0][1].title, "Second")
            verify(!picker.visible)
        }

        function test_verticalSwipeChangesOnlyTheSlide() {
            bar.pickerReducedMotion = true
            bar.open = true
            const picker = findChild(bar, "dockPicker")
            const deck = findChild(picker, "dockDeck_tv")
            const card = findChild(deck, "dockPickCard_tv")
            verify(waitForRendering(picker))
            verify(findChild(picker, "dockSlideDot_0") === null)
            bar.pickerMapSlide = mapComponent
            tryVerify(() => findChild(picker, "dockSlideDot_1") !== null)
            const x = card.width / 2
            const y = card.height / 2

            mousePress(card, x, y)
            mouseMove(card, x + 1, y - 15, 20, Qt.LeftButton)
            mouseMove(card, x - 60, y - 55, 20, Qt.LeftButton)
            mouseRelease(card, x - 60, y - 55)
            tryCompare(picker, "slide", 1)
            compare(deck.current, 0)
            compare(deck.moving, -1)
            compare(picks.count, 0)

            mouseClick(findChild(picker, "dockSlideDot_0"))
            tryCompare(picker, "slide", 0)
            compare(deck.current, 0)
        }

        function test_railActionsAndCompactMediaHandoff() {
            bar.open = true
            const picker = findChild(bar, "dockPicker")
            verify(waitForRendering(picker))
            mouseClick(findChild(picker, "dockShortcut_wallpaper"))
            compare(wallpapers.count, 1)
            verify(!picker.visible)

            bar.open = true
            tryCompare(findChild(bar, "taskbarDock"), "width", bar.pickerWidth)
            const shortcut = findChild(picker, "dockShortcut_media")
            mouseClick(shortcut)
            tryCompare(files, "count", 1)
            compare(widthAtChooser, bar.closedWidth)
            verify(!bar.open)

            bar.open = true
            verify(!shortcut.activeFocus)
            shortcut.forceActiveFocus(Qt.TabFocusReason)
            verify(shortcut.activeFocus)
            keyClick(Qt.Key_Return)
            tryCompare(files, "count", 2)
            compare(widthAtChooser, bar.closedWidth)
            mouseClick(findChild(bar, "taskbarToggleAction"))
            verify(picker.visible)
            verify(!shortcut.activeFocus, "Reopening does not restore a stale shortcut focus")
            keyClick(Qt.Key_Tab)
            verify(shortcut.activeFocus, "The keyboard can still reach the first shortcut")
        }

        function test_pageIconsFollowExistingDestinationState() {
            const extensions = findChild(bar, "taskbarExtensions")
            const activity = findChild(bar, "taskbarHistoryStats")
            verify(waitForRendering(bar))
            verify(!extensions.visible && !activity.visible)
            bar.extensionsActive = true
            verify(extensions.visible && !activity.visible)
            tryCompare(findChild(bar, "taskbarDock"), "width", 310)
            bar.extensionsActive = false
            bar.historyStatsActive = true
            verify(!extensions.visible && activity.visible)
            bar.historyStatsActive = false
            bar.focusHistoryStatsAction()
            verify(!activity.visible)
            verify(!bar.open)
            verify(findChild(bar, "taskbarToggleAction").activeFocus)
        }

        function test_manhwaAndUniverseLogoDecks() {
            catalogue.updateUniverses()
            const entries = catalogue.picks.universes
            verify(entries.length > 1)
            verify(entries.every(entry => entry.raw.page && entry.raw.logo))
            bar.pickerPicks = { universes: entries, manhwa: [{ id: "123", title: "Manhwa pick" }] }
            bar.open = true
            const picker = findChild(bar, "dockPicker")
            verify(waitForRendering(picker))
            const manhwa = findChild(picker, "dockPickCard_manhwa")
            const universes = findChild(picker, "dockPickCard_universes")
            compare(universes.width, manhwa.width)
            verify(findChild(picker, "dockDeck_music") === null)
            verify(findChild(picker, "dockDeck_tributes") === null)
            tryVerify(() => findChild(picker, "dockUniverseLogo")?.status === Image.Ready)
            mouseClick(universes)
            compare(picks.signalArguments[0][0], "universes")
            compare(picks.signalArguments[0][1].raw.page, entries[0].raw.page)
            bar.open = true
            mouseClick(manhwa)
            compare(picks.signalArguments[1][0], "manhwa")
        }

        function test_audiobooksHeadingFitsWithAndWithoutArtwork() {
            bar.open = true
            const picker = findChild(bar, "dockPicker")
            verify(waitForRendering(picker))
            const labels = findChild(picker, "dockPickLabels_audiobooks")
            const heading = labels.children[0]
            compare(heading.text, "Audiobooks")
            verify(!heading.truncated)
            const emptyWidth = labels.width
            bar.pickerPicks = { audiobooks: [{ id: "book", title: "An audiobook" }] }
            tryVerify(() => labels.width < emptyWidth)
            tryVerify(() => !heading.truncated && heading.contentWidth <= heading.width)
        }

        function test_autoRevealKeepsPickerClosed() {
            bar.reveal()
            verify(bar.open)
            verify(!bar.pickerShown)
            bar.open = false
            mouseClick(findChild(bar, "taskbarToggleAction"))
            verify(bar.pickerShown)
            bar.reveal()
            verify(bar.pickerShown)
        }
    }
}
