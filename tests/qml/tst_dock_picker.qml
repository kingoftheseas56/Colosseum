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
            const title = findChild(deck, "dockPickTitle_tv")
            const x = card.width / 2
            const y = card.height / 2
            mouseMove(card, card.width * .8, card.height * .2)
            tryVerify(() => card.hoverTilt > 0 && card.hoverPitch > 0)
            verify(title.visible)
            compare(title.text, "First")
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
            checkDeckBounds(deck)
            mouseRelease(card, x - 48, y)
            tryVerify(() => deck.behind && deck.moving === 0 && deck.current === 1)
            checkDeckBounds(deck)
            compare(title.text, "Second")
            tryCompare(deck, "moving", -1)
            compare(picks.count, 0)

            compare(deck.current, 1)
            mouseClick(card)
            compare(picks.count, 1)
            compare(picks.signalArguments[0][0], "tv")
            compare(picks.signalArguments[0][1].title, "Second")
            verify(!picker.visible)
        }

        function test_verticalDragAndWheelScrollPicksWithoutSwitchingSlides() {
            bar.pickerReducedMotion = true
            bar.open = true
            const picker = findChild(bar, "dockPicker")
            const deck = findChild(picker, "dockDeck_tv")
            const card = findChild(deck, "dockPickCard_tv")
            verify(waitForRendering(picker))
            verify(findChild(picker, "dockSlideDot_0") === null)
            bar.pickerMapSlide = mapComponent
            tryVerify(() => findChild(picker, "dockSlideDot_1") !== null)
            const scroll = findChild(picker, "dockPickScroll")
            const heading = findChild(picker, "dockPickHeading")
            const headingPosition = heading.mapToItem(root, 0, 0)
            const start = card.mapToItem(scroll, card.width / 2, card.height * .8)

            mousePress(scroll, start.x, start.y)
            mouseMove(scroll, start.x + 1, start.y - 15, 20, Qt.LeftButton)
            mouseMove(scroll, start.x - 60, start.y - 75, 20, Qt.LeftButton)
            mouseRelease(scroll, start.x - 60, start.y - 75)
            fuzzyCompare(scroll.contentY, 75, 1)
            compare(picker.slide, 0)
            compare(deck.current, 0)
            compare(deck.moving, -1)
            compare(picks.count, 0)
            compare(heading.mapToItem(root, 0, 0), headingPosition)

            mouseWheel(scroll, scroll.width / 2, scroll.height / 2, 0, -120)
            tryVerify(() => scroll.contentY > 75)
            compare(picker.slide, 0)
            mouseClick(findChild(picker, "dockSlideDot_1"))
            tryCompare(picker, "slide", 1)
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
            const logo = findChild(picker, "dockUniverseLogo")
            tryCompare(logo, "status", Image.Ready)
            compare(logo.fillMode, Image.PreserveAspectFit)
            const franchises = findChild(picker, "dockPickStage_universes")
            compare(franchises.Accessible.name, "Open " + entries[0].title + " (Franchises)")
            franchises.forceActiveFocus(Qt.TabFocusReason)
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
            const motion = deck.moving >= 0
            const area = motion ? findChild(bar, "dockPickScroll").contentItem : deck
            for (let i = 0; i < Math.min(3, deck.count); ++i) {
                const face = findChild(deck, "dockPickFace_" + i)
                const bounds = face.mapToItem(area, Qt.rect(0, 0, face.width, face.height))
                verify(bounds.x >= -1 && bounds.x + bounds.width <= area.width + 1,
                    "Card stays inside " + (motion ? "the picker" : "its column") + ": " + deck.medium)
                verify(bounds.y >= -1 && bounds.y + bounds.height <= area.height + 1,
                    "Card stays inside " + (motion ? "the picker" : "its row") + ": " + deck.medium)
                left = Math.min(left, bounds.x)
                right = Math.max(right, bounds.x + bounds.width)
            }
            if (!motion)
                fuzzyCompare((left + right) / 2, deck.artWidth / 2, 1)
        }

        function checkRows(picker, rows) {
            const grid = findChild(picker, "dockPickGrid")
            grid.forceLayout()
            let rowBottom = 0
            for (const row of rows) {
                let deckRight = 0
                let firstX = -1
                let rowY = -1
                for (const key of row) {
                    const deck = findChild(picker, "dockDeck_" + key)
                    const label = findChild(deck, "dockPickMedium_" + key)
                    const title = findChild(deck, "dockPickTitle_" + key)
                    verify(deck.visible && deck.coverHeight > 0 && label.visible && title.visible)
                    verify(!label.truncated && !title.truncated)
                    compare(label.text, key === "tv" ? "TV"
                                                    : key === "universes" ? "Franchises"
                                                                         : key[0].toUpperCase() + key.slice(1))
                    const image = findChild(deck, "dockPosterArt_img")
                    if (key !== "universes")
                        compare(image.fillMode, Image.PreserveAspectFit)
                    checkDeckBounds(deck)
                    verify(label.parent.x >= 0 && label.parent.x + label.width <= deck.width + 1)
                    verify(title.parent.y + title.y + title.height <= deck.height + 1)
                    if (rowY < 0) {
                        firstX = deck.x
                        rowY = deck.y
                    }
                    compare(deck.y, rowY)
                    verify(deck.x >= deckRight && deck.y >= rowBottom && deck.y + deck.height <= grid.height + 1)
                    deckRight = deck.x + deck.width
                }
                fuzzyCompare((firstX + deckRight) / 2, grid.width / 2, 1)
                rowBottom = rowY + findChild(picker, "dockDeck_" + row[0]).height
            }
        }

        function test_fixedRowsKeepReadableTitlesAndLargerArtwork() {
            bar.open = true
            const picker = findChild(bar, "dockPicker")
            verify(waitForRendering(picker))
            const heading = findChild(picker, "dockPickHeading")
            compare(heading.text, "Pick Something")
            compare(heading.horizontalAlignment, Text.AlignHCenter)
            compare(heading.width, heading.parent.width)
            mouseMove(heading)

            const rows = [["anime", "tv", "movies"], ["manga", "comics", "manhwa"], ["books", "audiobooks", "universes"]]
            const entries = bar.pickerPicks.tv
            const allPicks = {}
            for (const row of rows)
                for (const key of row)
                    allPicks[key] = entries
            bar.pickerPicks = allPicks
            const tv = findChild(picker, "dockPickCard_tv")
            const scroll = findChild(picker, "dockPickScroll")
            const stdHeight = tv.height
            verify(stdHeight >= 215, "TV poster height at 1280x720: " + stdHeight)
            checkRows(picker, rows)
            const audio = findChild(picker, "dockPickCard_audiobooks")
            compare(audio.width, audio.height)
            const universe = findChild(picker, "dockPickCard_universes")
            compare(universe.height, tv.height)
            root.width = 1264
            root.height = 656
            tryCompare(bar, "width", 1264)
            verify(tv.height >= 215, "TV poster height at 1264x656: " + tv.height)
            verify(scroll.contentHeight > scroll.height)
            verify(scroll.height >= 2 * findChild(picker, "dockDeck_tv").height + 8, "Two complete rows fit")
            compare(scroll.contentY, 0)
            checkRows(picker, rows)
            const windowedHeight = tv.height
            scroll.contentY = scroll.contentHeight - scroll.height
            root.width = 1600
            root.height = 900
            tryVerify(() => tv.height > stdHeight)
            verify(!scroll.interactive, "Tall windows can display all three rows at once")
            tryCompare(scroll, "contentY", 0)
            checkRows(picker, rows)
            console.info("TV poster height (1264x656 / 1280x720 / 1600x900):",
                windowedHeight.toFixed(1), "/", stdHeight.toFixed(1), "/", tv.height.toFixed(1))
            root.width = 900
            root.height = 720
            tryCompare(findChild(picker, "dockPickGrid"), "labelsBeside", false)
            verify(scroll.interactive)
            checkRows(picker, rows)
        }

        function test_visibleLabelAndAccessibleTitleReplaceTooltip() {
            const fullTitle = "The remarkably long title of the illustrated television series"
            bar.pickerPicks = { tv: [{ title: fullTitle }] }
            bar.pickerReducedMotion = true
            bar.open = true
            const picker = findChild(bar, "dockPicker")
            verify(waitForRendering(picker))
            const card = findChild(picker, "dockPickCard_tv")
            const tv = findChild(picker, "dockPickStage_tv")
            const audio = findChild(picker, "dockPickStage_audiobooks")
            const label = findChild(picker, "dockPickTitle_tv")
            compare(label.text, fullTitle)
            verify(label.visible && label.lineCount > 1)
            verify(!label.truncated, "Title width " + label.width + ", line count " + label.lineCount
                + " of " + label.maximumLineCount + ", font px " + label.font.pixelSize)
            mouseMove(card)
            tv.forceActiveFocus(Qt.TabFocusReason)
            wait(400)
            verify(!card.ToolTip.toolTip.visible)
            compare(tv.Accessible.name, "Open " + fullTitle + " (TV)")
            compare(audio.Accessible.name, "Audiobooks")
            compare(audio.Accessible.description, "No picks available")
            compare(findChild(picker, "dockPickTitle_audiobooks").text, "No picks available")
        }

        function test_keyboardBrowsesCardsAndRevealsTheThirdRow() {
            bar.pickerPicks = {
                anime: [{ title: "Anime pick" }],
                tv: [{ title: "First" }, { title: "Second" }],
                comics: [{ title: "Comic pick" }],
                audiobooks: [{ title: "An audiobook" }]
            }
            bar.pickerReducedMotion = true
            bar.open = true
            const picker = findChild(bar, "dockPicker")
            verify(waitForRendering(picker))
            const scroll = findChild(picker, "dockPickScroll")
            const tv = findChild(picker, "dockPickStage_tv")
            const audio = findChild(picker, "dockPickStage_audiobooks")
            const comics = findChild(picker, "dockPickStage_comics")
            findChild(picker, "dockPickStage_anime").forceActiveFocus(Qt.TabFocusReason)
            keyClick(Qt.Key_Tab)
            verify(tv.activeFocus)
            keyClick(Qt.Key_Right)
            tryCompare(findChild(picker, "dockPickTitle_tv"), "text", "Second")
            keyClick(Qt.Key_Down)
            verify(comics.activeFocus)
            keyClick(Qt.Key_Down)
            verify(audio.activeFocus)
            const rect = audio.mapToItem(scroll, Qt.rect(0, 0, audio.width, audio.height))
            verify(rect.y >= -1 && rect.y + rect.height <= scroll.height + 1)
            verify(scroll.contentY > 0)
            keyClick(Qt.Key_Return)
            compare(picks.signalArguments[0][0], "audiobooks")
            compare(picks.signalArguments[0][1].title, "An audiobook")
        }

        function test_touchPansTheWidgetWithoutSelectingACover() {
            bar.open = true
            const picker = findChild(bar, "dockPicker")
            verify(waitForRendering(picker))
            const scroll = findChild(picker, "dockPickScroll")
            const card = findChild(picker, "dockPickCard_tv")
            const start = card.mapToItem(scroll, card.width / 2, card.height * .8)
            const finger = touchEvent(scroll)
            finger.press(0, scroll, start.x, start.y).commit()
            finger.move(0, scroll, start.x, start.y - 20).commit()
            finger.move(0, scroll, start.x, start.y - 85).commit()
            finger.release(0, scroll, start.x, start.y - 85).commit()
            tryVerify(() => scroll.contentY > 30)
            compare(picks.count, 0)
            compare(findChild(picker, "dockDeck_tv").current, 0)
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
