import QtQuick
import QtQuick.Controls
import QtTest
import "../../qml" as App

Item {
    id: root
    width: 1280
    height: 720

    Rectangle { id: scene; anchors.fill: parent; color: "#26354e" }
    App.TooltipStyle { backdrop: root }
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
            root.width = 1280
            root.height = 720
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
            compare(picker.y, 20)
            const mark = findChild(deck, "dockPickMark_tv")
            const markPosition = Qt.point(mark.x, mark.y)
            const stage = findChild(deck, "dockPickStage_tv")
            const x = card.width / 2
            const y = card.height / 2
            mouseMove(card, card.width * .8, card.height * .2)
            tryVerify(() => card.hoverTilt > 0 && card.hoverPitch > 0)
            tryVerify(() => stage.ToolTip.visible)
            mousePress(card, x, y)
            compare(card.hoverTilt, 0)
            compare(card.hoverPitch, 0)
            verify(card.pressed, "Press reaches the front cover")
            tryVerify(() => !stage.ToolTip.visible)
            mouseMove(card, x - 11, y, 20, Qt.LeftButton)
            verify(deck.shift < 0)
            mouseRelease(card, x - 11, y)
            tryCompare(deck, "moving", -1)
            compare(deck.current, 0)
            compare(picks.count, 0)

            mousePress(card, x, y)
            mouseMove(card, x - 48, y, 20, Qt.LeftButton)
            compare(deck.moving, 0)
            checkDeckBounds(deck)
            compare(Qt.point(mark.x, mark.y), markPosition)
            mouseRelease(card, x - 48, y)
            tryVerify(() => deck.behind && deck.moving === 0 && deck.current === 1)
            verify(!stage.ToolTip.visible)
            checkDeckBounds(deck)
            compare(Qt.point(mark.x, mark.y), markPosition)
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

        function checkDeckBounds(deck) {
            let left = deck.width
            let right = 0
            for (let i = 0; i < Math.min(3, deck.count); ++i) {
                const face = findChild(deck, "dockPickFace_" + i)
                const bounds = face.mapToItem(deck, Qt.rect(0, 0, face.width, face.height))
                verify(bounds.x >= 0 && bounds.x + bounds.width <= deck.width + 1, "Card stays inside its column")
                verify(bounds.y >= 0 && bounds.y + bounds.height <= deck.height + 1, "Card stays inside its row")
                left = Math.min(left, bounds.x)
                right = Math.max(right, bounds.x + bounds.width)
            }
            if (deck.moving < 0)
                fuzzyCompare((left + right) / 2, deck.width / 2, 1)
            return right
        }

        function test_postersScaleAndMarksLoad() {
            bar.open = true
            const picker = findChild(bar, "dockPicker")
            verify(waitForRendering(picker))
            const heading = findChild(picker, "dockPickHeading")
            compare(heading.text, "Pick Something")
            compare(heading.horizontalAlignment, Text.AlignHCenter)
            compare(heading.width, heading.parent.width)
            mouseMove(heading)

            const keys = ["manga", "comics", "manhwa", "books", "audiobooks", "anime", "tv", "movies", "universes"]
            const entries = bar.pickerPicks.tv
            const allPicks = {}
            for (const key of keys)
                allPicks[key] = entries
            bar.pickerPicks = allPicks
            const stdHeight = findChild(picker, "dockPickCard_tv").height
            verify(stdHeight > 160, "TV poster height at 1280×720: " + stdHeight)
            const iconSources = new Set()
            for (const key of keys) {
                const deck = findChild(picker, "dockDeck_" + key)
                const mark = findChild(deck, "dockPickMark_" + key)
                tryCompare(mark, "status", Image.Ready)
                iconSources.add(mark.source.toString())
                verify(mark.x > checkDeckBounds(deck), "The mark sits beside the resting stack")
                verify(mark.x + mark.width <= deck.width && mark.y + mark.height <= deck.height)
                verify(findChild(deck, "dockPickLabels_" + key) === null)
            }
            compare(iconSources.size, 9)
            const audio = findChild(picker, "dockPickCard_audiobooks")
            compare(audio.width, audio.height)
            root.width = 1600
            root.height = 900
            tryVerify(() => findChild(picker, "dockPickCard_tv").height >= stdHeight + 40)
            for (const key of keys)
                checkDeckBounds(findChild(picker, "dockDeck_" + key))
            console.info("TV poster height (1280×720 → 1600×900):",
                stdHeight.toFixed(1), "→", findChild(picker, "dockPickCard_tv").height.toFixed(1))
        }

        function test_tooltipOnHoverAndKeyboardWithReducedMotion() {
            const fullTitle = "A very long title that takes more than three tooltip lines to render in the picker. The final words also stay visible."
            bar.pickerPicks = { tv: [{ title: fullTitle }, { title: "Second" }] }
            bar.pickerReducedMotion = true
            bar.open = true
            const picker = findChild(bar, "dockPicker")
            verify(waitForRendering(picker))
            const audio = findChild(picker, "dockPickStage_audiobooks")
            const audioMark = findChild(picker, "dockPickMark_audiobooks")
            mouseMove(audioMark)
            verify(!audio.ToolTip.visible)
            tryVerify(() => audio.ToolTip.visible)
            compare(audio.ToolTip.toolTip.text, "Audiobooks\nNo picks available")
            const card = findChild(picker, "dockPickCard_tv")
            const tv = findChild(picker, "dockPickStage_tv")
            mouseMove(card)
            tryVerify(() => tv.ToolTip.visible)
            compare(tv.ToolTip.toolTip.text, fullTitle + "\nTV")
            compare(tv.Accessible.name, "Open " + fullTitle + " (TV)")
            const label = tv.ToolTip.toolTip.contentItem.children[0]
            tryVerify(() => label.lineCount > 3)
            verify(!label.truncated)
            mouseMove(findChild(picker, "dockPickHeading"))
            tryVerify(() => !tv.ToolTip.visible)
            tv.forceActiveFocus(Qt.TabFocusReason)
            tryVerify(() => tv.ToolTip.visible)
            keyClick(Qt.Key_Right)
            tryVerify(() => tv.ToolTip.visible && tv.ToolTip.toolTip.text === "Second\nTV")
            keyClick(Qt.Key_Return)
            compare(picks.signalArguments[0][1].title, "Second")
            tryVerify(() => !tv.ToolTip.visible)
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
